cmake_minimum_required(VERSION 3.20)
project(project_orbis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -Wno-cast-function-type -mfsgsbase -fno-exceptions)
endif()

# Core Native Runner
add_executable(orbis_runner
    src/main.cpp
    src/core/elf.cpp
    src/core/resolver.cpp
    src/hle/hle_libc.cpp
    src/hle/hle_kernel.cpp
    src/hle/hle_video.cpp
    src/hle/hle_pad.cpp
    src/hle/hle_audio.cpp
)

target_include_directories(orbis_runner PRIVATE src)

target_link_libraries(orbis_runner PRIVATE
    dbghelp
    xinput
    gdi32
    user32
)

# Auxiliary Tools
add_executable(patch_subsystem_jumps tools/patch_subsystem_jumps.cpp)
add_executable(dump_gamemain_stack tools/dump_gamemain_stack.cpp)
add_executable(read_live_globals tools/read_live_globals.cpp)
add_executable(test_printwindow tools/test_printwindow.cpp)
target_link_libraries(test_printwindow PRIVATE gdi32 user32)
