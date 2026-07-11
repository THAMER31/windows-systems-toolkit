// main.cpp
//
// Wiring the four modules together into something you can actually run.
// This file itself isn't trying to demonstrate anything clever — it's just
// proof the pieces above compose the way their headers claim they do.

#include "process_manager.h"
#include "ipc_pipe.h"
#include "thread_pool.h"
#include "service_control.h"

#include <iostream>
#include <iomanip>

namespace {

void PrintSectionHeader(const std::string& title)
{
    std::cout << "\n== " << title << " " << std::string(50 - title.size(), '=') << "\n";
}

void DemoProcessManager()
{
    PrintSectionHeader("Process enumeration");

    if (wsk::EnableDebugPrivilege()) {
        std::cout << "SeDebugPrivilege enabled.\n";
    } else {
        std::cout << "SeDebugPrivilege not available (not elevated) - continuing anyway.\n";
    }

    auto processes = wsk::EnumerateProcesses();
    std::cout << "Found " << processes.size() << " running processes. Showing the first 10:\n";

    for (size_t i = 0; i < processes.size() && i < 10; ++i) {
        std::wcout << L"  PID " << processes[i].pid
                    << L"  PPID " << processes[i].parentPid
                    << L"  " << processes[i].exeName << L"\n";
    }

    if (auto details = wsk::QueryProcessDetails(GetCurrentProcessId())) {
        std::cout << "\nDetails for our own process:\n";
        std::wcout << L"  Image path: " << details->imagePath << L"\n";
        std::cout << "  Working set: " << details->workingSetBytes / 1024 << " KB\n";
        std::cout << "  Private bytes: " << details->privateBytes / 1024 << " KB\n";
    }
}

void DemoJobObject()
{
    PrintSectionHeader("Job Object containment");

    try {
        wsk::JobObject job;
        job.SetActiveProcessLimit(4);

        DWORD childPid = 0;
        if (job.LaunchContained(L"cmd.exe /c echo hello from a contained process && timeout 1", childPid)) {
            std::cout << "Launched contained process, PID " << childPid << "\n";
        } else {
            std::cout << "Failed to launch contained process.\n";
        }
    } catch (const std::exception& ex) {
        std::cout << "JobObject demo failed: " << ex.what() << "\n";
    }
}

void DemoThreadPool()
{
    PrintSectionHeader("Native thread pool");

    wsk::ThreadPool pool(2, 4);

    for (int i = 0; i < 5; ++i) {
        pool.Submit([i] {
            std::cout << "  work item " << i << " running on thread "
                      << GetCurrentThreadId() << "\n";
        });
    }

    pool.SubmitDelayed([] {
        std::cout << "  delayed work item fired after ~500ms\n";
    }, 500);

    pool.WaitForPendingWork();
    Sleep(700); // give the delayed timer a chance to fire before we exit
}

void DemoNamedPipeIpc()
{
    PrintSectionHeader("Named pipe IPC");

    const std::wstring pipeName = L"\\\\.\\pipe\\wsk_demo_pipe";

    wsk::PipeServer server(pipeName, [](const std::string& request) {
        std::cout << "  server received: " << request << "\n";
        return "ack: " + request;
    });

    server.Start();
    Sleep(100); // let the accept loop actually get into ConnectNamedPipe

    auto reply = wsk::SendPipeMessage(pipeName, "ping from client");
    if (reply) {
        std::cout << "  client received: " << *reply << "\n";
    } else {
        std::cout << "  client got no reply (server may not have been ready).\n";
    }

    server.Stop();
}

void DemoServiceControl()
{
    PrintSectionHeader("Service Control Manager query");

    // Querying a well-known built-in service rather than installing one,
    // so this demo doesn't require elevation just to run end to end.
    // Swap in InstallService/StartServiceByName if you're running as admin
    // and want to see the full mutation path.
    if (auto status = wsk::QueryServiceStatusByName(L"Spooler")) {
        std::cout << "Print Spooler service - PID: " << status->processId
                  << ", state: " << static_cast<int>(status->state) << "\n";
    } else {
        std::cout << "Could not query the Spooler service on this machine.\n";
    }
}

} // namespace

int wmain()
{
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "Windows Systems Toolkit - demo driver\n";
    std::cout << "Each section below can also be used standalone; see README.md.\n";

    DemoProcessManager();
    DemoJobObject();
    DemoThreadPool();
    DemoNamedPipeIpc();
    DemoServiceControl();

    std::cout << "\nDone.\n";
    return 0;
}
