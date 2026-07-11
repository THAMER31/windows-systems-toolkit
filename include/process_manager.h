// process_manager.h
//
// A thin, RAII-friendly wrapper around the process-related corners of the
// Win32 API that I actually end up using in real tooling: enumeration,
// spawning, privilege elevation, and Job Object containment.
//
// Nothing here is exotic on purpose. The goal isn't to show off undocumented
// NT internals, it's to show that I know the *correct* way to hold handles,
// check every return value, and not leak a single HANDLE across error paths.
//
// Author: (your name)

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <optional>

namespace wsk {

// Plain-old-data snapshot of one row from the process list. Deliberately
// not pulling in the full PROCESSENTRY32 struct — callers shouldn't have
// to know about Toolhelp32 to read a PID.
struct ProcessInfo {
    DWORD pid = 0;
    DWORD parentPid = 0;
    std::wstring exeName;
};

// Extra detail that costs a handle + a couple of syscalls to obtain, so it's
// kept separate from ProcessInfo rather than eagerly filled in for every
// process on the system.
struct ProcessDetails {
    std::wstring imagePath;
    SIZE_T workingSetBytes = 0;
    SIZE_T privateBytes = 0;
    FILETIME creationTime{};
};

// RAII wrapper so a HANDLE can never outlive its owner by accident.
// Move-only, same spirit as std::unique_ptr.
class ScopedHandle {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE h) : handle_(h) {}
    ~ScopedHandle() { reset(); }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.release()) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.release();
        }
        return *this;
    }

    HANDLE get() const { return handle_; }
    bool valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    HANDLE release() {
        HANDLE h = handle_;
        handle_ = nullptr;
        return h;
    }

    void reset(HANDLE h = nullptr) {
        if (valid()) CloseHandle(handle_);
        handle_ = h;
    }

private:
    HANDLE handle_ = nullptr;
};

// Walks the system-wide process snapshot. Returns an empty vector (not an
// exception) on failure — call GetLastError() yourself if you need the why;
// this is a toolkit function, not a place to impose an error-handling
// philosophy on the caller.
std::vector<ProcessInfo> EnumerateProcesses();

// Best-effort detail lookup. Returns std::nullopt if the process can't be
// opened — which happens a lot for system/protected processes even when
// running elevated, so callers need to expect it.
std::optional<ProcessDetails> QueryProcessDetails(DWORD pid);

// Tries to enable SeDebugPrivilege on the current process token. This is
// what lets you OpenProcess() things you don't otherwise own. Returns false
// if the token doesn't have the privilege available at all (i.e. the process
// isn't running elevated) — that's an expected outcome, not an error.
bool EnableDebugPrivilege();

// Launches a child process and blocks until it exits, returning its exit
// code. Throws std::runtime_error on launch failure (as opposed to the
// enumeration functions above) because a failed launch is almost always a
// programmer error worth surfacing loudly.
DWORD RunAndWait(const std::wstring& commandLine, DWORD creationFlags = 0);

// A Job Object wrapper for corralling a process (and anything it spawns)
// under one containment boundary. Useful anywhere you want "kill this and
// everything it started" semantics, or a hard cap on how many processes a
// subtree can fork.
class JobObject {
public:
    JobObject();
    ~JobObject();

    JobObject(const JobObject&) = delete;
    JobObject& operator=(const JobObject&) = delete;

    // Caps the number of simultaneously active processes inside the job.
    bool SetActiveProcessLimit(DWORD limit);

    // Killing the job terminates every process still attached to it,
    // regardless of whether we're still holding a handle to them.
    bool Kill(UINT exitCode = 1);

    // Starts commandLine suspended, attaches it to the job, then resumes it.
    // Suspended-then-attach avoids the race where the process does something
    // observable before it's actually contained.
    bool LaunchContained(const std::wstring& commandLine, DWORD& outPid);

private:
    HANDLE job_ = nullptr;
};

} // namespace wsk
