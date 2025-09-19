#!/usr/bin/env pwsh
# PowerShell build script for ASCII-Chat on Windows
# Usage: 
#   .\build.ps1                    # Build with Clang in native Windows mode
#   .\build.ps1 -MinGW            # Build with GCC or Clang in MinGW mode  
#   .\build.ps1 -VSWithClang      # Build with Clang using Visual Studio 17 2022 generator
#   .\build.ps1 -Config Release    # Build in Release mode
#   .\build.ps1 -BuildDir mybuild  # Use custom build directory
#   .\build.ps1 -Clean             # Clean and rebuild
#   .\build.ps1 -CFlags "-DDEBUG_THREADS","-DDEBUG_MEMORY"  # Add compiler flags

param(
    [switch]$Clean,
    [string]$Config = "Debug",
    [string]$BuildDir = "build",
    [switch]$MinGW,
    [switch]$Test,
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
} else {
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

# Configure with CMake 
Write-Host "Configuring project ($Config build) in $BuildDir..." -ForegroundColor Cyan

# Build CMake arguments
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
    elseif ($MinGW) {
        # MinGW mode - prefer GCC
        if (Get-Command gcc -ErrorAction SilentlyContinue) {
            $env:CC = "gcc"
            $env:CXX = "g++"
            Write-Host "Using GCC compiler (MinGW mode)" -ForegroundColor Yellow
        }
        elseif (Get-Command clang -ErrorAction SilentlyContinue) {
            $env:CC = "clang"  
            $env:CXX = "clang++"
            $cmakeArgs += "-DUSE_MINGW=ON"
            Write-Host "Using Clang compiler (MinGW mode)" -ForegroundColor Yellow
        }
    }
    else {
        # Native Windows mode - prefer Clang  
        if (Get-Command clang -ErrorAction SilentlyContinue) {
            $env:CC = "clang"
            $env:CXX = "clang++"
            Write-Host "Using Clang compiler (native Windows mode)" -ForegroundColor Yellow
        }
        elseif (Get-Command cl -ErrorAction SilentlyContinue) {
            # Use MSVC if available
            # Remove Ninja generator and use Visual Studio generator
            $cmakeArgs = $cmakeArgs | Where-Object { $_ -ne "Ninja" -and $_ -ne "-G" }
            $cmakeArgs += "-G", "Visual Studio 17 2022"
            Write-Host "Using MSVC compiler" -ForegroundColor Yellow
        }
        elseif (Get-Command gcc -ErrorAction SilentlyContinue) {
            # Fall back to GCC
            $env:CC = "gcc"
            $env:CXX = "g++"
            Write-Host "Using GCC compiler (fallback)" -ForegroundColor Yellow
        }
    }
}
else {
    Write-Host "Using compiler from environment: CC=$env:CC" -ForegroundColor Yellow
}

# Add MinGW flag if requested
if ($MinGW) {
    $cmakeArgs += "-DUSE_MINGW=ON"
    # Disable vcpkg for MinGW builds as vcpkg libraries are for MSVC
    $env:VCPKG_ROOT = ""
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
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "ERROR: CMake configuration failed!" -ForegroundColor Red
    exit 1
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

# Run tests if requested
if ($Test) {
    Write-Host ""
    Write-Host "Running tests..." -ForegroundColor Cyan
    & ctest --test-dir build -C $Config --output-on-failure
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
Write-Host "Copied build outputs to bin/" -ForegroundColor Green

# compile_commands.json is now automatically handled by CMake (see CMakeLists.txt)

Write-Host ""
Write-Host "Build complete!" -ForegroundColor Green
Write-Host ""
Write-Host "Run the server:  bin\ascii-chat-server.exe" -ForegroundColor White
Write-Host "Run the client:  bin\ascii-chat-client.exe" -ForegroundColor White
