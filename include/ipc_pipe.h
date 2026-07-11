// ipc_pipe.h
//
// A minimal, message-mode named pipe server/client pair. This is the same
// primitive Windows services use to talk to their tray-icon front ends —
// I've used variations of this in a couple of real internal tools where a
// privileged background service needed to accept commands from an
// unprivileged UI process without opening a network socket.
//
// Deliberately not async/overlapped I/O here. Overlapped named pipes are
// their own rabbit hole and would double the size of this file for a demo
// that's trying to prove I understand IPC fundamentals, not that I can
// wire up an IOCP. Happy to talk through the overlapped version too.
//
// Author: (your name)

#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <optional>

namespace wsk {

// Runs a named pipe server on its own background thread, one client
// connection at a time (this is a demo, not a production dispatcher — a
// real thread-per-client or thread-pool version would relax that).
class PipeServer {
public:
    // handler is invoked once per received message and returns the bytes to
    // write back. Called on the server's background thread, so keep it
    // either quick or thread-safe on your end.
    using MessageHandler = std::function<std::string(const std::string& request)>;

    explicit PipeServer(std::wstring pipeName, MessageHandler handler);
    ~PipeServer();

    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    // Spins up the background accept loop. Returns immediately.
    bool Start();

    // Signals the accept loop to stop and joins the thread. Safe to call
    // even if Start() was never called or already stopped.
    void Stop();

private:
    void ServeLoop();

    std::wstring pipeName_;
    MessageHandler handler_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

// One-shot client: connect, send a request, read the reply, disconnect.
// Returns std::nullopt if the pipe doesn't exist or the server isn't
// listening within the wait timeout — both routine outcomes if the server
// hasn't started yet, so this deliberately isn't an exception.
std::optional<std::string> SendPipeMessage(const std::wstring& pipeName,
                                            const std::string& request,
                                            DWORD connectTimeoutMs = 2000);

} // namespace wsk
