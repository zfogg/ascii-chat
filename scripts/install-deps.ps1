#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Install dependencies for ascii-chat using vcpkg

.DESCRIPTION
    This script installs all required vcpkg dependencies before CMake configuration.
    It handles the chicken-and-egg problem where CMake requires deps to be installed
    but you need to configure CMake to know what deps to install.

.PARAMETER Triplet
    The vcpkg triplet to use (e.g., x64-windows-static, arm64-windows, x86-windows)
    If not specified, automatically determined based on Config and Architecture

.PARAMETER Config
    Build configuration: Debug, Release, or Dev (default: Debug)

.PARAMETER Architecture
    Target architecture: x64, x86, arm64, or arm (default: auto-detect)

.PARAMETER Release
    Convenience switch to install Release dependencies (equivalent to -Config Release)

.EXAMPLE
    ./install-deps.ps1
    ./install-deps.ps1 -Release
    ./install-deps.ps1 -Config Release
    ./install-deps.ps1 -Architecture arm64
    ./install-deps.ps1 -Triplet x64-windows-static
#>

param(
    [string]$Triplet = "",
    [ValidateSet("Debug", "Release", "Dev")]
    [string]$Config = "Debug",
    [ValidateSet("x64", "x86", "arm64", "arm", "")]
    [string]$Architecture = "",
    [switch]$Release
)

# Handle -Release convenience switch
if ($Release) {
    $Config = "Release"
}

# Detect architecture if not specified
if (-not $Architecture) {
    $Architecture = if ([Environment]::Is64BitOperatingSystem) {
        # Check if running on ARM64
        $ProcessorArch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
        if ($ProcessorArch -eq "ARM64") {
            "arm64"
        } else {
            "x64"
        }
    } else {
        # 32-bit OS
        $ProcessorArch = $env:PROCESSOR_ARCHITECTURE
        if ($ProcessorArch -eq "ARM") {
            "arm"
        } else {
            "x86"
        }
    }
    Write-Host "Auto-detected architecture: $Architecture" -ForegroundColor Cyan
}

$ErrorActionPreference = "Stop"
$RunningOnWindows = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT

Write-Host "`n=== ascii-chat Dependency Installer ===`n" -ForegroundColor Cyan

$ModuleFile = Join-Path $PSScriptRoot '../deps/ascii-chat-deps/mmmbed/go.mod'
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot '../.git')) {
    Write-Host "Getting submodules" -ForegroundColor Yellow
    & git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "Submodule initialization failed." }
    & git submodule update --init --checkout -- deps/ascii-chat-deps/mmmbed
    if ($LASTEXITCODE -ne 0) { throw "mmmbed submodule initialization failed." }
}
if (-not (Test-Path -LiteralPath $ModuleFile)) { throw "mmmbed source is missing ($ModuleFile)." }

# SQL catalog generation builds mmmbed from its pinned Go sources.
if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "Go is required. Install Go or winget and rerun this script."
    }
    & winget install --id GoLang.Go --exact --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) { throw "Go installation failed." }
    $MachinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $UserPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$MachinePath;$UserPath;$env:Path"
    if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
        throw "Go was installed but is not on PATH. Open a new shell and rerun this script."
    }
}
$GoVersion = & go version
if ($GoVersion -match 'go(\d+)\.(\d+)' -and ([int]$Matches[1] -lt 1 -or ([int]$Matches[1] -eq 1 -and [int]$Matches[2] -lt 21))) {
    $GoToolDir = & go env GOBIN
    if (-not $GoToolDir) {
        $GoPaths = (& go env GOPATH) -split [IO.Path]::PathSeparator
        $GoToolDir = Join-Path $GoPaths[0] 'bin'
    }
    $GoToolVersion = ((Get-Content -LiteralPath $ModuleFile | Where-Object { $_ -match '^go ' }) -replace '^go\s+', '').Trim()
    New-Item -ItemType Directory -Force -Path $GoToolDir | Out-Null
    $PreviousGoBin = $env:GOBIN
    try {
        $env:GOBIN = $GoToolDir
        & go install "golang.org/dl/go$GoToolVersion@latest"
        if ($LASTEXITCODE -ne 0) { throw "Could not install the Go toolchain launcher." }
        & (Join-Path $GoToolDir "go$GoToolVersion.exe") download
        if ($LASTEXITCODE -ne 0) { throw "Could not download the Go toolchain." }
    } finally {
        $env:GOBIN = $PreviousGoBin
    }
}

