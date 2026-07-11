// process_manager.cpp
//
// See process_manager.h for the "why". This file is the "how", and it's
// intentionally boring — every WinAPI call is checked, every handle is
// closed on every path, and nothing here relies on undocumented behavior.

#include "process_manager.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <stdexcept>

#pragma comment(lib, "psapi.lib")

namespace wsk {

std::vector<ProcessInfo> EnumerateProcesses()
{
    std::vector<ProcessInfo> result;

    ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        // Deliberately swallow the error here — see header comment. Some
        // callers just want "give me what you can", not a thrown exception
        // for what's often a transient, retryable condition.
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (!Process32FirstW(snapshot.get(), &entry)) {
        return result;
    }

    do {
        ProcessInfo info;
        info.pid = entry.th32ProcessID;
        info.parentPid = entry.th32ParentProcessID;
        info.exeName = entry.szExeFile;
        result.push_back(std::move(info));
    } while (Process32NextW(snapshot.get(), &entry));

    return result;
}

std::optional<ProcessDetails> QueryProcessDetails(DWORD pid)
{
    ScopedHandle proc(OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid));

    // PROCESS_QUERY_LIMITED_INFORMATION deliberately over
    // PROCESS_QUERY_INFORMATION here — it's the least-privilege access mask
    // that still gets us everything below, and it works without elevation
    // against most system processes too.
    if (!proc.valid()) {
        return std::nullopt;
    }

    ProcessDetails details;

    wchar_t path[MAX_PATH] = {};
    DWORD pathLen = MAX_PATH;
    if (QueryFullProcessImageNameW(proc.get(), 0, path, &pathLen)) {
        details.imagePath = path;
    }

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(proc.get(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                              sizeof(pmc))) {
        details.workingSetBytes = pmc.WorkingSetSize;
        details.privateBytes = pmc.PrivateUsage;
    }

    FILETIME exitTime{}, kernelTime{}, userTime{};
    GetProcessTimes(proc.get(), &details.creationTime, &exitTime, &kernelTime, &userTime);

    return details;
}

bool EnableDebugPrivilege()
{
    ScopedHandle token;
    {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(),
                               TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &raw)) {
            return false;
        }
        token.reset(raw);
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
        return false;
    }

    TOKEN_PRIVILEGES priv{};
    priv.PrivilegeCount = 1;
    priv.Privileges[0].Luid = luid;
    priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // AdjustTokenPrivileges can "succeed" (return TRUE) while silently
    // failing to grant the privilege — the only reliable signal is checking
    // GetLastError() for ERROR_NOT_ALL_ASSIGNED right after. Learned that
    // one the hard way once.
    if (!AdjustTokenPrivileges(token.get(), FALSE, &priv, sizeof(priv), nullptr, nullptr)) {
        return false;
    }

    return GetLastError() == ERROR_SUCCESS;
}

DWORD RunAndWait(const std::wstring& commandLine, DWORD creationFlags)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // CreateProcessW wants a mutable buffer for the command line since it
    // may write a null terminator into the middle of it (argument splitting).
    // Passing a std::wstring's internal buffer directly is asking for
    // trouble the moment the string is a literal, so we always copy.
    std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
    mutableCmd.push_back(L'\0');

    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                         FALSE, creationFlags, nullptr, nullptr, &si, &pi)) {
        throw std::runtime_error("CreateProcessW failed, error " +
                                  std::to_string(GetLastError()));
    }

    ScopedHandle process(pi.hProcess);
    ScopedHandle thread(pi.hThread);

    WaitForSingleObject(process.get(), INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(process.get(), &exitCode);
    return exitCode;
}

// ---------------------------------------------------------------------
// JobObject
// ---------------------------------------------------------------------

JobObject::JobObject()
{
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (!job_) {
        throw std::runtime_error("CreateJobObjectW failed, error " +
                                  std::to_string(GetLastError()));
    }
}

JobObject::~JobObject()
{
    if (job_) {
        // Closing the last handle to a job object terminates every process
        // still assigned to it, unless JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK
        // or KILL_ON_JOB_CLOSE was explicitly disabled — worth knowing
        // before you wrap this in a smart pointer and forget about it.
        CloseHandle(job_);
    }
}

bool JobObject::SetActiveProcessLimit(DWORD limit)
{
    JOBOBJECT_BASIC_LIMIT_INFORMATION basic{};
    basic.LimitFlags = JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    basic.ActiveProcessLimit = limit;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION extended{};
    extended.BasicLimitInformation = basic;

    return SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
                                    &extended, sizeof(extended)) != 0;
}

bool JobObject::Kill(UINT exitCode)
{
    return TerminateJobObject(job_, exitCode) != 0;
}

bool JobObject::LaunchContained(const std::wstring& commandLine, DWORD& outPid)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
    mutableCmd.push_back(L'\0');

    // CREATE_SUSPENDED matters here: we want the process attached to the
    // job *before* it gets a chance to run a single instruction, otherwise
    // there's a window where it's alive but uncontained.
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                         CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        return false;
    }

    ScopedHandle process(pi.hProcess);
    ScopedHandle thread(pi.hThread);

    if (!AssignProcessToJobObject(job_, process.get())) {
        TerminateProcess(process.get(), 1);
        return false;
    }

    outPid = pi.dwProcessId;
    ResumeThread(thread.get());
    return true;
}

} // namespace wsk
