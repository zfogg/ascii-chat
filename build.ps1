#!/usr/bin/env pwsh
# PowerShell build script for ASCII-Chat on Windows
# Usage: 
#   .\build.ps1                    # Build using default/debug preset
#   .\build.ps1 -Config Release    # Build using release preset
#   .\build.ps1 -Config Dev        # Build using dev preset (debug without sanitizers)
#   .\build.ps1 -Config Coverage   # Build using coverage preset
#   .\build.ps1 -Clean             # Clean and reconfigure from scratch
#   .\build.ps1 -MinGW             # Use custom config with GCC/Clang in MinGW mode
#   .\build.ps1 -VSWithClang       # Use custom config with Visual Studio + ClangCL
#   .\build.ps1 -BuildDir mybuild  # Use custom build directory (disables presets)
#   .\build.ps1 -CFlags "-DDEBUG_THREADS","-DDEBUG_MEMORY"  # Add compiler flags (disables presets)
#
# Note: CMake configuration only runs when build directory doesn't exist (or after -Clean).
#       Subsequent runs skip configuration and go straight to building.

param(
  [switch]$Clean,
  [string]$Config = "Debug",
  [string]$BuildDir = "build",
  [switch]$MinGW,
  [switch]$Verbose,
  [switch]$VSWithClang,
  [string[]]$CFlags = @()
)

Write-Host "ASCII-Chat Build Script" -ForegroundColor Green
Write-Host ""

# Kill any running server/client processes before building
Write-Host "Checking for running ASCII-Chat processes..." -ForegroundColor Cyan
$processes = @("ascii-chat-server", "ascii-chat-client", "server", "client")
$killed = $false

foreach ($proc in $processes) {
  $running = Get-Process -Name $proc -ErrorAction SilentlyContinue
  if ($running) {
    Write-Host "Killing running $proc processes..." -ForegroundColor Yellow
    Stop-Process -Name $proc -Force -ErrorAction SilentlyContinue
    Wait-Process -Name $proc -ErrorAction SilentlyContinue
    $killed = $true
  }
}

if ($killed) {
  Write-Host "Processes terminated." -ForegroundColor Green
  Write-Host ""
}
else {
  Write-Host "No running ASCII-Chat processes found." -ForegroundColor Green
  Write-Host ""
}

# Clean build directory if requested
if ($Clean) {
  Write-Host "Cleaning build directory: $BuildDir" -ForegroundColor Yellow
  if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
  }
  Write-Host ""
}

# Only configure if build directory doesn't exist
$needsConfigure = -not (Test-Path $BuildDir)

if ($needsConfigure) {
  Write-Host "Configuring project ($Config build) in $BuildDir..." -ForegroundColor Cyan

  # Map Config parameter to preset name
  $presetName = $Config.ToLower()
    
  # Check if using special modes that require custom configuration
  $useCustomConfig = $MinGW -or $VSWithClang -or ($CFlags.Count -gt 0) -or ($BuildDir -ne "build")
    
  if ($useCustomConfig) {
    Write-Host "Using custom configuration (preset not applicable with current flags)" -ForegroundColor Yellow
        
    # Build CMake arguments for custom configuration
    $cmakeArgs = @("-B", $BuildDir)

    # Always use Ninja if available for faster builds (unless using Visual Studio generator)
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
      $cmakeArgs += "-G", "Ninja"
    }

    # Add build type
    $cmakeArgs += "-DCMAKE_BUILD_TYPE=$Config"

    # Set compiler based on mode
    if (-not $env:CC) {
      if ($VSWithClang) {
        # Visual Studio generator with Clang toolset
        if (Get-Command clang -ErrorAction SilentlyContinue) {
          $env:CC = "clang"
          $env:CXX = "clang++"
          # Remove Ninja generator and use Visual Studio generator with Clang toolset
          $cmakeArgs = $cmakeArgs | Where-Object { $_ -ne "Ninja" -and $_ -ne "-G" }
          $cmakeArgs += "-G", "Visual Studio 17 2022"
          # Use Clang toolset for Visual Studio generator
          $cmakeArgs += "-T", "ClangCL"
          # Explicitly set Clang compiler for Visual Studio generator
          $cmakeArgs += "-DCMAKE_C_COMPILER=clang"
          $cmakeArgs += "-DCMAKE_CXX_COMPILER=clang++"
          # Force C23 standard
          $cmakeArgs += "-DCMAKE_C_STANDARD=17"
          $cmakeArgs += "-DCMAKE_C_STANDARD_REQUIRED=ON"
          # Prevent CMakeLists.txt from overriding the generator to Ninja
          $cmakeArgs += "-DCMAKE_GENERATOR=Visual Studio 17 2022"
          Write-Host "Using Clang compiler with Visual Studio 17 2022 generator (ClangCL toolset, C23 standard)" -ForegroundColor Yellow
        }
        else {
          Write-Host "ERROR: Clang not found! Please install Clang via Scoop: scoop install llvm" -ForegroundColor Red
          exit 1
        }
      }
      else {
        # Native Windows mode - prefer Clang  
        if (Get-Command clang -ErrorAction SilentlyContinue) {
          $env:CC = "clang"
          $env:CXX = "clang++"
          Write-Host "Using Clang compiler" -ForegroundColor Yellow
        }
        elseif (Get-Command gcc -ErrorAction SilentlyContinue) {
          # Fall back to GCC
          $env:CC = "gcc"
          $env:CXX = "g++"
          Write-Host "Using GCC compiler (Clang's fallback)" -ForegroundColor Yellow
        }
      }
    }
    else {
      Write-Host "Using compiler from environment: CC=$env:CC" -ForegroundColor Yellow
    }

    # Add compiler flags if provided
    if ($CFlags.Count -gt 0) {
      $flagString = $CFlags -join " "
      $cmakeArgs += "-DCMAKE_C_FLAGS=$flagString"
      Write-Host "Using C flags: $flagString" -ForegroundColor Yellow
    }

    if ($Verbose) {
      Write-Host "CMake arguments: $($cmakeArgs -join ' ')" -ForegroundColor DarkGray
    }

    & cmake $cmakeArgs
  }
  else {
    # Use preset-based configuration
    Write-Host "Using preset: $presetName" -ForegroundColor Yellow
        
    if ($Verbose) {
      Write-Host "CMake command: cmake --preset $presetName" -ForegroundColor DarkGray
    }
        
    & cmake --preset $presetName
  }
    
  if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "ERROR: CMake configuration failed!" -ForegroundColor Red
    exit 1
  }
}
else {
  Write-Host "Build directory exists, skipping configuration (use -Clean to reconfigure)" -ForegroundColor Cyan
}

# Build the project
Write-Host ""
Write-Host "Building project..." -ForegroundColor Cyan
& cmake --build build --config $Config --parallel
if ($LASTEXITCODE -ne 0) {
  Write-Host ""
  Write-Host "ERROR: Build failed!" -ForegroundColor Red
  exit 1
}

# Copy all build outputs to ./bin/ directory
Write-Host ""
Write-Host "Copying build outputs to bin/ directory..." -ForegroundColor Cyan
# Create bin directory if it doesn't exist
if (!(Test-Path "bin")) {
  New-Item -ItemType Directory -Path "bin" | Out-Null
}
# Copy everything from build/bin to bin
Copy-Item "build\bin\*" "bin\" -Force -Recurse
Write-Host "Build complete!" -ForegroundColor Green

Write-Host ""
Write-Host "Run the server:  bin\ascii-chat-server.exe" -ForegroundColor White
Write-Host "Run the client:  bin\ascii-chat-client.exe" -ForegroundColor White