if ($RunningOnWindows -and $Architecture -eq 'arm64' -and -not (Get-Command aarch64-w64-mingw32-clang -ErrorAction SilentlyContinue)) {
    $MsysRoot = $env:MSYS2_ROOT
    if (-not $MsysRoot) { $MsysRoot = Join-Path $env:LOCALAPPDATA 'Programs/ascii-chat-msys2' }
    $MsysBash = Join-Path $MsysRoot 'usr/bin/bash.exe'
    if (-not (Test-Path -LiteralPath $MsysBash)) {
        if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
            throw "MSYS2 is required for the ARM64 CGo compiler. Install MSYS2 or winget and rerun this script."
        }
        & winget install --id MSYS2.MSYS2 --exact --location $MsysRoot --accept-package-agreements --accept-source-agreements
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $MsysBash)) {
            throw "MSYS2 installation failed. Set MSYS2_ROOT to an existing installation and rerun this script."
        }
    }
    & $MsysBash -lc 'pacman -S --needed --noconfirm mingw-w64-clang-aarch64-toolchain'
    if ($LASTEXITCODE -ne 0) { throw "ARM64 MinGW toolchain installation failed." }
    $CompilerDir = Join-Path $MsysRoot 'clangarm64/bin'
    if (-not (Test-Path -LiteralPath (Join-Path $CompilerDir 'aarch64-w64-mingw32-clang.exe'))) {
        throw "ARM64 MinGW compiler was not found in $CompilerDir."
    }
    $UserPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (($UserPath -split ';') -notcontains $CompilerDir) {
        [Environment]::SetEnvironmentVariable('Path', "$UserPath;$CompilerDir", 'User')
    }
    $env:Path = "$CompilerDir;$env:Path"
}

if ($RunningOnWindows -and $Architecture -ne 'arm64' -and -not (Get-Command gcc -ErrorAction SilentlyContinue)) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "MinGW GCC is required for CGo. Install it or winget and rerun this script."
    }
    if ($Architecture -ne 'x64') {
        throw "Install a native MinGW GCC toolchain for $Architecture and rerun this script."
    }
    & winget install --id BrechtSanders.WinLibs.POSIX.UCRT --exact --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) { throw "MinGW GCC installation failed." }
    $MachinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $UserPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$MachinePath;$UserPath;$env:Path"
    if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
        throw "MinGW GCC was installed but is not on PATH. Open a new shell and rerun this script."
    }
}

# Check for Visual Studio Build Tools (provides nmake for BearSSL)
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $VsWhere) {
    $VsInstall = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if (-not $VsInstall) {
        Write-Host "WARNING: Visual Studio Build Tools not found" -ForegroundColor Yellow
        Write-Host "BearSSL requires nmake from Visual Studio Build Tools" -ForegroundColor Yellow
        Write-Host "Install from: https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022" -ForegroundColor Yellow
        Write-Host ""
    } else {
        Write-Host "Found Visual Studio at: $VsInstall" -ForegroundColor Green
    }
} else {
    Write-Host "WARNING: Could not verify Visual Studio installation" -ForegroundColor Yellow
    Write-Host "BearSSL requires nmake from Visual Studio Build Tools" -ForegroundColor Yellow
}

# Check if vcpkg is available
if (-not $env:VCPKG_ROOT) {
    Write-Host "ERROR: VCPKG_ROOT environment variable not set" -ForegroundColor Red
    Write-Host "Please install vcpkg and set VCPKG_ROOT to point to it" -ForegroundColor Yellow
    Write-Host "Example: `$env:VCPKG_ROOT = 'C:\path\to\vcpkg'" -ForegroundColor Yellow
    exit 1
}

