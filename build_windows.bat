@echo off
setlocal enabledelayedexpansion

echo Building ASCII-Chat with Clang and Visual Studio libraries...
echo.

REM Detect Visual Studio and Windows SDK
set VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community
set MSVC_PATH=%VS_PATH%\VC\Tools\MSVC\14.43.34808
set WINDOWS_SDK_DIR=C:\Program Files (x86)\Windows Kits\10
set SDK_VERSION=10.0.22621.0

if not exist "%MSVC_PATH%" (
    echo Error: Visual Studio not found at %MSVC_PATH%
    exit /b 1
)

if not exist "%WINDOWS_SDK_DIR%\Lib\%SDK_VERSION%" (
    set SDK_VERSION=10.0.22000.0
)
if not exist "%WINDOWS_SDK_DIR%\Lib\%SDK_VERSION%" (
    set SDK_VERSION=10.0.19041.0
)

echo Found Windows SDK version: %SDK_VERSION%
echo Found MSVC at: %MSVC_PATH%

REM Set paths
set "WINDOWS_SDK_INCLUDE=%WINDOWS_SDK_DIR%\Include\%SDK_VERSION%"
set "WINDOWS_SDK_LIB=%WINDOWS_SDK_DIR%\Lib\%SDK_VERSION%"
set "MSVC_INCLUDE=%MSVC_PATH%\include"
set "MSVC_LIB=%MSVC_PATH%\lib\x64"

REM Create build directory
if not exist build_clang mkdir build_clang
cd build_clang

REM Set environment variables for Clang to find libraries
set LIB=%MSVC_LIB%;%WINDOWS_SDK_LIB%\ucrt\x64;%WINDOWS_SDK_LIB%\um\x64
set INCLUDE=%MSVC_INCLUDE%;%WINDOWS_SDK_INCLUDE%\ucrt;%WINDOWS_SDK_INCLUDE%\um;%WINDOWS_SDK_INCLUDE%\shared

REM Print paths for debugging
echo LIB=%LIB%
echo INCLUDE=%INCLUDE%

REM Configure with CMake
echo Configuring with CMake...
cmake .. ^
    -G "Ninja" ^
    -DCMAKE_C_COMPILER=clang ^
    -DCMAKE_CXX_COMPILER=clang++ ^
    -DCMAKE_BUILD_TYPE=Debug

if errorlevel 1 (
    echo CMake configuration failed!
    cd ..
    exit /b 1
)

echo.
echo Building...
cmake --build . --config Debug

if errorlevel 1 (
    echo Build failed!
    cd ..
    exit /b 1
)

echo.
echo Build complete!
echo.
echo Executables:
echo   Server: build_clang\bin\server.exe
echo   Client: build_clang\bin\client.exe
echo.
echo To run:
echo   Server: build_clang\bin\server.exe
echo   Client: build_clang\bin\client.exe --address 127.0.0.1 --port 8080

cd ..