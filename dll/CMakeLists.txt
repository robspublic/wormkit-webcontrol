cmake_minimum_required(VERSION 3.17)
project(wkWebControl
        VERSION "0.1.0"
        DESCRIPTION "Web-based couch co-op controls for Worms Armageddon 3.8.1"
        LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Toolchain requirements
#
# This module MUST be built as a 32-bit Windows DLL with the MSVC toolchain.
# The hooking / entity code relies on MSVC inline asm (_asm), __fastcall, and
# naked functions that do NOT compile under GCC/MinGW. Configure with an x86
# MSVC generator, e.g.:
#
#   cmake -B build -A Win32
#   cmake --build build --config Release
#
# Debug builds are unsupported: some hooked game functions use custom calling
# conventions that only line up under optimized (Release/RelWithDebInfo) builds.
# ---------------------------------------------------------------------------

if(NOT MSVC)
    message(FATAL_ERROR "wkWebControl must be built with MSVC (32-bit). See comments in CMakeLists.txt.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "wkWebControl must be built for 32-bit (x86). Configure with: cmake -B build -A Win32")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

# ---------------------------------------------------------------------------
# Third-party dependencies (git submodule under lib/)
#   - PolyHook_2_0 : x86 detours / IAT / vtable hooking (bundles Zydis/AsmJit)
#
# Byte-signature scanning is vendored directly (src/PatternScanner.*) instead
# of pulling an external submodule, to keep the build reproducible on public CI.
#
# Populate with:
#   git submodule add https://github.com/stevemk14ebr/PolyHook_2_0.git dll/lib/PolyHook_2_0
#   git submodule update --init --recursive
# ---------------------------------------------------------------------------
# Build PolyHook as a *library* rather than its unit-test executable, with the
# static CRT so it links cleanly into our module.
#
# NOTE the option semantics: POLYHOOK_BUILD_DLL=ON means "build the library
# target" (SHARED if POLYHOOK_BUILD_SHARED_LIB, else STATIC); OFF builds the
# test executable, which cannot be linked into another target. So we want ON
# here, with SHARED_LIB=OFF to get a static PolyHook_2.
set(POLYHOOK_BUILD_DLL ON CACHE BOOL "" FORCE)
set(POLYHOOK_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(POLYHOOK_BUILD_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
add_subdirectory(lib/PolyHook_2_0)

# PolyHook headers are included as <polyhook2/...>; the PolyHook_2 target
# propagates its own dependency include dirs (zydis/asmjit) when linked.
include_directories(
        src
        lib/PolyHook_2_0)

# nlohmann/json (header-only) for the IPC JSON wire format. Fetched at
# configure time so we don't vendor the ~800KB single header into git.
include(FetchContent)
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW ON)
FetchContent_MakeAvailable(nlohmann_json)

# Generate version.h (PROJECT_NAME / version macros used by the source).
configure_file(cmake/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/gen/version.h)

add_definitions(-DLIBRARY_EXPORTS)

add_library(wkWebControl SHARED
        src/dllmain.cpp
        src/Config.cpp            src/Config.h
        src/Hooks.cpp             src/Hooks.h
        src/PatternScanner.cpp    src/PatternScanner.h
        src/Constants.h
        src/W2App.cpp             src/W2App.h
        src/Log.cpp               src/Log.h
        src/Protocol.cpp          src/Protocol.h
        src/entities/CTask.h
        src/entities/CGameTask.h
        src/entities/CTaskTeam.cpp     src/entities/CTaskTeam.h
        src/entities/CTaskTurnGame.cpp src/entities/CTaskTurnGame.h
        src/entities/CTaskWorm.cpp     src/entities/CTaskWorm.h
        # ---- new subsystems (web control bridge) ----
        src/ControlState.cpp      src/ControlState.h
        src/ControlHooks.cpp      src/ControlHooks.h
        src/IpcServer.cpp         src/IpcServer.h)

add_dependencies(wkWebControl PolyHook_2)

target_include_directories(wkWebControl PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)

# Static CRT (/MT) so the DLL doesn't depend on a runtime the game process lacks.
set_property(TARGET wkWebControl PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

# psapi: GetModuleInformation used by the vendored pattern scanner.
# ws2_32: Winsock TCP for the IPC server (loopback; reachable from the native
#         Linux backend because Wine maps Winsock onto the host network stack).
# nlohmann_json: header-only IPC JSON.
target_link_libraries(wkWebControl PolyHook_2 psapi ws2_32 nlohmann_json::nlohmann_json)
