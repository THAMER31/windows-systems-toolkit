// service_control.cpp

#include "service_control.h"
#include "process_manager.h" // reusing ScopedHandle-style RAII pattern below

namespace wsk {

namespace {

// SC_HANDLE isn't a plain HANDLE, so it needs its own tiny RAII guard
// rather than reusing wsk::ScopedHandle — CloseServiceHandle vs CloseHandle,
// small but very much not interchangeable.
class ScopedScHandle {
public:
    explicit ScopedScHandle(SC_HANDLE h) : handle_(h) {}
    ~ScopedScHandle() { if (handle_) CloseServiceHandle(handle_); }
    ScopedScHandle(const ScopedScHandle&) = delete;
    ScopedScHandle& operator=(const ScopedScHandle&) = delete;
    SC_HANDLE get() const { return handle_; }
    bool valid() const { return handle_ != nullptr; }
private:
    SC_HANDLE handle_;
};

ServiceState ToServiceState(DWORD win32State)
{
    switch (win32State) {
        case SERVICE_STOPPED:          return ServiceState::Stopped;
        case SERVICE_START_PENDING:    return ServiceState::StartPending;
        case SERVICE_STOP_PENDING:     return ServiceState::StopPending;
        case SERVICE_RUNNING:          return ServiceState::Running;
        case SERVICE_CONTINUE_PENDING: return ServiceState::ContinuePending;
        case SERVICE_PAUSE_PENDING:    return ServiceState::PausePending;
        case SERVICE_PAUSED:           return ServiceState::Paused;
        default:                       return ServiceState::Unknown;
    }
}

} // namespace

bool InstallService(const std::wstring& serviceName,
                     const std::wstring& displayName,
                     const std::wstring& binaryPath,
                     bool autoStart)
{
    ScopedScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE));
    if (!scm.valid()) {
        return false; // most common cause: not running elevated
    }

    ScopedScHandle svc(CreateServiceW(
        scm.get(),
        serviceName.c_str(),
        displayName.c_str(),
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        autoStart ? SERVICE_AUTO_START : SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        binaryPath.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr
    ));

    return svc.valid();
}

bool UninstallService(const std::wstring& serviceName)
{
    ScopedScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.valid()) return false;

    ScopedScHandle svc(OpenServiceW(scm.get(), serviceName.c_str(), DELETE));
    if (!svc.valid()) return false;

    return DeleteService(svc.get()) != 0;
}

bool StartServiceByName(const std::wstring& serviceName)
{
    ScopedScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.valid()) return false;

    ScopedScHandle svc(OpenServiceW(scm.get(), serviceName.c_str(), SERVICE_START));
    if (!svc.valid()) return false;

    return StartServiceW(svc.get(), 0, nullptr) != 0;
}

bool StopServiceByName(const std::wstring& serviceName, DWORD timeoutMs)
{
    ScopedScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.valid()) return false;

    ScopedScHandle svc(OpenServiceW(scm.get(), serviceName.c_str(),
                                     SERVICE_STOP | SERVICE_QUERY_STATUS));
    if (!svc.valid()) return false;

    SERVICE_STATUS status{};
    if (!ControlService(svc.get(), SERVICE_CONTROL_STOP, &status)) {
        // ERROR_SERVICE_NOT_ACTIVE just means it was already stopped —
        // that's success from the caller's point of view, not a failure.
        if (GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
            return false;
        }
    }

    // Poll for the actual state transition. Not elegant, but this is
    // literally what sc.exe itself does under the hood — there's no event
    // to wait on here, just SERVICE_STATUS snapshots.
    const DWORD pollIntervalMs = 250;
    DWORD waited = 0;

    while (waited < timeoutMs) {
        if (!QueryServiceStatus(svc.get(), &status)) {
            return false;
        }
        if (status.dwCurrentState == SERVICE_STOPPED) {
            return true;
        }
        Sleep(pollIntervalMs);
        waited += pollIntervalMs;
    }

    return false; // didn't stop within the timeout — caller's call what to do next
}

std::optional<ServiceStatusInfo> QueryServiceStatusByName(const std::wstring& serviceName)
{
    ScopedScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.valid()) return std::nullopt;

    ScopedScHandle svc(OpenServiceW(scm.get(), serviceName.c_str(), SERVICE_QUERY_STATUS));
    if (!svc.valid()) return std::nullopt;

    SERVICE_STATUS_PROCESS statusProcess{};
    DWORD bytesNeeded = 0;

    if (!QueryServiceStatusEx(svc.get(), SC_STATUS_PROCESS_INFO,
                               reinterpret_cast<LPBYTE>(&statusProcess),
                               sizeof(statusProcess), &bytesNeeded)) {
        return std::nullopt;
    }

    ServiceStatusInfo info;
    info.state = ToServiceState(statusProcess.dwCurrentState);
    info.processId = statusProcess.dwProcessId;
    info.win32ExitCode = statusProcess.dwWin32ExitCode;
    return info;
}

} // namespace wsk
