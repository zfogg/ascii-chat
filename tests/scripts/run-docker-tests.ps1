#!/usr/bin/env pwsh
# PowerShell script to run ASCII-Chat tests via Docker
# Usage:
#   ./tests/scripts/run-docker-tests.ps1                          # Run all tests
#   ./tests/scripts/run-docker-tests.ps1 unit                     # Run all unit tests
#   ./tests/scripts/run-docker-tests.ps1 integration              # Run all integration tests
#   ./tests/scripts/run-docker-tests.ps1 performance              # Run all performance tests
#   ./tests/scripts/run-docker-tests.ps1 -ClangTidy               # Run clang-tidy analysis on all files
#   ./tests/scripts/run-docker-tests.ps1 clang-tidy               # Run clang-tidy analysis on all files
#   ./tests/scripts/run-docker-tests.ps1 clang-tidy lib/common.c  # Run clang-tidy on specific file
#   ./tests/scripts/run-docker-tests.ps1 unit options             # Run unit tests: options
#   ./tests/scripts/run-docker-tests.ps1 unit options terminal_detect # Run unit tests: options and terminal_detect
#   ./tests/scripts/run-docker-tests.ps1 test_unit_buffer_pool -f "creation" # Run specific test case

[CmdletBinding()]
param(
    [string]$Suite = "",
    [string]$Test = "",
    [string]$Filter = "",
    [switch]$Build,
    [switch]$NoBuild,
    [switch]$Interactive,
    [switch]$VerboseOutput,
    [switch]$Clean,
    [string]$BuildType = "debug",
    [int]$ScanPort = 8080,
    [switch]$ClangTidy,
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$TestTargets
)

$ErrorActionPreference = "Stop"

Write-Host "ASCII-Chat Docker Test Runner" -ForegroundColor Green
Write-Host ""

# Get the repository root (script is in tests/scripts, so go up twice)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$TestsDir = Split-Path -Parent $ScriptDir
$RepoRoot = Split-Path -Parent $TestsDir

# Helper function to normalize paths for Docker (forward slashes)
function ConvertTo-DockerPath {
    param([string]$Path)
    return $Path -replace '\\', '/'
}

# Docker image name
$ImageName = "ascii-chat-tests"
$ContainerName = "ascii-chat-test-runner"

# Volume names for persistent caching
$CcacheVolume = "ascii-chat-ccache"
$DepsCacheVolume = "ascii-chat-deps-cache"

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
        $DockerfilePath = ConvertTo-DockerPath "$TestsDir/Dockerfile"
        $DockerContext = ConvertTo-DockerPath $RepoRoot
        docker build -t $ImageName -f "$DockerfilePath" "$DockerContext"

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

