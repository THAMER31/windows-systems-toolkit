# Windows Systems Toolkit

A small collection of Win32 systems-programming building blocks, written to
be read as much as run. No third-party dependencies — just the Windows SDK
and the standard library.

I put this together to have something concrete to point people at when
"advanced Windows programming" comes up in an interview or a portfolio
review, rather than trying to explain it verbally. Each module is scoped
around a problem I've actually run into:

| Module | What it's for | The interesting bit |
|---|---|---|
| [`process_manager`](include/process_manager.h) | Enumerate, inspect, spawn, and contain processes | Job Objects launched suspended-then-attached to avoid the containment race |
| [`ipc_pipe`](include/ipc_pipe.h) | Local service-to-UI communication | Message-mode named pipes, clean shutdown without overlapped I/O |
| [`thread_pool`](include/thread_pool.h) | Background work without hand-rolling a thread manager | Wraps the native `CreateThreadpool` API instead of `std::thread` + a queue |
| [`service_control`](include/service_control.h) | Install/query/start/stop Windows services | SCM handle lifetime, polling `SERVICE_CONTROL_STOP` correctly |

## Why these four

Most "advanced Windows API" sample code out there either goes straight for
injection tricks that are more about looking edgy than being useful, or
stays so shallow it's just `CreateProcess` with no error handling. I wanted
this repo to sit in the gap: real primitives that show up in actual
production tooling — installers, monitoring agents, privileged helper
services — built with the boring discipline that code actually has to have
before it ships (every handle closed on every path, every return value
checked, RAII wrappers instead of manual cleanup scattered through the
code).

## Building

Requires the Windows SDK and a C++17 compiler.

```powershell
cmake -B build -S .
cmake --build build --config Release
.\build\Release\wsk_demo.exe
```

Or without CMake, using the MSVC command-line tools directly:

```powershell
cl /EHsc /std:c++17 /I include ^
   src\main.cpp src\process_manager.cpp src\ipc_pipe.cpp ^
   src\thread_pool.cpp src\service_control.cpp ^
   /link psapi.lib advapi32.lib /out:wsk_demo.exe
```

Some parts of the demo (the Job Object section, in particular) behave more
interestingly when run elevated, but nothing in the build or the base demo
requires it.

## A note on scope

There's no process injection, no code hollowing, no AMSI bypass, none of
that here — deliberately. Those techniques are legitimate in the right
context (EDR research, game modding, accessibility tooling) but they're also
the first thing that gets flagged when a stranger's Windows code shows up on
a scanner, and they don't actually demonstrate more skill than getting Job
Object lifetimes and SCM handle cleanup right does. This repo is meant to be
something you can point a recruiter or a teammate at without a conversation
about intent.

## License

MIT — see [LICENSE](LICENSE).
