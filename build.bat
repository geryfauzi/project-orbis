@echo off
setlocal

set CXX=%~dp0tools\w64devkit\bin\g++.exe

if not exist "%CXX%" (
    echo Error: Toolchain not found at %CXX%
    exit /b 1
)

taskkill /F /IM bb_runner.exe >nul 2>&1

echo Compiling Beach Buggy Native Runner (bb_runner.exe)...
"%CXX%" -std=c++20 -O2 -Wall -Wextra -Wno-cast-function-type -mfsgsbase -fno-exceptions ^
    -I"%~dp0src" ^
    "%~dp0src\main.cpp" ^
    "%~dp0src\core\elf.cpp" ^
    "%~dp0src\core\resolver.cpp" ^
    "%~dp0src\hle\hle_libc.cpp" ^
    "%~dp0src\hle\hle_kernel.cpp" ^
    "%~dp0src\hle\hle_video.cpp" ^
    "%~dp0src\hle\hle_pad.cpp" ^
    "%~dp0src\hle\hle_audio.cpp" ^
    -ldbghelp ^
    -lxinput ^
    -lgdi32 ^
    -luser32 ^
    -Wl,--stack=33554432 ^
    -o "%~dp0bb_runner.exe"

if %ERRORLEVEL% equ 0 (
    echo Build succeeded: bb_runner.exe
) else (
    echo Build failed with error %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
