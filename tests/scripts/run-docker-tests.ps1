# PowerShell script to run ASCII-Chat tests via Docker
# Usage:
#   ./tests/scripts/run-docker-tests.ps1                          # Run all tests
#   ./tests/scripts/run-docker-tests.ps1 unit                     # Run all unit tests
#   ./tests/scripts/run-docker-tests.ps1 integration              # Run all integration tests
#   ./tests/scripts/run-docker-tests.ps1 performance              # Run all performance tests
#   ./tests/scripts/run-docker-tests.ps1 unit options             # Run unit tests: options
#   ./tests/scripts/run-docker-tests.ps1 unit options terminal_detect # Run unit tests: options and terminal_detect
#   ./tests/scripts/run-docker-tests.ps1 test_unit_buffer_pool -f "creation" # Run specific test case

param(
    [Parameter(Mandatory=$false)][string]$Filter = "",
    [Parameter(Mandatory=$false)][switch]$Build,
    [Parameter(Mandatory=$false)][switch]$NoBuild,
    [Parameter(Mandatory=$false)][switch]$Interactive,
    [Parameter(Mandatory=$false)][switch]$VerboseOutput,
    [Parameter(Mandatory=$false)][string]$BuildType = "debug",
    [Parameter(ValueFromRemainingArguments=$true, Position=0)]
    [string[]]$TestTargets
)

$ErrorActionPreference = "Stop"

Write-Host "ASCII-Chat Docker Test Runner" -ForegroundColor Green
Write-Host ""

# Get the repository root (script is in tests/scripts, so go up twice)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$TestsDir = Split-Path -Parent $ScriptDir
$RepoRoot = Split-Path -Parent $TestsDir

# Docker image name
$ImageName = "ascii-chat-tests"
$ContainerName = "ascii-chat-test-runner"

# Check if Docker is running
Write-Host "Checking Docker..." -ForegroundColor Cyan
try {
    docker version | Out-Null
} catch {
    Write-Host "ERROR: Docker is not running or not installed!" -ForegroundColor Red
    Write-Host "Please start Docker Desktop and try again." -ForegroundColor Yellow
    exit 1
}

# Build Docker image if needed
if (-not $NoBuild) {
    $ImageExists = docker images -q $ImageName 2>$null

    if (-not $ImageExists -or $Build) {
        Write-Host "Building Docker image..." -ForegroundColor Yellow
        Write-Host "(This may take a few minutes on first run)" -ForegroundColor Gray

        # Build from the tests directory context, but include parent directory
        docker build -t $ImageName -f "$TestsDir\Dockerfile" "$RepoRoot"

        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: Failed to build Docker image!" -ForegroundColor Red
            exit 1
        }
        Write-Host "Docker image built successfully!" -ForegroundColor Green
        Write-Host ""
    }
}

# Remove any existing container with the same name (silently)
$ErrorActionPreference = "SilentlyContinue"
docker rm -f $ContainerName 2>$null | Out-Null
$ErrorActionPreference = "Stop"

# Prepare the command to run
if ($Interactive) {
    $DockerFlags = "-it"
    Write-Host "Starting interactive shell in test container..." -ForegroundColor Cyan
} else {
    # Build test command using run_tests.sh
    $TestCommand = "./tests/scripts/run_tests.sh"

    # Add options first
    if ($BuildType -ne "debug") {
        $TestCommand += " -b $BuildType"
    }

    if ($VerboseOutput) {
        $TestCommand += " -v"
    }

    if ($Filter) {
        $TestCommand += " -f `"$Filter`""
    }

    # Add test targets (positional arguments) after options
    if ($TestTargets.Count -gt 0) {
        $TestCommand += " " + ($TestTargets -join " ")
    }

    $FullCommand = "rm -rf build_docker && CC=clang CXX=clang++ cmake -B build_docker -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_STANDARD=23 -DCMAKE_C_FLAGS='-std=c2x' -DBUILD_TESTS=ON -DCURSES_LIBRARY=/usr/lib/x86_64-linux-gnu/libncurses.so -DCURSES_INCLUDE_PATH=/usr/include && cmake --build build_docker && $TestCommand"
    $DockerFlags = "-t"

    Write-Host "Running tests in Docker container..." -ForegroundColor Cyan
    if ($TestTargets.Count -gt 0) {
        Write-Host "Test targets: $($TestTargets -join ', ')" -ForegroundColor Gray
    } else {
        Write-Host "Running all tests" -ForegroundColor Gray
    }
    if ($Filter) {
        Write-Host "Filter: $Filter" -ForegroundColor Gray
    }
    Write-Host "Command: $TestCommand" -ForegroundColor Gray
    Write-Host "Docker volume mount: ${RepoRoot}:/app" -ForegroundColor Gray
    Write-Host ""
}

# Run the container
# Mount the source code as a volume so we can test local changes without rebuilding
if ($Interactive) {
    docker run `
        $DockerFlags `
        --rm `
        --name $ContainerName `
        -v "${RepoRoot}:/app" `
        -w /app `
        $ImageName `
        /bin/bash
} else {
    docker run `
        $DockerFlags `
        --rm `
        --name $ContainerName `
        -v "${RepoRoot}:/app" `
        -w /app `
        $ImageName `
        bash -c "$FullCommand"
}

$ExitCode = $LASTEXITCODE

# Print results
Write-Host ""
if ($ExitCode -eq 0) {
    Write-Host "Tests completed successfully!" -ForegroundColor Green
} else {
    Write-Host "Tests failed with exit code: $ExitCode" -ForegroundColor Red
}

exit $ExitCode