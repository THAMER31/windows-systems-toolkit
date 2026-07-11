# CMakeLists.txt
#
# Nothing fancy here on purpose - a static executable target plus the
# handful of import libs the modules above need. If you're vendoring this
# into a bigger project, turn the four modules into a proper `wsk` static
# lib target instead of building main.cpp alongside them.

cmake_minimum_required(VERSION 3.20)
project(windows_systems_toolkit LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "This project targets the Win32 API and only builds on Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(wsk_demo
    src/main.cpp
    src/process_manager.cpp
    src/ipc_pipe.cpp
    src/thread_pool.cpp
    src/service_control.cpp
)

target_include_directories(wsk_demo PRIVATE include)

# Unicode entry point (wmain) + wide Win32 calls throughout.
target_compile_definitions(wsk_demo PRIVATE UNICODE _UNICODE)

target_link_libraries(wsk_demo PRIVATE
    psapi   # GetProcessMemoryInfo, QueryFullProcessImageNameW's neighbors
    advapi32 # privilege APIs + Service Control Manager
)