$VcpkgRoot = $env:VCPKG_ROOT

# Detect platform and set vcpkg executable name
# Windows PowerShell 5.1 does not define the PowerShell Core platform variables.
if ($RunningOnWindows) {
    $VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
    $BootstrapCmd = ".\bootstrap-vcpkg.bat"
    $Platform = "windows"
} elseif ($IsMacOS) {
    $VcpkgExe = Join-Path $VcpkgRoot "vcpkg"
    $BootstrapCmd = "./bootstrap-vcpkg.sh"
    $Platform = "osx"
} elseif ($IsLinux) {
    $VcpkgExe = Join-Path $VcpkgRoot "vcpkg"
    $BootstrapCmd = "./bootstrap-vcpkg.sh"
    $Platform = "linux"
} else {
    Write-Host "ERROR: Unknown platform" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $VcpkgExe)) {
    Write-Host "ERROR: vcpkg not found at: $VcpkgExe" -ForegroundColor Red
    Write-Host "Please bootstrap vcpkg first by running:" -ForegroundColor Yellow
    Write-Host "  cd `"$VcpkgRoot`" && $BootstrapCmd" -ForegroundColor Yellow
    exit 1
}

Write-Host "Found vcpkg at: $VcpkgRoot" -ForegroundColor Green
Write-Host "Detected platform: $Platform" -ForegroundColor Green

# Determine triplet based on platform, config, and architecture
if (-not $Triplet) {
    $BaseTriplet = "$Architecture-$Platform"
    # Only Windows uses -static suffix for Release builds
    # Linux/macOS use regular triplets (vcpkg defaults to static linking on Unix)
    if ($RunningOnWindows -and $Config -eq "Release") {
        $Triplet = "$BaseTriplet-static"
    } else {
        $Triplet = $BaseTriplet
    }
}

Write-Host "Target architecture: $Architecture" -ForegroundColor Green
Write-Host "Using triplet: $Triplet" -ForegroundColor Green
Write-Host "Build configuration: $Config" -ForegroundColor Green

# Show what type of libraries will be installed
if ($RunningOnWindows) {
    if ($Triplet -like "*-static") {
        Write-Host "Installing STATIC libraries (no DLLs required)" -ForegroundColor Cyan
    } else {
        Write-Host "Installing DYNAMIC libraries (DLLs required)" -ForegroundColor Cyan
    }
} else {
    Write-Host "Installing packages from vcpkg" -ForegroundColor Cyan
}

# Define required packages
# These match what CMakeLists.txt looks for
# Note: For Release builds with x64-windows-static triplet, these will be static libraries
$RequiredPackages = @(
    "mimalloc",        # Memory allocator (high-performance replacement for malloc)
    "zstd",            # Compression library for frame data
    "libsodium",       # Cryptography library for encryption
    "portaudio",       # Audio I/O library for capture/playback
    "fftw3",          # Spectral audio processing
    "opus",            # Audio codec library for real-time compression
    "sqlite3",         # Database library for discovery service
    "openssl",         # SSL/TLS library (required by libdatachannel for TURN credentials)
    "libdatachannel",  # WebRTC DataChannels for P2P connections
    "miniupnpc",       # UPnP client library for automatic router port forwarding
    "abseil",          # C++ utility library (used by WebRTC AEC3)
    "icu",            # Unicode IDNA hostname normalization
    "pcre2",          # Regular expressions
    "libuv",          # Native event-driven TCP I/O
    "libwebsockets",   # WebSocket transport library for browser clients
    "ffmpeg",          # Media file/URL streaming library
    "yyjson",          # JSON library (write-only for this build)
    "freetype",        # Font rendering library
    "fontconfig"       # Font configuration library
)

Write-Host "`nInstalling required packages..." -ForegroundColor Cyan

# Classic-mode installs do not read this repository's vcpkg configuration.
$OverlayPorts = Join-Path (Split-Path $PSScriptRoot -Parent) "vcpkg-overlay"

foreach ($Package in $RequiredPackages) {
    $PackageSpec = "${Package}:${Triplet}"
    Write-Host "`n  Installing: $PackageSpec" -ForegroundColor Yellow

    # Check if already installed using direct command execution
    $Installed = & $VcpkgExe list $PackageSpec 2>$null

    if ($Installed -match [regex]::Escape($PackageSpec)) {
        Write-Host "    ✓ Already installed: $PackageSpec" -ForegroundColor DarkGray
        continue
    }

    # Install the package using direct command execution (not Invoke-Expression)
    Write-Host "    Running: vcpkg install $PackageSpec --overlay-ports=$OverlayPorts" -ForegroundColor Gray
    try {
        & $VcpkgExe install $PackageSpec "--overlay-ports=$OverlayPorts"
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg install failed with exit code $LASTEXITCODE"
        }
        Write-Host "    ✓ Installed: $PackageSpec" -ForegroundColor Green
    } catch {
        Write-Host "    ✗ ERROR: Failed to install $PackageSpec" -ForegroundColor Red
        Write-Host "    $_" -ForegroundColor Red
        exit 1
    }
}