# Check if this is a clang-tidy request (switch, Suite, or TestTargets)
$IsClangTidy = $ClangTidy -or ($Suite -eq "clang-tidy") -or ($TestTargets -contains "clang-tidy")
if ($IsClangTidy) {
    Write-Host "Running clang-tidy static analysis..." -ForegroundColor Cyan
    Write-Host "This will analyze all source code using clang-tidy" -ForegroundColor Gray
    Write-Host ""

    # Create a temporary script file with clang-tidy implementation
    $TempScript = Join-Path $RepoRoot "temp-clang-tidy.sh"
    $ScriptContent = @'
#!/bin/bash
set -e

echo '=== Starting ASCII-Chat clang-tidy Analysis ==='
echo ""

# Check if build_tidy exists and is recent
if [ ! -d "build_tidy" ] || [ ! -f "build_tidy/compile_commands.json" ] || \
   [ "CMakeLists.txt" -nt "build_tidy/compile_commands.json" ] || \
   [ "$(find lib src -name '*.c' -o -name '*.h' | head -1)" -nt "build_tidy/compile_commands.json" ]; then

    echo 'Configuring build for clang-tidy...'
    # Configure with clang to generate compile_commands.json
    CC=clang CXX=clang++ cmake -B build_tidy -G Ninja \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DBUILD_TESTS=OFF

    echo 'Building project to ensure compilation database is complete...'
    cmake --build build_tidy --target ascii-chat-server ascii-chat-client
else
    echo 'Using cached build (build_tidy exists and is up to date)'
fi

echo 'Running clang-tidy analysis...'
echo ""

# Check if specific files were provided as arguments
if [ $# -gt 0 ]; then
    echo "Analyzing specific files: $@"
    # Analyze specific files provided as arguments
    for file in "$@"; do
        if [[ "$file" == *.c ]] && [[ -f "$file" ]]; then
            echo "=== Analyzing: $file ==="

            # Run clang-tidy on the specific file using .clang-tidy configuration
            if ! clang-tidy "$file" -p=build_tidy --format-style=none 2>&1; then
                echo "Failed to analyze $file"
            fi
            echo ""
        else
            echo "Skipping $file (not a .c file or doesn't exist)"
        fi
    done
else
    echo "No specific files provided, analyzing all C files"
    # Find all C source files and run clang-tidy using .clang-tidy config
    find src lib -name "*.c" -not -path "*/tests/*" | while read -r file; do
        echo "=== Analyzing: $file ==="

        # Run clang-tidy on each file using .clang-tidy configuration
        if ! clang-tidy "$file" -p=build_tidy --format-style=none 2>&1; then
            echo "Failed to analyze $file"
        fi
        echo ""
    done
fi

echo ""
echo "=== clang-tidy Analysis Complete ==="
echo ""
echo "Summary of analysis completed for ASCII-Chat source files."
echo "Any issues found have been reported above."
echo ""
'@

    # Write script with proper line endings
    $ScriptContent -replace "`r`n", "`n" | Out-File -FilePath $TempScript -Encoding UTF8 -NoNewline

    # Run container and stream output directly (no web server needed)
    Write-Host "Starting clang-tidy analysis container..." -ForegroundColor Yellow

    # When Suite is "clang-tidy", combine Test and TestTargets for file arguments
    $FilesToCheck = @()
    if ($Test -and $Test -ne "") {
        $FilesToCheck += $Test
    }
    if ($TestTargets -and $TestTargets.Count -gt 0) {
        $FilesToCheck += $TestTargets
    }

    $ScriptArgs = ""
    if ($FilesToCheck.Count -gt 0) {
        $ProcessedFiles = @()

        foreach ($Item in $FilesToCheck) {
            $TestPath = Join-Path $RepoRoot $Item

            if (Test-Path $TestPath) {
                if (Test-Path $TestPath -PathType Container) {
                    # It's a directory - find all .c files in it
                    Write-Host "Finding .c files in directory: $Item" -ForegroundColor Gray
                    $DirCFiles = Get-ChildItem -Path $TestPath -Filter "*.c" -Recurse |
                                 ForEach-Object {
                                     $relativePath = $_.FullName.Replace($RepoRoot, "").TrimStart('\', '/')
                                     $relativePath -replace '\\', '/'
                                 }
                    if ($DirCFiles) {
                        $ProcessedFiles += $DirCFiles
                    } else {
                        Write-Host "Warning: No .c files found in directory: $Item" -ForegroundColor Yellow
                    }
                } elseif ($Item -match '\.c$') {
                    # It's a .c file
                    $ProcessedFiles += $Item
                } else {
                    Write-Host "Warning: Skipping non-.c file: $Item" -ForegroundColor Yellow
                }
            } else {
                Write-Host "Warning: Path does not exist: $Item" -ForegroundColor Yellow
            }
        }

        if ($ProcessedFiles.Count -gt 0) {
            $ScriptArgs = " " + ($ProcessedFiles -join " ")
        }
    }

    $DockerRepoRoot = ConvertTo-DockerPath $RepoRoot
    docker run `
        --rm `
        --name $ContainerName `
        -v "${DockerRepoRoot}:/app" `
        -v "${CcacheVolume}:/ccache" `
        -v "${DepsCacheVolume}:/deps-cache" `
        -e CCACHE_DIR=/ccache `
        -e DEPS_CACHE_BASE=/deps-cache `
        -w /app `
        $ImageName `
        bash -c "/app/temp-clang-tidy.sh$ScriptArgs"

    $ExitCode = $LASTEXITCODE

    # Clean up temporary script
    Remove-Item $TempScript -ErrorAction SilentlyContinue

    if ($ExitCode -eq 0) {
        Write-Host ""
        Write-Host "clang-tidy analysis completed successfully!" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "clang-tidy analysis failed with exit code: $ExitCode" -ForegroundColor Red
    }

    exit $ExitCode
}

# Prepare the command to run (for non-scan-build modes)
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
        $TestCommand += " --verbose"
    }

    if ($Filter) {
        $TestCommand += " -f `"$Filter`""
    }

    # Use Suite and Test parameters if provided
    if ($Suite) {
        $TestCommand += " $Suite"
        if ($Test) {
            $TestCommand += " $Test"
        }
    } elseif ($TestTargets.Count -gt 0) {
        # Fall back to positional arguments for backward compatibility
        $TestCommand += " " + ($TestTargets -join " ")
    }

    # Configure CMake if build_docker doesn't exist or -Clean is specified
    # Let run_tests.sh handle building only the specific test executables needed
    if ($Clean) {
        Write-Host "Clean rebuild requested - removing build_docker directory" -ForegroundColor Yellow
        $BuildCommand = @"
echo 'Clean rebuild - removing build_docker directory...'
rm -rf build_docker
echo 'Configuring CMake with Docker-specific deps cache...'
CC=clang CXX=clang++ DEPS_CACHE_BASE=/deps-cache cmake -B build_docker -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_STANDARD=23 -DCMAKE_C_FLAGS='-std=c2x' -DBUILD_TESTS=ON
echo 'CMake configuration complete. run_tests.sh will build only the test executables needed.'
"@
    } else {
        $BuildCommand = @"
if [ ! -d build_docker ]; then
    echo 'First time setup - configuring CMake with Docker-specific deps cache...'
    CC=clang CXX=clang++ DEPS_CACHE_BASE=/deps-cache cmake -B build_docker -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_STANDARD=23 -DCMAKE_C_FLAGS='-std=c2x' -DBUILD_TESTS=ON
    echo 'CMake configuration complete. run_tests.sh will build only the test executables needed.'
else
    echo 'Using existing build_docker directory (run_tests.sh will build only the test executables needed)'
fi
"@
    }

    $FullCommand = "$BuildCommand && $TestCommand"
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
$DockerRepoRoot = ConvertTo-DockerPath $RepoRoot
if ($Interactive) {
    docker run `
        $DockerFlags `
        --rm `
        --name $ContainerName `
        -v "${DockerRepoRoot}:/app" `
        -v "${CcacheVolume}:/ccache" `
        -v "${DepsCacheVolume}:/deps-cache" `
        -e CCACHE_DIR=/ccache `
        -e DEPS_CACHE_BASE=/deps-cache `
        -w /app `
        $ImageName `
        /bin/bash
} else {
    docker run `
        $DockerFlags `
        --rm `
        --name $ContainerName `
        -v "${DockerRepoRoot}:/app" `
        -v "${CcacheVolume}:/ccache" `
        -v "${DepsCacheVolume}:/deps-cache" `
        -e CCACHE_DIR=/ccache `
        -e DEPS_CACHE_BASE=/deps-cache `
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

# Show ccache statistics after test runs (for non-interactive mode)
if (-not $Interactive) {
    Write-Host ""
    Write-Host "ccache Statistics:" -ForegroundColor Cyan
    docker run `
        --rm `
        -v "${CcacheVolume}:/ccache" `
        -e CCACHE_DIR=/ccache `
        $ImageName `
        ccache --show-stats | Select-Object -First 10
}

exit $ExitCode
