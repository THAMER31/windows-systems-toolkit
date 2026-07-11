// ipc_pipe.cpp
//
// Implementation notes live inline below — the short version is that named
// pipes on Windows are session/desktop-scoped by default, which is exactly
// what you want for local service<->UI communication and exactly what you
// don't want if you were hoping this doubles as cross-machine IPC (it
// doesn't, not without SMB and a very different security posture).

#include "ipc_pipe.h"

namespace wsk {

namespace {
constexpr DWORD kBufferSize = 4096;
}

PipeServer::PipeServer(std::wstring pipeName, MessageHandler handler)
    : pipeName_(std::move(pipeName)), handler_(std::move(handler))
{
}

PipeServer::~PipeServer()
{
    Stop();
}

bool PipeServer::Start()
{
    if (running_.exchange(true)) {
        return true; // already running, nothing to do
    }

    worker_ = std::thread(&PipeServer::ServeLoop, this);
    return true;
}

void PipeServer::Stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    // The accept loop is blocked in ConnectNamedPipe/ReadFile, which won't
    // notice running_ flipping to false on its own. Connecting a throwaway
    // client to our own pipe is the simplest way to unblock it without
    // reaching for overlapped I/O just for shutdown.
    HANDLE nudge = CreateFileW(pipeName_.c_str(), GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (nudge != INVALID_HANDLE_VALUE) {
        CloseHandle(nudge);
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

void PipeServer::ServeLoop()
{
    while (running_.load()) {
        HANDLE pipe = CreateNamedPipeW(
            pipeName_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            kBufferSize, kBufferSize,
            0,       // default timeout
            nullptr  // default security descriptor — fine for a same-user
                     // demo; a real service exposing this cross-session
                     // would want an explicit DACL here.
        );

        if (pipe == INVALID_HANDLE_VALUE) {
            break; // something's seriously wrong (e.g. name collision); bail
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (!running_.load()) {
            CloseHandle(pipe);
            break;
        }

        if (connected) {
            char buffer[kBufferSize];
            DWORD bytesRead = 0;

            if (ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
                std::string request(buffer, bytesRead);
                std::string response = handler_(request);

                DWORD bytesWritten = 0;
                WriteFile(pipe, response.data(),
                          static_cast<DWORD>(response.size()), &bytesWritten, nullptr);
            }

            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
        }

        CloseHandle(pipe);
    }
}

std::optional<std::string> SendPipeMessage(const std::wstring& pipeName,
                                            const std::string& request,
                                            DWORD connectTimeoutMs)
{
    // WaitNamedPipe blocks until an instance is available or the timeout
    // hits — this is what saves us from a retry-loop-with-sleep against
    // ERROR_PIPE_BUSY.
    if (!WaitNamedPipeW(pipeName.c_str(), connectTimeoutMs)) {
        return std::nullopt;
    }

    HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    DWORD bytesWritten = 0;
    if (!WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()),
                    &bytesWritten, nullptr)) {
        CloseHandle(pipe);
        return std::nullopt;
    }

    char buffer[kBufferSize];
    DWORD bytesRead = 0;
    std::optional<std::string> result;

    if (ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr)) {
        result = std::string(buffer, bytesRead);
    }

    CloseHandle(pipe);
    return result;
}

} // namespace wsk
