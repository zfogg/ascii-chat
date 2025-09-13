# PowerShell build script for ASCII-Chat on Windows
# Usage: 
#   .\build.ps1                    # Build with Clang in native Windows mode
#   .\build.ps1 -MinGW            # Build with GCC or Clang in MinGW mode  
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

# Always use Ninja if available for faster builds
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    $cmakeArgs += "-G", "Ninja"
}

# Add build type
$cmakeArgs += "-DCMAKE_BUILD_TYPE=$Config"

# Set compiler based on mode
if (-not $env:CC) {
    if ($MinGW) {
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
            $cmakeArgs[3] = "Visual Studio 17 2022"  # Change generator for MSVC
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

# Create symlinks in ./bin/ directory for consistency with Unix builds
Write-Host ""
Write-Host "Creating symlinks in bin/ directory..." -ForegroundColor Cyan

# Create bin directory if it doesn't exist
if (!(Test-Path "bin")) {
    New-Item -ItemType Directory -Path "bin" | Out-Null
}

# Remove old symlinks/files if they exist
if (Test-Path "bin\ascii-chat-server.exe") { Remove-Item "bin\ascii-chat-server.exe" -Force }
if (Test-Path "bin\ascii-chat-client.exe") { Remove-Item "bin\ascii-chat-client.exe" -Force }
# Also remove old names if they exist
if (Test-Path "bin\server.exe") { Remove-Item "bin\server.exe" -Force }
if (Test-Path "bin\client.exe") { Remove-Item "bin\client.exe" -Force }

# Create hard links (no admin needed, same volume required)
New-Item -ItemType HardLink -Path "bin\ascii-chat-server.exe" -Target "$PWD\build\bin\ascii-chat-server.exe" | Out-Null
New-Item -ItemType HardLink -Path "bin\ascii-chat-client.exe" -Target "$PWD\build\bin\ascii-chat-client.exe" | Out-Null
Write-Host "Created hard links in bin/" -ForegroundColor Green

# Link compile_commands.json to repo root for IDE/tool integration
if (Test-Path "$BuildDir\compile_commands.json") {
    Write-Host ""
    Write-Host "Linking compile_commands.json to repo root..." -ForegroundColor Cyan
    
    # Remove old link/file if it exists
    if (Test-Path "compile_commands.json") { 
        Remove-Item "compile_commands.json" -Force 
    }
    
    # Create hard link (no admin needed, works across drives)
    # Use symbolic link if on different volumes (requires admin on Windows)
    try {
        New-Item -ItemType HardLink -Path "compile_commands.json" -Target "$PWD\$BuildDir\compile_commands.json" -ErrorAction Stop | Out-Null
        Write-Host "Created hard link for compile_commands.json" -ForegroundColor Green
    }
    catch {
        # Fall back to copying if hard link fails (e.g., different volumes)
        Copy-Item "$BuildDir\compile_commands.json" "compile_commands.json" -Force
        Write-Host "Copied compile_commands.json (hard link failed, possibly different volumes)" -ForegroundColor Yellow
    }
}

# Copy DLLs to bin/ directory so binaries can find them
$dlls = Get-ChildItem "build\bin\*.dll" -ErrorAction SilentlyContinue
if ($dlls) {
    Copy-Item "build\bin\*.dll" "bin\" -Force
    Write-Host "Copied runtime DLLs to bin/" -ForegroundColor Green
}

Write-Host ""
Write-Host "Build complete!" -ForegroundColor Green
Write-Host ""
Write-Host "Run the server:  bin\ascii-chat-server.exe" -ForegroundColor White
Write-Host "Run the client:  bin\ascii-chat-client.exe" -ForegroundColor White