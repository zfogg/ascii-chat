# Installing Windows SDK for ascii-chat Development

## Check if Windows SDK is Already Installed

```powershell
# Check for Windows SDK
dir "C:\Program Files (x86)\Windows Kits\10\Lib"
```

## Installation Methods

### Option 1: Visual Studio Installer (Recommended)
1. Download Visual Studio Installer from https://visualstudio.microsoft.com/
2. Run the installer
3. Select "Desktop development with C++"
4. Under "Individual components", ensure these are checked:
   - Windows 10 SDK (latest version)
   - MSVC v143 - VS 2022 C++ x64/x86 build tools

### Option 2: Standalone Windows SDK
1. Download from: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/
2. Run the installer
3. Select all components for C++ development

### Option 3: Using winget (Windows Package Manager)
```powershell
# Install Windows SDK
winget install Microsoft.WindowsSDK

# Install Build Tools for Visual Studio
winget install Microsoft.VisualStudio.2022.BuildTools
```

### Option 4: Using Scoop
```powershell
# Add extras bucket if not already added
scoop bucket add extras

# Install Visual Studio Build Tools
scoop install vcredist2022
```

## Configure Environment Variables

After installation, you may need to set environment variables:

```powershell
# Add to Path (adjust version as needed)
$env:Path += ";C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64"

# Set Windows SDK version
$env:WindowsSDKVersion = "10.0.22621.0\"

# For permanent changes, use System Properties > Environment Variables
```

## Using with Clang

To use Windows SDK with Clang:

```bash
# Find Windows SDK
clang -print-search-dirs

# Compile with Windows SDK
clang -target x86_64-pc-windows-msvc \
      -I"C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/ucrt" \
      -I"C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/shared" \
      -I"C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/um" \
      -L"C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22621.0/ucrt/x64" \
      -L"C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22621.0/um/x64" \
      your_file.c
```

## Using with CMake

Configure CMake to find Windows SDK:

```cmake
# In CMakeLists.txt or toolchain file
set(CMAKE_SYSTEM_VERSION 10.0.22621.0)
set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION 10.0.22621.0)
```

Or when configuring:
```bash
cmake -B build -DCMAKE_SYSTEM_VERSION=10.0.22621.0
```

## Verify Installation

```powershell
# Check if cl.exe is available (MSVC compiler)
where cl

# Check Clang can find Windows headers
clang -E -x c - -v < nul 2>&1 | findstr /C:"Windows Kits"
```

## For ascii-chat

Since ascii-chat uses POSIX APIs, you have two options:

1. **Use MSYS2/MinGW** (Easier) - Provides POSIX compatibility layer
2. **Port to Windows APIs** (More work) - Replace POSIX calls with Windows equivalents

The project includes `windows_compat.h` to help with the porting effort.
