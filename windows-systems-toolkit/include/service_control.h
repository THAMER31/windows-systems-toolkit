// service_control.h
//
// A wrapper around the Service Control Manager APIs — the stuff behind
// `sc.exe` and services.msc. This is the part of Windows systems programming
// that shows up in almost every piece of real infrastructure tooling
// (installers, agents, monitoring daemons) but that most sample code online
// skips entirely in favor of flashier stuff.
//
// Every function here requires the caller to already be running elevated
// for anything that mutates service state (install/start/stop/delete) —
// that's an OS requirement, not a limitation of this wrapper, so none of
// these functions attempt to self-elevate.
//
// Author: (your name)

#pragma once

#include <windows.h>
#include <string>
#include <optional>

namespace wsk {

enum class ServiceState {
    Stopped,
    StartPending,
    StopPending,
    Running,
    ContinuePending,
    PausePending,
    Paused,
    Unknown,
};

struct ServiceStatusInfo {
    ServiceState state = ServiceState::Unknown;
    DWORD processId = 0;
    DWORD win32ExitCode = 0;
};

// Registers a new service pointing at binaryPath. displayName is what shows
// up in services.msc; serviceName is the internal identifier used by every
// other function below. autoStart controls whether it comes up on boot vs
// requiring a manual/triggered start.
bool InstallService(const std::wstring& serviceName,
                     const std::wstring& displayName,
                     const std::wstring& binaryPath,
                     bool autoStart);

// Fully removes a service registration. Note this only marks it for
// deletion if the service is currently running — it disappears once
// stopped, same as `sc delete` behaves.
bool UninstallService(const std::wstring& serviceName);

bool StartServiceByName(const std::wstring& serviceName);

// Sends a stop control and waits (bounded by timeoutMs) for the service to
// actually transition to Stopped, since SERVICE_CONTROL_STOP is a request,
// not a guarantee — a service can take its time (or ignore it) during
// shutdown.
bool StopServiceByName(const std::wstring& serviceName, DWORD timeoutMs = 10000);

std::optional<ServiceStatusInfo> QueryServiceStatusByName(const std::wstring& serviceName);

} // namespace wsk
