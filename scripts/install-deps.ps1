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
        $ProcessorArch = $env:PROCESSOR_ARCHITECTURE
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

Write-Host "`n=== ascii-chat Dependency Installer ===`n" -ForegroundColor Cyan

Write-Host "Getting submodules" -ForegroundColor Yellow
& git submodule init
& git submodule update --recursive

# Check if vcpkg is available
if (-not $env:VCPKG_ROOT) {
    Write-Host "ERROR: VCPKG_ROOT environment variable not set" -ForegroundColor Red
    Write-Host "Please install vcpkg and set VCPKG_ROOT to point to it" -ForegroundColor Yellow
    Write-Host "Example: `$env:VCPKG_ROOT = 'C:\path\to\vcpkg'" -ForegroundColor Yellow
    exit 1
}

$VcpkgRoot = $env:VCPKG_ROOT
$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"

if (-not (Test-Path $VcpkgExe)) {
    Write-Host "ERROR: vcpkg.exe not found at: $VcpkgExe" -ForegroundColor Red
    Write-Host "Please bootstrap vcpkg first by running:" -ForegroundColor Yellow
    Write-Host "  cd `"$VcpkgRoot`" && .\bootstrap-vcpkg.bat" -ForegroundColor Yellow
    exit 1
}

Write-Host "Found vcpkg at: $VcpkgRoot" -ForegroundColor Green

# Determine triplet based on config and architecture
if (-not $Triplet) {
    $BaseTriplet = "$Architecture-windows"
    if ($Config -eq "Release") {
        $Triplet = "$BaseTriplet-static"
    } else {
        $Triplet = $BaseTriplet
    }
}

Write-Host "Target architecture: $Architecture" -ForegroundColor Green
Write-Host "Using triplet: $Triplet" -ForegroundColor Green
Write-Host "Build configuration: $Config" -ForegroundColor Green

# Show what type of libraries will be installed
if ($Triplet -like "*-static") {
    Write-Host "Installing STATIC libraries (no DLLs required)" -ForegroundColor Cyan
} else {
    Write-Host "Installing DYNAMIC libraries (DLLs required)" -ForegroundColor Cyan
}

# Define required packages
# These match what CMakeLists.txt looks for
# Note: For Release builds with x64-windows-static triplet, these will be static libraries
$RequiredPackages = @(
    "mimalloc",    # Memory allocator (high-performance replacement for malloc)
    "zstd",        # Compression library for frame data
    "libsodium",   # Cryptography library for encryption
    "portaudio"    # Audio I/O library for capture/playback
)

Write-Host "`nInstalling required packages..." -ForegroundColor Cyan

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
    Write-Host "    Running: vcpkg install $PackageSpec" -ForegroundColor Gray
    try {
        & $VcpkgExe install $PackageSpec
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