Write-Host "`n=== Verifying Installation ===" -ForegroundColor Cyan
Write-Host ""

$AllInstalled = $true
foreach ($Package in $RequiredPackages) {
    $PackageSpec = "${Package}:${Triplet}"
    $Installed = & $VcpkgExe list $PackageSpec 2>$null

    if ($Installed -match [regex]::Escape($PackageSpec)) {
        Write-Host "  ✓ $PackageSpec" -ForegroundColor Green
    } else {
        Write-Host "  ✗ $PackageSpec (NOT INSTALLED)" -ForegroundColor Red
        $AllInstalled = $false
    }
}

Write-Host ""

# Install uv-managed Python and yt-dlp for YouTube support.
Write-Host "`nInstalling uv-managed Python and yt-dlp..." -ForegroundColor Cyan
if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    Write-Host "  Installing uv" -ForegroundColor Gray
    irm https://astral.sh/uv/install.ps1 | iex
    $env:Path = "$env:USERPROFILE\.local\bin;$env:Path"
}
if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    throw "uv installation failed"
}
uv python install 3.15
uv python pin 3.15
uv tool install --upgrade "yt-dlp[default]"
uv sync --project "$PSScriptRoot\..\tests\ascii_chat_tests" --frozen

Write-Host ""
if ($AllInstalled) {
    Write-Host "All dependencies installed successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "You can now run:" -ForegroundColor Cyan
    Write-Host "  ./build.ps1 -Config $Config" -ForegroundColor White

    if ($Triplet -like "*-static") {
        Write-Host ""
        Write-Host "Note: Your $Architecture Release build will be fully static with no DLL dependencies." -ForegroundColor Yellow
    } else {
        Write-Host ""
        Write-Host "Note: Your $Architecture build will require DLLs from vcpkg." -ForegroundColor Yellow
    }
} else {
    Write-Host "Some dependencies failed to install!" -ForegroundColor Red
    exit 1
}

# Install the Vite+ toolchain used by the repository formatters.
if (-not (Get-Command vp -ErrorAction SilentlyContinue)) {
    Invoke-RestMethod https://vite.plus/ps1 | Invoke-Expression
    $env:Path = "$env:USERPROFILE\.vite-plus\bin;$env:Path"
}
if (Get-Command vp -ErrorAction SilentlyContinue) {
    $RepoRoot = Split-Path $PSScriptRoot -Parent
    Push-Location $RepoRoot
    try {
        & vp install --ignore-scripts
        if ($LASTEXITCODE -ne 0) { throw 'Root Vite+ install failed' }
        Push-Location web
        try {
            & vp install --ignore-scripts
            if ($LASTEXITCODE -ne 0) { throw 'Web Vite+ install failed' }
        } finally { Pop-Location }
    } finally { Pop-Location }
} else {
    Write-Warning 'Open a new shell and run vp install in the repository and web directories.'
}
