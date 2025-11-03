#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Install dependencies for ascii-chat using vcpkg

.DESCRIPTION
    This script installs all required vcpkg dependencies before CMake configuration.
    It handles the chicken-and-egg problem where CMake requires deps to be installed
    but you need to configure CMake to know what deps to install.

.PARAMETER Triplet
    The vcpkg triplet to use (default: x64-windows for Debug, x64-windows-static for Release)

.PARAMETER Config
    Build configuration: Debug, Release, or Dev (default: Debug)

.EXAMPLE
    ./deps.ps1
    ./deps.ps1 -Config Release
    ./deps.ps1 -Triplet x64-windows-static
#>

param(
    [string]$Triplet = "",
    [ValidateSet("Debug", "Release", "Dev")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

Write-Host "`n=== ascii-chat Dependency Installer ===`n" -ForegroundColor Cyan

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

# Determine triplet based on config
if (-not $Triplet) {
    if ($Config -eq "Release") {
        $Triplet = "x64-windows-static"
    } else {
        $Triplet = "x64-windows"
    }
}

Write-Host "Using triplet: $Triplet" -ForegroundColor Green
Write-Host "Build configuration: $Config" -ForegroundColor Green

# Define required packages
# These match what CMakeLists.txt looks for
$RequiredPackages = @(
    "zstd",        # Compression
    "libsodium",   # Cryptography
    "portaudio"    # Audio I/O
)

Write-Host "`nInstalling required packages..." -ForegroundColor Cyan

foreach ($Package in $RequiredPackages) {
    Write-Host "  Installing: $Package" -ForegroundColor Yellow

    # Check if already installed
    $CheckCmd = "& `"$VcpkgExe`" list $Package`:$Triplet"
    $Installed = Invoke-Expression $CheckCmd 2>$null

    if ($Installed -match "$Package`:$Triplet") {
        Write-Host "    Already installed: $Package`:$Triplet" -ForegroundColor DarkGray
        continue
    }

    # Install the package
    $InstallCmd = "& `"$VcpkgExe`" install $Package`:$Triplet"
    try {
        Invoke-Expression $InstallCmd
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg install failed with exit code $LASTEXITCODE"
        }
        Write-Host "    Installed: $Package`:$Triplet" -ForegroundColor Green
    } catch {
        Write-Host "    ERROR: Failed to install $Package`:$Triplet" -ForegroundColor Red
        Write-Host "    $_" -ForegroundColor Red
        exit 1
    }
}

Write-Host "`nAll dependencies installed successfully!" -ForegroundColor Green
Write-Host "`nYou can now run:" -ForegroundColor Cyan
Write-Host "  ./build.ps1 -Config $Config" -ForegroundColor White
