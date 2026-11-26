cmake_minimum_required(VERSION 3.20)
project(csgo_legacy_patcher LANGUAGES CXX)

# The legacy CS:GO client is a 32-bit process, so the patcher DLL and launcher
# must be 32-bit too. Configure with:  cmake -S . -B build -A Win32
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "This project must be built for 32-bit (x86). "
                        "Re-run CMake with:  -A Win32")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "The guard thunk uses MSVC x86 __declspec(naked) inline "
                        "assembly; build with the MSVC toolchain (cl).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------------- patcher DLL
add_library(csgo-patcher SHARED
    client/src/scan/pattern.cpp
    client/src/sys/module.cpp
    client/src/sys/memory.cpp
    client/src/sys/log.cpp
    client/src/patch/registry.cpp
    client/src/patches/challenge_key_overflow.cpp
    client/src/dllmain.cpp
)
target_include_directories(csgo-patcher PRIVATE client/src)
target_compile_definitions(csgo-patcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(csgo-patcher PRIVATE /W4 /permissive-)
target_link_libraries(csgo-patcher PRIVATE psapi)
set_target_properties(csgo-patcher PROPERTIES OUTPUT_NAME "csgo-patcher" PREFIX "")

# -------------------------------------------------------------------- launcher
add_executable(csgo-patched-launcher WIN32 launcher/src/main.cpp)
target_compile_definitions(csgo-patched-launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(csgo-patched-launcher PRIVATE /W4 /permissive-)
target_link_libraries(csgo-patched-launcher PRIVATE shell32)
set_target_properties(csgo-patched-launcher PROPERTIES OUTPUT_NAME "csgo-patched-launcher")
