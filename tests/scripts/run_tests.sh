#!/usr/bin/env bash

# =============================================================================
# ascii-chat Test Runner Script
# =============================================================================
# This script unifies all the common test running patterns from the Makefile
# into a single, reusable script that can be called from various contexts.
#
# BUILD DIRECTORY DETECTION:
#   - Docker environment (/.docker exists): always uses build_docker/
#   - Native environment: uses build/
#   - Centralized in get_build_directory() function - use that instead of
#     hardcoding paths or adding environment detection logic

set -uo pipefail # Remove 'e' flag so we can handle errors ourselves
set -m           # Enable job control for better signal handling

# Make sure terminal interrupt character is set properly
stty intr '^C' 2>/dev/null || true
# Make sure we're in the foreground process group
if [[ -t 0 ]]; then
  # We have a TTY, ensure we can receive signals
  stty isig 2>/dev/null || true
fi

# =============================================================================
# Signal Handling
# =============================================================================

# Track if we've been interrupted
INTERRUPTED=0

# Global test result variables (no subshells needed!)
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_TIMEDOUT=0
TESTS_STARTED=0
FAILED_TEST_NAMES=()  # Array to track failed test names
TIMEDOUT_TEST_NAMES=()  # Array to track timed-out test names

# Test timeout in seconds
TEST_TIMEOUT=25

# Check if wait -n is supported (bash 4.3+)
WAIT_N_SUPPORTED=0
# Check bash version - wait -n was added in bash 4.3
if [[ "${BASH_VERSINFO[0]}" -gt 4 ]] || ([[ "${BASH_VERSINFO[0]}" -eq 4 ]] && [[ "${BASH_VERSINFO[1]}" -ge 3 ]]); then
  WAIT_N_SUPPORTED=1
fi

# Print cancellation message
function print_cancellation_message() {
  echo ""
  echo "=========================================="
  echo "❌ TESTS CANCELLED BY USER (Ctrl-C)"
  echo "=========================================="
}

# Cleanup function for Ctrl-C
function handle_interrupt() {
  # Set interrupted flag immediately
  INTERRUPTED=1

  # Disable the trap immediately to prevent re-entry
  trap - INT TERM HUP QUIT

  # Print cancellation message to the terminal directly (before redirecting output)
  {
    echo ""
    echo ""
    echo "=========================================="
    echo "❌ TESTS CANCELLED BY USER (Ctrl-C)"
    echo "=========================================="
  } >/dev/tty 2>&1 || echo "TESTS CANCELLED BY USER (Ctrl-C)"

  # Kill only our specific test PIDs (from both global arrays and job control)
  # Kill any PIDs tracked in the global running_pids array (for parallel tests)
  if [[ -n "${running_pids:-}" ]]; then
    for pid in "${running_pids[@]:-}"; do
      kill -9 "$pid" >/dev/null 2>&1 || true
      kill -9 -"$pid" >/dev/null 2>&1 || true  # Kill process group too
    done
  fi

  # Kill only our direct child processes (jobs started by this script instance)
  jobs -p | xargs -r kill -9 >/dev/null 2>&1 || true

  # Clean up temp files silently
  rm -f /tmp/worker_*_$$.* /tmp/test_*_$$.* /tmp/queue_*_$$.* /tmp/junit_*_$$.* /tmp/failed_tests_$$.txt >/dev/null 2>&1 || true

  # Force immediate termination - no questions asked
  kill -9 $$ 2>/dev/null || exit 130
}

# Set up signal handlers - use INT instead of SIGINT for better compatibility
trap handle_interrupt INT TERM HUP QUIT

# Enable job control for better process management
set -m

# Make sure signals are not ignored
trap - TSTP
trap - TTIN
trap - TTOU

# =============================================================================
# Configuration
# =============================================================================

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Default values
BUILD_TYPE="debug"
TEST_TYPE="all"
GENERATE_JUNIT=""
VERBOSE=""
COVERAGE=""
JOBS=""
SINGLE_TEST=""
FILTER=""
MULTIPLE_TEST_MODE=""
MULTIPLE_TESTS=()
LOG_FILE=""
PARALLEL_TESTS="1" # Default to parallel execution
NO_PARALLEL=""

# Test categories
TEST_CATEGORIES=("unit" "integration" "performance")

# =============================================================================
# Logging Functions
# =============================================================================

function log_info() {
  echo "ℹ️ [INFO] $*" >&2
}

function log_error() {
  echo "❌ [ERROR] $*" >&2
}

function log_success() {
  echo "✅ [SUCCESS] $*" >&2
}

function log_warning() {
  echo "⚠️ [WARNING] $*" >&2
}

function log_verbose() {
  if [[ -n "$VERBOSE" ]]; then
    echo "🔍 [VERBOSE] $*" >&2
  fi
}

# =============================================================================
# Help and Documentation
# =============================================================================

function show_help() {
  cat <<EOF
ascii-chat Test Runner

Usage: $0 [OPTIONS] [TEST_NAME]

OPTIONS:
    -t, --type TYPE         Test type: all, unit, integration, performance (default: all)
    -b, --build TYPE        Build type: debug, dev, release, coverage, tsan (default: debug)
    -j, --jobs N            Number of parallel test executables to run and jobs per test (default: auto-detect CPU cores and auto-detected jobs)
    -J, --junit             Generate JUnit XML output
    -l, --log-file PATH     Save test output to specified log file (default: tests.log in current dir)
    -v, --verbose           Verbose output
    -c, --coverage          Enable coverage (overrides build type)
    -f, --filter PATTERN    Filter tests by pattern with wildcard support (only works with single test binary)
    --no-parallel           Disable parallel test execution (tests run in parallel by default)
    -h, --help              Show this help message

ARGUMENTS:
    TEST_TYPE               Run all tests of a type (e.g., unit, integration, performance)
    TEST_NAME               Run a single test binary by name or path (e.g., test_unit_ascii, unit_ascii, bin/test_unit_ascii, /path/to/test)
    TYPE NAME               Run a specific test by type and name (e.g., unit options, integration crypto_network)
    TYPE NAME1 NAME2...     Run multiple tests of the same type (e.g., unit options audio, integration crypto_network neon_color_renderers)

EXAMPLES:
    $0                                    # Run all tests with debug build
    $0 unit                               # Run all unit tests programmatically
    $0 integration                        # Run all integration tests with debug build
    $0 performance                        # Run all performance tests with release build
    $0 -t unit -b release                 # Run unit tests with release build
    $0 -t performance -J                  # Run performance tests with JUnit output
    $0 -c -t integration                  # Run integration tests with coverage
    $0 -b coverage -J                     # Run all tests with coverage+JUnit
    $0 -b debug                          # Run with AddressSanitizer for memory checking
    $0 -b tsan                           # Run with ThreadSanitizer for race detection
    $0 --log-file=/tmp/my_test.log unit  # Run unit tests with custom log file
    $0 test_unit_ascii                    # Run single test binary by name
    $0 unit_ascii                         # Run single test binary without test_ prefix
    $0 bin/test_unit_ascii                # Run single test binary by relative path
    $0 unit options                       # Run test_unit_options by type and name
    $0 unit options audio buffer_pool      # Run multiple unit tests
    $0 integration crypto_network          # Run test_integration_crypto_network
    $0 test_unit_ascii -f "basic"         # Run tests matching "*basic*" (wildcards added automatically)
    $0 test_unit_ascii -f "*basic*"       # Same as above (asterisks stripped if provided)
    $0 -J -l /tmp/custom.log              # Generate JUnit XML and save logs to custom file

BUILD TYPES:
    debug                 - Debug build with AddressSanitizer (default, safe)
    dev                   - Debug build without sanitizers (faster iteration)
    release               - Release build with optimizations and LTO
    coverage              - Debug build with coverage instrumentation
    tsan                  - Debug build with ThreadSanitizer for race detection

TEST TYPES:
    all                   - Run all test categories
    unit                  - Run only unit tests
    integration           - Run only integration tests
    performance           - Run only performance tests

EOF
}

# =============================================================================
# System and Build Utilities
# =============================================================================

# Note: colored_make function removed - now using CMake exclusively
# CMake handles both initial builds and incremental updates efficiently

# Determine build directory based on environment
# In Docker: always use build_docker
# Otherwise: use build
function get_build_directory() {
  if [ -f /.dockerenv ]; then
    echo "build_docker"
  else
    echo "build"
  fi
}

# Run cmake --build with optional grc colorization
function cmake_build() {
  local build_dir="$1"
  shift  # Remove first argument, pass the rest to cmake

  # Use grc for colored output if available (works with Ninja)
  if command -v grc >/dev/null 2>&1; then
    grc --colour=auto cmake --build "$build_dir" "$@"
  else
    cmake --build "$build_dir" "$@"
  fi
}

# Detect number of CPU cores
function detect_cpu_cores() {
  if [[ -n "$JOBS" ]]; then
    echo "$JOBS"
    return
  fi

  case "$(uname -s)" in
  Darwin)
    sysctl -n hw.logicalcpu
    ;;
  Linux)
    nproc
    ;;
  *)
    echo "4" # fallback
    ;;
  esac
}

# =============================================================================
# Test Discovery and File Management
# =============================================================================

# Get test executables for a specific category
function get_test_executables() {
  local category="$1"
  local build_type="${2:-debug}" # Optional build type parameter

  # Use centralized build directory detection
  local cmake_build_dir=$(get_build_directory)
  local bin_dir="$PROJECT_ROOT/$cmake_build_dir/bin"

  if [[ ! -d "$bin_dir" ]]; then
    log_verbose "No bin directory found: $bin_dir"
    return
  fi

  log_verbose "Using test executables from: $bin_dir"

  # Handle coverage builds differently
  if [[ "$build_type" == "coverage" ]]; then
    case "$category" in
    unit)
      # For coverage, discover test source files and build coverage executables
      for test_file in "$PROJECT_ROOT/tests/unit"/*_test.c; do
        if [[ -f "$test_file" ]]; then
          test_name=$(basename "$test_file" _test.c)
          executable_name="test_unit_${test_name}"
          echo "$bin_dir/$executable_name"
        fi
      done | sort
      ;;
    integration)
      # For coverage, discover test source files and build coverage executables
      for test_file in "$PROJECT_ROOT/tests/integration"/*_test.c; do
        if [[ -f "$test_file" ]]; then
          test_name=$(basename "$test_file" _test.c)
          executable_name="test_integration_${test_name}"
          echo "$bin_dir/$executable_name"
        fi
      done | sort
      ;;
    performance)
      # For coverage, discover test source files and build coverage executables
      for test_file in "$PROJECT_ROOT/tests/performance"/*_test.c; do
        if [[ -f "$test_file" ]]; then
          test_name=$(basename "$test_file" _test.c)
          executable_name="test_performance_${test_name}"
          echo "$bin_dir/$executable_name"
        fi
      done | sort
      ;;
    all)
      # All coverage tests
      for f in "$bin_dir"/test_*; do
        [[ -f "$f" ]] && echo "$f"
      done | sort
      ;;
    *)
      log_error "Unknown test category: $category"
      return 1
      ;;
    esac
  else
    # Regular builds discover from source files (same as coverage builds)
    # This ensures we discover all tests, not just ones already built
    case "$category" in
    unit)
      # Discover test source files and build executables
      for test_file in "$PROJECT_ROOT/tests/unit"/*_test.c; do
        if [[ -f "$test_file" ]]; then
          test_name=$(basename "$test_file" _test.c)
          executable_name="test_unit_${test_name}"
          echo "$bin_dir/$executable_name"
        fi
      done | sort
      ;;
    integration)
      # Discover test source files and build executables
      for test_file in "$PROJECT_ROOT/tests/integration"/*_test.c; do
        if [[ -f "$test_file" ]]; then
          test_name=$(basename "$test_file" _test.c)
          executable_name="test_integration_${test_name}"
          echo "$bin_dir/$executable_name"
        fi
      done | sort
      ;;
    performance)
      # Discover test source files and build executables
      for test_file in "$PROJECT_ROOT/tests/performance"/*_test.c; do
        if [[ -f "$test_file" ]]; then
          test_name=$(basename "$test_file" _test.c)
          executable_name="test_performance_${test_name}"
          echo "$bin_dir/$executable_name"
        fi
      done | sort
      ;;
    all)
      # For "all", discover all test source files
      for test_file in "$PROJECT_ROOT/tests/unit"/*_test.c "$PROJECT_ROOT/tests/integration"/*_test.c "$PROJECT_ROOT/tests/performance"/*_test.c; do
        if [[ -f "$test_file" ]]; then
          test_name=$(basename "$test_file" _test.c)
          # Determine category from path
          if [[ "$test_file" == */unit/* ]]; then
            executable_name="test_unit_${test_name}"
          elif [[ "$test_file" == */integration/* ]]; then
            executable_name="test_integration_${test_name}"
          elif [[ "$test_file" == */performance/* ]]; then
            executable_name="test_performance_${test_name}"
          else
            continue
          fi
          echo "$bin_dir/$executable_name"
        fi
      done | sort
      ;;
    *)
      log_error "Unknown test category: $category"
      return 1
      ;;
    esac
  fi
}

# Build tests if they don't exist
function ensure_tests_built() {
  local build_type="$1"
  local test_type="$2"

  cd "$PROJECT_ROOT"

  # Use centralized build directory detection
  local cmake_build_dir=$(get_build_directory)

  # Ensure build directory exists
  if [[ ! -d "$cmake_build_dir" ]]; then
    log_info "Creating build directory: $cmake_build_dir"
    mkdir -p "$cmake_build_dir"
  fi

  # Check if we have a build directory with CMake cache
  if [[ -d "$cmake_build_dir" ]] && [[ -f "$cmake_build_dir/CMakeCache.txt" ]]; then
    # Build directory exists, use CMake for incremental builds
    log_verbose "Using CMake for incremental build (build directory exists: $cmake_build_dir)"

    # No need to build anything here - we'll build specific test targets later
    # The static library will be built automatically as a dependency
  else
    # No build directory or CMake cache, use CMake for full build
    log_info "Using CMake build system (no build directory found)"

    if [[ ! -f "CMakeLists.txt" ]]; then
      log_error "No CMakeLists.txt found and no build directory exists!"
      return 1
    fi

    if ! command -v cmake >/dev/null 2>&1; then
      log_error "CMake not found but required for initial build!"
      return 1
    fi

    # Configure CMake with appropriate flags based on build type
    local cmake_build_type
    local cmake_flags="-DCMAKE_C_STANDARD=23 -DCMAKE_C_FLAGS='-std=c2x' -DBUILD_TESTS=ON"

    case "$build_type" in
    debug)
      cmake_build_type="Debug"
      cmake_flags="$cmake_flags -DCMAKE_C_FLAGS='-std=c2x'"
      ;;
    dev)
      cmake_build_type="Debug"
      # No sanitizers for dev builds
      ;;
    release)
      cmake_build_type="Release"
      ;;
    coverage)
      cmake_build_type="Debug"
      cmake_flags="$cmake_flags -DCMAKE_C_FLAGS='-std=c2x --coverage'"
      ;;
    tsan)
      cmake_build_type="Debug"
      cmake_flags="$cmake_flags -DCMAKE_C_FLAGS='-std=c2x -fsanitize=thread'"
      ;;
    *)
      log_error "Unknown build type: $build_type"
      return 1
      ;;
    esac

    log_info "Configuring CMake build in $cmake_build_dir (build type: $cmake_build_type)..."
    echo CC=clang CXX=clang++ cmake -B "$cmake_build_dir" -G Ninja \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_BUILD_TYPE="$cmake_build_type" $cmake_flags

    # Build with CMake
    log_info "Building tests with CMake..."
    cmake_build "$cmake_build_dir"

    # For integration tests, also build server and client binaries
    if [[ "$test_type" == "integration" ]] || [[ "$test_type" == "all" ]]; then
      log_info "🔨 Building server and client binaries for integration tests..."
      cmake_build "$cmake_build_dir" --target ascii-chat ascii-chat
    fi
  fi
}

# =============================================================================
# Test Execution Functions
# =============================================================================

function clean_verbose_test_output() {
  tail -n+2 | grep --color=always -v -E '\[.*SKIP.*\]' | grep --color=always -v -E '\[.*RUN.*\]'
}

# Old exec_test_executable function removed - replaced with simplified architecture

# =============================================================================
# JUnit XML Generation Functions
# =============================================================================

# Generate JUnit XML for a test result
function generate_junit_xml() {
  local test_name=""
  local test_class=""
  local duration=""
  local test_exit_code=""
  local xml_file=""
  local output_file=""
  local junit_file=""

  # Parse flags
  while [[ $# -gt 0 ]]; do
    case $1 in
      --test-name)
        test_name="$2"
        shift 2
        ;;
      --test-class)
        test_class="$2"
        shift 2
        ;;
      --duration)
        duration="$2"
        shift 2
        ;;
      --exit-code)
        test_exit_code="$2"
        shift 2
        ;;
      --xml-file)
        xml_file="$2"
        shift 2
        ;;
      --output-file)
        output_file="$2"
        shift 2
        ;;
      --junit-file)
        junit_file="$2"
        shift 2
        ;;
      *)
        echo "Unknown option: $1" >&2
        return 1
        ;;
    esac
  done

    if [[ $test_exit_code -eq 0 ]]; then
    # Test passed
      if [[ -f "$xml_file" ]] && [[ -s "$xml_file" ]]; then
        # Transform the XML to match our naming convention
        if grep -q '<testsuite' "$xml_file"; then
          sed -n '/<testsuite/,/<\/testsuite>/p' "$xml_file" |
            sed -e "s/<testsuite name=\"[^\"]*\"/<testsuite name=\"$test_class\"/" \
              -e "s/<testcase name=\"/<testcase classname=\"$test_class\" name=\"/" >>"$junit_file"
        else
          # XML exists but no testsuite - create one
        # Format duration properly for JUnit XML (ensure it's a valid decimal)
        local formatted_duration=$(printf "%.3f" "$duration")
        echo "<testsuite name=\"$test_class\" tests=\"1\" failures=\"0\" errors=\"0\" time=\"$formatted_duration\">" >>"$junit_file"
        echo "  <testcase classname=\"$test_class\" name=\"all\" time=\"$formatted_duration\"/>" >>"$junit_file"
          echo "</testsuite>" >>"$junit_file"
        fi
        rm -f "$xml_file"
      else
        # Test passed but no XML generated - create a minimal entry
      # Format duration properly for JUnit XML (ensure it's a valid decimal)
      local formatted_duration=$(printf "%.3f" "$duration")
      echo "<testsuite name=\"$test_class\" tests=\"1\" failures=\"0\" errors=\"0\" time=\"$formatted_duration\">" >>"$junit_file"
      echo "  <testcase classname=\"$test_class\" name=\"all\" time=\"$formatted_duration\"/>" >>"$junit_file"
        echo "</testsuite>" >>"$junit_file"
      fi
      rm -f "$output_file"
    else
    # Test failed - determine failure type and generate appropriate XML
      local failure_type="TestFailure"
      local failure_msg="Test failed with exit code $test_exit_code"

      if [[ $test_exit_code -eq 124 ]]; then
        failure_type="Timeout"
        failure_msg="Test timed out after ${TEST_TIMEOUT} seconds"
      elif [[ $test_exit_code -gt 128 ]]; then
        # Exit code > 128 usually means killed by signal
        local signal=$((test_exit_code - 128))
        failure_type="Crash"
        failure_msg="Test crashed with signal $signal"
      fi

      # Try to use generated XML if available
      if [[ -f "$xml_file" ]] && [[ -s "$xml_file" ]] && grep -q '<testsuite' "$xml_file"; then
        # Use the XML but ensure it shows as failed
        sed -n '/<testsuite/,/<\/testsuite>/p' "$xml_file" |
          sed -e "s/<testsuite name=\"[^\"]*\"/<testsuite name=\"$test_class\"/" \
            -e "s/<testcase name=\"/<testcase classname=\"$test_class\" name=\"/" >>"$junit_file"
        rm -f "$xml_file"
      else
        # No usable XML - create comprehensive failure entry
        local error_count=0
        local failure_count=1
        if [[ "$failure_type" == "Crash" ]] || [[ "$failure_type" == "Timeout" ]]; then
          error_count=1
          failure_count=0
        fi

      # Format duration properly for JUnit XML (ensure it's a valid decimal)
      local formatted_duration=$(printf "%.3f" "$duration")
      echo "<testsuite name=\"$test_class\" tests=\"1\" failures=\"$failure_count\" errors=\"$error_count\" time=\"$formatted_duration\">" >>"$junit_file"
      echo "  <testcase classname=\"$test_class\" name=\"all\" time=\"$formatted_duration\">" >>"$junit_file"

        # Include last 50 lines of output in the failure message
        local output_tail=""
        if [[ -f "$output_file" ]]; then
          output_tail=$(tail -50 "$output_file" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g; s/"/\&quot;/g; s/'"'"'/\&apos;/g')
        fi

        if [[ $error_count -eq 1 ]]; then
          echo "    <error message=\"$failure_msg\" type=\"$failure_type\">" >>"$junit_file"
        else
          echo "    <failure message=\"$failure_msg\" type=\"$failure_type\">" >>"$junit_file"
        fi

        echo "Exit code: $test_exit_code" >>"$junit_file"
        echo "" >>"$junit_file"
        echo "Last 50 lines of output:" >>"$junit_file"
        echo "$output_tail" >>"$junit_file"

        if [[ $error_count -eq 1 ]]; then
          echo "    </error>" >>"$junit_file"
        else
          echo "    </failure>" >>"$junit_file"
        fi

        echo "  </testcase>" >>"$junit_file"
        echo "</testsuite>" >>"$junit_file"
      fi

      rm -f "$output_file" "$xml_file"
  fi
}

# Determine test failure type and message
function determine_test_failure() {
  local test_exit_code=""
  local test_name=""
  local duration=""

  # Parse flags
  while [[ $# -gt 0 ]]; do
    case $1 in
      --exit-code)
        test_exit_code="$2"
        shift 2
        ;;
      --test-name)
        test_name="$2"
        shift 2
        ;;
      --duration)
        duration="$2"
        shift 2
        ;;
      *)
        echo "Unknown option: $1" >&2
        return 1
        ;;
    esac
  done

  if [[ $test_exit_code -eq 0 ]]; then
    echo "✅ [TEST] PASSED: $test_name (${duration}s)"
    return 0
  else
    local failure_type="TestFailure"
    local failure_msg="Test failed with exit code $test_exit_code"
    local emoji="❌"

    if [[ $test_exit_code -eq 130 ]]; then
      # Interrupted by Ctrl-C
      failure_type="Interrupted"
      failure_msg="Test cancelled by user (Ctrl-C)"
      emoji="🛑"
      echo "$emoji [TEST] CANCELLED: $test_name"
    elif [[ $test_exit_code -eq 124 ]]; then
      failure_type="Timeout"
      failure_msg="Test timed out after ${TEST_TIMEOUT} seconds"
      emoji="⏱️"
      echo "$emoji [TEST] FAILED: $test_name (${duration}s, exit code: $test_exit_code)"
    elif [[ $test_exit_code -gt 128 ]]; then
      # Exit code > 128 usually means killed by signal
      local signal=$((test_exit_code - 128))
      failure_type="Crash"
      failure_msg="Test crashed with signal $signal"
      emoji="💥"
      echo "$emoji [TEST] FAILED: $test_name (${duration}s, exit code: $test_exit_code)"
    else
      echo "$emoji [TEST] FAILED: $test_name (${duration}s, exit code: $test_exit_code)"
    fi
      return $test_exit_code
  fi
}

# =============================================================================
# SIMPLIFIED TEST EXECUTION - SHARED SPAWNING + TWO EXECUTION MODES
# =============================================================================

# Shared function to spawn a single test (sync or async)
function spawn_test() {
  local test_executable="$1"
  local background="$2"  # "sync" or "async"

  local test_name=$(basename "$test_executable")

  # Use JOBS if specified, otherwise use 0 for auto-detection
  local jobs_arg="0"
  if [[ -n "$JOBS" ]] && [[ "$JOBS" -gt 0 ]]; then
    jobs_arg="$JOBS"
  fi

  local test_args=("--jobs" "$jobs_arg" "--timeout" "$TEST_TIMEOUT" "--color=always" "--short-filename")
  if [[ -n "$VERBOSE" ]]; then
    test_args+=(--verbose)
    log_verbose "Adding --verbose flag to $test_name (VERBOSE=$VERBOSE)"
  fi

  if [[ "$background" == "async" ]]; then
    # Background execution - return PID
    log_verbose "EXECUTING ASYNC: $test_executable ${test_args[*]}"

    if [[ -n "$VERBOSE" ]]; then
      # In verbose mode, show output in real-time
      {
        TESTING=1 CRITERION_TEST=1 "$test_executable" "${test_args[@]}"
        echo "EXIT_CODE:$?" > /tmp/test_${test_name}_$$.exitcode
      } 2>&1 &
    else
      # In non-verbose mode, capture output to file
      {
        TESTING=1 CRITERION_TEST=1 "$test_executable" "${test_args[@]}"
        echo "EXIT_CODE:$?" > /tmp/test_${test_name}_$$.exitcode
      } > /tmp/test_${test_name}_$$.log 2>&1 &
    fi
    local pid=$!
    # Write PID to file to avoid mixing with test output
    echo "$pid" > /tmp/test_${test_name}_$$.pid
  else
    # Synchronous execution - return exit code
    # Always show output in real-time
    log_verbose "EXECUTING SYNC: $test_executable ${test_args[*]}"
    # Redirect test output to stderr so it's visible but not captured in the return value
    TESTING=1 CRITERION_TEST=1 "$test_executable" "${test_args[@]}" 2>&1 | tee /tmp/test_${test_name}_$$.log >&2
    local exit_code=${PIPESTATUS[0]}  # Get exit code of test, not tee
    echo $exit_code  # Return only the exit code to stdout
  fi
}

# Function 1: Run tests sequentially (no parallelism)
function run_tests_sequential() {
  local test_list=("$@")
  local passed=0
  local failed=0
  local timedout=0
  local started=0
  local failed_tests=()  # Array to track failed test names
  local timedout_tests=()  # Array to track timed-out test names

  log_info "🔄 Running ${#test_list[@]} tests sequentially"

  for test_executable in "${test_list[@]}"; do
    # Check for interrupt before each test
    if [[ $INTERRUPTED -eq 1 ]]; then
      log_info "❌ Tests interrupted by user"
      break
    fi

    local test_name=$(basename "$test_executable")
    echo "🚀 [TEST] Starting: $test_name"
    ((started++))

    # Run test synchronously using shared spawning function
    local start_time=$(date +%s.%N)
    # Use the shared spawn_test function for consistency - it handles verbose flag properly
    local exit_code=$(spawn_test "$test_executable" "sync")
    local end_time=$(date +%s.%N)
    local duration
    if command -v bc >/dev/null 2>&1; then
      duration=$(echo "$end_time - $start_time" | bc -l)
    else
      # Fallback: use awk for basic arithmetic if bc is not available
      duration=$(awk "BEGIN {printf \"%.3f\", $end_time - $start_time}")
    fi

    # Report result
    if [[ $exit_code -eq 0 ]]; then
      echo -e "✅ [TEST] \033[32mPASSED\033[0m: $test_name ($(printf "%.2f" $duration)s)"
      ((passed++))
    elif [[ $exit_code -eq 124 ]]; then
      echo -e "⏱️ [TEST] \033[33mTIMEOUT\033[0m: $test_name (exceeded ${TEST_TIMEOUT}s)"
      ((timedout++))
      timedout_tests+=("$test_name")  # Track timed-out test name
    else
      echo -e "❌ [TEST] \033[31mFAILED\033[0m: $test_name (exit: $exit_code, $(printf "%.2f" $duration)s)"
      ((failed++))
      failed_tests+=("$test_name")  # Track failed test name
    fi
  done

  # Return results via global variables (no subshell needed)
  TESTS_PASSED=$passed
  TESTS_FAILED=$failed
  TESTS_TIMEDOUT=$timedout
  TESTS_STARTED=$started
  if [ ${#failed_tests[@]} -gt 0 ]; then
    FAILED_TEST_NAMES=("${failed_tests[@]}")  # Copy failed test names to global array
  else
    FAILED_TEST_NAMES=()  # Initialize as empty array if no failed tests
  fi
  if [ ${#timedout_tests[@]} -gt 0 ]; then
    TIMEDOUT_TEST_NAMES=("${timedout_tests[@]}")  # Copy timed-out test names to global array
  else
    TIMEDOUT_TEST_NAMES=()  # Initialize as empty array if no timed-out tests
  fi
}

# Function 2: Run tests in parallel (up to max_parallel at once)
function run_tests_parallel() {
  local max_parallel=$1
  shift
  local test_list=("$@")
  local passed=0
  local failed=0
  local timedout=0
  local started=0
  local failed_tests=()  # Array to track failed test names
  local timedout_tests=()  # Array to track timed-out test names

  log_info "🚀 Running ${#test_list[@]} tests in parallel (up to $max_parallel concurrent)"

  local running_pids=()
  local running_names=()
  local running_start_times=()
  local test_index=0

  # Main execution loop - NO SUBSHELLS
  while [[ $test_index -lt ${#test_list[@]} ]] || [[ ${#running_pids[@]} -gt 0 ]]; do
    # Check for interrupt - this will work since we're in main process
    if [[ $INTERRUPTED -eq 1 ]]; then
      log_info "❌ Tests interrupted by user - killing running tests"
      for pid in "${running_pids[@]}"; do
        # Kill the entire process group to make sure we get all child processes
        kill -9 -"$pid" 2>/dev/null || true
        kill -9 "$pid" 2>/dev/null || true
      done
      break
    fi

    # Clean up finished tests - use numeric iteration to avoid sparse array issues
    local new_pids=()
    local new_names=()
    local new_start_times=()
    # Iterate over actual indices that exist in the arrays
    for i in "${!running_pids[@]}"; do
      # Check if process is still running
      if kill -0 "${running_pids[i]}" 2>/dev/null; then
        # Still running
        new_pids+=("${running_pids[i]}")
        new_names+=("${running_names[i]:-unknown}")
        new_start_times+=("${running_start_times[i]:-$(date +%s.%N)}")
      else
        # Finished - get results
        local test_name="${running_names[i]:-unknown}"
        wait "${running_pids[i]}" 2>/dev/null

        # Get the actual exit code from the file
        local exit_code=1  # Default to failure
        if [[ -f "/tmp/test_${test_name}_$$.exitcode" ]]; then
          local exit_line=$(cat "/tmp/test_${test_name}_$$.exitcode" 2>/dev/null)
          if [[ "$exit_line" =~ EXIT_CODE:([0-9]+) ]]; then
            exit_code="${BASH_REMATCH[1]}"
          fi
          rm -f "/tmp/test_${test_name}_$$.exitcode"
        fi

        local end_time=$(date +%s.%N)
        local start_time="${running_start_times[i]:-$end_time}"
        local duration
        if command -v bc >/dev/null 2>&1; then
          duration=$(echo "$end_time - $start_time" | bc -l)
        else
          # Fallback: use awk for basic arithmetic if bc is not available
          duration=$(awk "BEGIN {printf \"%.3f\", $end_time - $start_time}")
        fi

        # Show test output when it completes (only in non-verbose mode since verbose shows real-time)
        if [[ -f "/tmp/test_${test_name}_$$.log" ]]; then
          # Only show summary in non-verbose mode (verbose already showed everything)
          tail -5 "/tmp/test_${test_name}_$$.log" | grep -E "Synthesis:|PASSED|FAILED|Error" || tail -5 "/tmp/test_${test_name}_$$.log"
          rm -f "/tmp/test_${test_name}_$$.log"
        fi

        if [[ $exit_code -eq 0 ]]; then
          echo -e "✅ [TEST] \033[32mPASSED\033[0m: $test_name ($(printf "%.2f" $duration)s)"
          ((passed++))
        elif [[ $exit_code -eq 124 ]]; then
          echo -e "⏱️ [TEST] \033[33mTIMEOUT\033[0m: $test_name (exceeded ${TEST_TIMEOUT}s)"
          ((timedout++))
          timedout_tests+=("$test_name")  # Track timed-out test name
        else
          echo -e "❌ [TEST] \033[31mFAILED\033[0m: $test_name (exit: $exit_code, $(printf "%.2f" $duration)s)"
          ((failed++))
          failed_tests+=("$test_name")  # Track failed test name
        fi
      fi
    done

    if [[ ${#new_pids[@]} -gt 0 ]]; then
      running_pids=("${new_pids[@]}")
      running_names=("${new_names[@]}")
      running_start_times=("${new_start_times[@]}")
    else
      running_pids=()
      running_names=()
      running_start_times=()
    fi

    # Check for interrupt again before launching new tests
    if [[ $INTERRUPTED -eq 1 ]]; then
      log_info "❌ Tests interrupted by user - stopping new test launches"
      break
    fi

    # Launch new tests if we have room
    while [[ ${#running_pids[@]} -lt $max_parallel ]] && [[ $test_index -lt ${#test_list[@]} ]] && [[ $INTERRUPTED -eq 0 ]]; do
      local test_executable="${test_list[$test_index]}"
      local test_name=$(basename "$test_executable")

      echo "🚀 [TEST] Starting: $test_name"
      ((started++))

      # Run test asynchronously using shared spawning function
      local start_time=$(date +%s.%N)
      spawn_test "$test_executable" "async"

      # Read PID from file
      local test_pid=""
      if [[ -f "/tmp/test_${test_name}_$$.pid" ]]; then
        test_pid=$(cat "/tmp/test_${test_name}_$$.pid")
        rm -f "/tmp/test_${test_name}_$$.pid"
      fi

      # Only add to arrays if we got a valid PID
      if [[ -n "$test_pid" ]] && [[ "$test_pid" =~ ^[0-9]+$ ]]; then
        running_pids+=($test_pid)
        running_names+=($test_name)
        running_start_times+=($start_time)
      else
        echo "❌ [TEST] Failed to spawn: $test_name"
        ((failed++))
      fi
      ((test_index++))
    done

    # Very short sleep to be EXTREMELY responsive to Ctrl-C
    sleep 0.01
  done

  # Return results via global variables (no subshell needed)
  TESTS_PASSED=$passed
  TESTS_FAILED=$failed
  TESTS_TIMEDOUT=$timedout
  TESTS_STARTED=$started
  if [ ${#failed_tests[@]} -gt 0 ]; then
    FAILED_TEST_NAMES=("${failed_tests[@]}")  # Copy failed test names to global array
  else
    FAILED_TEST_NAMES=()  # Initialize as empty array if no failed tests
  fi
  if [ ${#timedout_tests[@]} -gt 0 ]; then
    TIMEDOUT_TEST_NAMES=("${timedout_tests[@]}")  # Copy timed-out test names to global array
  else
    TIMEDOUT_TEST_NAMES=()  # Initialize as empty array if no timed-out tests
  fi
}

# =============================================================================
# Resource Allocation Function
# =============================================================================

# Calculate optimal resource allocation for parallel test execution
function calculate_resource_allocation() {
  local num_tests=$1
  local total_cores=$2
  local jobs=$3 # Optional: explicitly specified jobs

  local max_parallel_tests

  # If jobs explicitly specified, use that for parallel tests
  if [[ -n "$jobs" ]] && [[ "$jobs" -gt 0 ]]; then
    max_parallel_tests=$jobs
  else
    # Always use CORES/2 parallel executables for maximum CPU utilization
    max_parallel_tests=$((total_cores / 2))

    # Ensure at least 1 parallel test
    if [[ $max_parallel_tests -lt 1 ]]; then
      max_parallel_tests=1
    fi
  fi

  # Return the max parallel tests value
  echo "$max_parallel_tests"
}

# =============================================================================
# Core Test Running Functions
# =============================================================================

# Run a single test with proper error handling
# Old run_single_test function removed - replaced with simplified architecture


# =============================================================================
# Build Management Functions
# =============================================================================

function build_test_executable() {
  local test_name="$1"
  local build_type="$2"

  log_info "🔨 Building test: $test_name in $build_type mode"

  # Use centralized build directory detection
  local cmake_build_dir=$(get_build_directory)

  # Ensure build directory exists
  if [[ ! -d "$PROJECT_ROOT/$cmake_build_dir" ]]; then
    log_info "Creating build directory: $cmake_build_dir"
    mkdir -p "$PROJECT_ROOT/$cmake_build_dir"
  fi

  # Check if we have a build directory with CMake cache
  if [[ -d "$PROJECT_ROOT/$cmake_build_dir" ]] && [[ -f "$PROJECT_ROOT/$cmake_build_dir/CMakeCache.txt" ]]; then
    # Build directory exists, use CMake for incremental builds
    log_info "Using CMake for incremental build of $test_name"
    cd "$PROJECT_ROOT"
    cmake_build "$cmake_build_dir" --target "$test_name"
    local cmake_exit_code=$?

    if [[ $cmake_exit_code -eq 0 ]]; then
      log_success "Successfully built $test_name with CMake"
      return 0
    else
      log_error "Failed to build test with CMake: $test_name"
      return 1
    fi
  else
    # No build directory, use ensure_tests_built to set everything up
    log_info "No build directory found, using ensure_tests_built to configure CMake"
    ensure_tests_built "$build_type" "all"
    local ensure_exit_code=$?

    if [[ $ensure_exit_code -eq 0 ]]; then
      log_success "Successfully built $test_name with CMake via ensure_tests_built"
      return 0
    else
      log_error "Failed to build test with CMake: $test_name"
      return 1
    fi
  fi
}

# =============================================================================
# Single Test Binary Execution
# =============================================================================

# Old run_single_test_binary function removed - replaced with simplified architecture

# =============================================================================
# Main Entry Point
# =============================================================================

function main() {
  # Store positional arguments before processing options
  local positional_args=()

  # Parse command line arguments
  while [[ $# -gt 0 ]]; do
    case $1 in
    -t | --type)
      TEST_TYPE="$2"
      shift 2
      ;;
    -b | --build)
      BUILD_TYPE="$2"
      shift 2
      ;;
    -j | --jobs)
      JOBS="$2"
      shift 2
      ;;
    -J | --junit)
      GENERATE_JUNIT="1"
      shift
      ;;
    -v | --verbose)
      VERBOSE="1"
      shift
      ;;
    -c | --coverage)
      COVERAGE="1"
      shift
      ;;
    --no-parallel)
      NO_PARALLEL="1"
      PARALLEL_TESTS=""
      shift
      ;;
    -f | --filter)
      FILTER="$2"
      # Process filter pattern: strip asterisks if provided, add them if not
      if [[ -n "$FILTER" ]]; then
        # Remove leading and trailing asterisks if they exist
        FILTER="${FILTER#\*}"
        FILTER="${FILTER%\*}"
        # Add asterisks if the pattern doesn't already have them
        if [[ "$FILTER" != *"*"* ]]; then
          FILTER="*${FILTER}*"
        fi
      fi
      shift 2
      ;;
    -l | --log-file)
      LOG_FILE="$2"
      shift 2
      ;;
    --log-file=*)
      LOG_FILE="${1#*=}"
      shift
      ;;
    -h | --help)
      show_help
      exit 0
      ;;
    -*)
      log_error "Unknown option: $1"
      show_help
      exit 1
      ;;
    *)
      # If it doesn't start with -, it's a positional argument
      positional_args+=("$1")
      shift
      ;;
    esac
  done

  # Process positional arguments
  if [[ ${#positional_args[@]} -eq 1 ]]; then
    local arg="${positional_args[0]}"
    # Check if it's a test type (unit, integration, performance)
    if [[ "$arg" =~ ^(unit|integration|performance)$ ]]; then
      # Single test type - run all tests of that type
      TEST_TYPE="$arg"
      # Clear any default test type since we have a specific one
      SINGLE_TEST=""
    else
      # Single argument - it's a test name
      SINGLE_TEST="$arg"
    fi
  elif [[ ${#positional_args[@]} -ge 2 ]]; then
    # Two or more arguments - check if first is a test type or a test binary name
    local first_arg="${positional_args[0]}"

    # Check if first argument looks like a test binary name (starts with test_ or contains test)
    if [[ "$first_arg" =~ ^test_ ]] || [[ "$first_arg" =~ test ]]; then
      # All arguments are test binary names - run them all
      MULTIPLE_TESTS=()
      for arg in "${positional_args[@]}"; do
        MULTIPLE_TESTS+=("$arg")
      done
      MULTIPLE_TEST_MODE=1
    elif [[ "$first_arg" =~ ^(unit|integration|performance)$ ]]; then
      # First arg is a test type, rest are test names within that type
      local type_arg="$first_arg"
      if [[ ${#positional_args[@]} -eq 2 ]]; then
        # Two arguments: type and single test name
        local name_arg="${positional_args[1]}"
        SINGLE_TEST="test_${type_arg}_${name_arg}"
      else
        # Three or more: type and multiple test names
        MULTIPLE_TESTS=()
        for ((i = 1; i < ${#positional_args[@]}; i++)); do
          local test_name="${positional_args[i]}"
          # Construct the full test name
          MULTIPLE_TESTS+=("test_${type_arg}_${test_name}")
        done
        MULTIPLE_TEST_MODE=1
      fi
    else
      # Not a test type and doesn't look like a test binary - assume all are test names
      MULTIPLE_TESTS=()
      for arg in "${positional_args[@]}"; do
        MULTIPLE_TESTS+=("$arg")
      done
      MULTIPLE_TEST_MODE=1
    fi
  fi

  # Validate arguments
  if [[ ! "$TEST_TYPE" =~ ^(all|unit|integration|performance)$ ]]; then
    log_error "Invalid test type: $TEST_TYPE"
    exit 1
  fi

  if [[ ! "$BUILD_TYPE" =~ ^(debug|dev|release|coverage|tsan)$ ]]; then
    log_error "Invalid build type: $BUILD_TYPE"
    exit 1
  fi

  # Validate filter usage
  if [[ -n "$FILTER" && -z "$SINGLE_TEST" ]]; then
    log_error "--filter option can only be used when running a single test binary"
    exit 1
  fi

  # Override build type if coverage is requested
  if [[ -n "$COVERAGE" ]]; then
    case "$BUILD_TYPE" in
    debug)
      BUILD_TYPE="coverage"
      ;;
    release)
      BUILD_TYPE="release"
      ;;
    esac
    log_info "📊 Coverage requested, using build type: $BUILD_TYPE"
  fi

  # Detect CPU cores
  local jobs
  if [[ -z "$JOBS" ]]; then
    # No jobs specified, let the test functions handle automatic allocation
    jobs=""
    log_info "⚡ Using automatic resource allocation"
  else
    jobs="$JOBS"
    log_info "⚡ Using $jobs parallel jobs"
  fi

  # Setup logging
  local log_file
  if [[ -n "$LOG_FILE" ]]; then
    log_file="$LOG_FILE"
  else
    log_file="tests.log"
  fi

  local junit_file="$PROJECT_ROOT/junit.xml"

  if [[ -n "$GENERATE_JUNIT" ]]; then
    log_info "📄 JUnit XML output will be generated: $junit_file"
    rm -f "$junit_file"
  fi

  # Always set up logging (not just for JUnit)
  log_info "📝 Test logs will be saved to: $log_file"
  >"$log_file"

  # Change to project root
  cd "$PROJECT_ROOT"

  # Determine which test categories to run
  local categories_to_run=()
  if [[ "$TEST_TYPE" == "all" ]]; then
    categories_to_run=("${TEST_CATEGORIES[@]}")
  else
    categories_to_run=("$TEST_TYPE")
  fi

  # Determine all tests to run upfront
  local all_tests_to_run=()
  local overall_start_time=$(date +%s.%N)

  # Use centralized build directory detection for single and multiple test modes
  local cmake_build_dir=$(get_build_directory)
  local bin_dir="$PROJECT_ROOT/$cmake_build_dir/bin"

  if [[ -n "$SINGLE_TEST" ]]; then
    # Single test mode
    # Queue test to be built - don't check if it exists yet
    all_tests_to_run=("$bin_dir/$SINGLE_TEST")
  elif [[ -n "$MULTIPLE_TEST_MODE" ]]; then
    # Multiple specific tests mode
    # Queue tests to be built - don't check if they exist yet
    for test in "${MULTIPLE_TESTS[@]}"; do
      all_tests_to_run+=("$bin_dir/$test")
    done
  else
    # Category mode - determine all categories to run
    local categories_to_run=()
    # If we have a specific TEST_TYPE (not "all"), use that instead of TEST_CATEGORIES
    if [[ -n "$TEST_TYPE" && "$TEST_TYPE" != "all" ]]; then
      categories_to_run=("$TEST_TYPE")
    elif [[ ${#TEST_CATEGORIES[@]} -eq 0 ]]; then
      # No categories specified via command line, run all
      categories_to_run=("unit" "integration" "performance")
    else
      # Use specified categories
      categories_to_run=("${TEST_CATEGORIES[@]}")
    fi

    # Get all test executables from all categories
    for category in "${categories_to_run[@]}"; do
      local category_tests=($(get_test_executables "$category" "$BUILD_TYPE"))
      if [[ ${#category_tests[@]} -gt 0 ]]; then
        all_tests_to_run+=("${category_tests[@]}")
      fi
    done
  fi

  # Now we have all tests - build and run them
  if [[ ${#all_tests_to_run[@]} -eq 0 ]]; then
    log_error "No tests found to run!"
    exit 1
  fi

  log_info "🎯 Running ${#all_tests_to_run[@]} test(s)"

  # Initialize JUnit XML
    if [[ -n "$GENERATE_JUNIT" ]]; then
      echo '<?xml version="1.0" encoding="UTF-8"?>' >"$junit_file"
    echo "<testsuites name=\"ascii-chat Tests\">" >>"$junit_file"
    fi

  # Ensure build directory is configured
  ensure_tests_built "$BUILD_TYPE" "$TEST_TYPE"

  # Build all test executables in one parallel ninja command
  # Extract just the test target names from full paths
  local test_targets=()
  for test_path in "${all_tests_to_run[@]}"; do
    echo $test_path
    local test_name=$(basename "$test_path")
    test_targets+=("$test_name")
  done

  # cmake_build_dir already set above using get_build_directory()

  if [[ ${#test_targets[@]} -gt 0 ]]; then
    # Build all test targets in one command - ninja will parallelize and handle dependencies
    # Use ninja directly instead of cmake --build for faster execution
    local build_output
    local build_exit_code
    build_output=$(cd "$cmake_build_dir" && ninja "${test_targets[@]}" 2>&1)
    build_exit_code=$?

    # Only show build message and output if ninja actually did work
    if [[ "$build_output" != *"ninja: no work to do"* ]]; then
      log_info "🔨 Building ${#test_targets[@]} test executable(s) in parallel with Ninja..."
      echo "$build_output"
    elif [[ -n "$VERBOSE" ]]; then
      log_verbose "All test executables are up to date (ninja: no work to do)"
    fi

    # Check if build failed
    if [[ $build_exit_code -ne 0 ]]; then
      log_error "Failed to build test executables"
      exit 1
    fi

    # Verify that all requested tests were actually built
    local missing_tests=()
    for test_path in "${all_tests_to_run[@]}"; do
      if [[ ! -f "$test_path" ]]; then
        missing_tests+=("$(basename "$test_path")")
      fi
    done

    if [[ ${#missing_tests[@]} -gt 0 ]]; then
      log_error "The following test(s) failed to build or do not exist:"
      for missing_test in "${missing_tests[@]}"; do
        log_error "  • $missing_test"
      done
      log_error ""
      log_error "Available tests in $cmake_build_dir/bin/:"
      ls -1 "$cmake_build_dir/bin/" | grep "^test_" || echo "  (none found)"
      exit 1
    fi
  fi

  # SINGLE DECISION POINT - Choose execution mode
  local total_cores=$(detect_cpu_cores)
  local num_tests=${#all_tests_to_run[@]}
  local max_parallel_tests=$(calculate_resource_allocation $num_tests $total_cores "$jobs")

  if [[ "$NO_PARALLEL" == "1" ]] || [[ ${#all_tests_to_run[@]} -eq 1 ]]; then
    # Run sequentially
    run_tests_sequential "${all_tests_to_run[@]}"
  else
    # Run in parallel
    run_tests_parallel "$max_parallel_tests" "${all_tests_to_run[@]}"
  fi

  # Results are now in global variables (no subshell issues!)
  local passed_tests=$TESTS_PASSED
  local failed_tests=$TESTS_FAILED
  local timedout_tests=$TESTS_TIMEDOUT
  local tests_that_started=$TESTS_STARTED

  # Close JUnit XML
        if [[ -n "$GENERATE_JUNIT" ]]; then
    echo '</testsuites>' >>"$junit_file"

    # Validate JUnit XML
    local xml_errors
    xml_errors=$(xmllint --noout "$junit_file" 2>&1)
    if [[ $? -ne 0 ]]; then
      log_error "❌ JUnit XML validation failed!"
      log_error "Invalid XML in: $junit_file"
      log_error "XML errors: $xml_errors"
      log_error "XML content:"
      cat "$junit_file" >&2
      log_error "Exiting due to invalid JUnit XML"
      exit 1
    else
      log_info "✅ JUnit XML validation passed"
    fi
  fi

  # Report results
        echo ""
        echo "=========================================="
  if [[ $failed_tests -eq 0 && $timedout_tests -eq 0 ]]; then
    echo "✅ Tests completed: $passed_tests passed, $failed_tests failed, $timedout_tests timed out"
      overall_failed=0
    else
    echo "❌ Tests completed: $passed_tests passed, $failed_tests failed, $timedout_tests timed out"
      overall_failed=1
    fi
  echo "=========================================="

  # Final exit logic
  local overall_end_time=$(date +%s.%N)
  local total_duration
  if command -v bc >/dev/null 2>&1; then
    total_duration=$(echo "$overall_end_time - $overall_start_time" | bc -l)
  else
    # Fallback: use awk for basic arithmetic if bc is not available
    total_duration=$(awk "BEGIN {printf \"%.3f\", $overall_end_time - $overall_start_time}")
  fi

  echo ""
  echo "=========================================="
  echo "⏱️ TOTAL EXECUTION TIME: ${total_duration}s"
  echo "=========================================="

  if [[ $overall_failed -eq 0 ]]; then
    log_success "All tests completed successfully! 🎉"
    log_info "📋 View test logs: cat $log_file"
  else
    log_error "Some tests failed!"
    if [[ ${#FAILED_TEST_NAMES[@]} -gt 0 ]]; then
      echo ""
      echo "❌ FAILED TESTS:"
      for failed_test in "${FAILED_TEST_NAMES[@]}"; do
        echo -e "   • \033[31m$failed_test\033[0m"
      done
    fi
    if [[ ${#TIMEDOUT_TEST_NAMES[@]} -gt 0 ]]; then
      echo ""
      echo "⏱️ TIMED OUT TESTS:"
      for timedout_test in "${TIMEDOUT_TEST_NAMES[@]}"; do
        echo -e "   • \033[33m$timedout_test\033[0m"
      done
    fi
    echo ""
    log_info "View test logs: cat $log_file 📋"
    exit 1
  fi
}

# =============================================================================
# Script Entry Point
# =============================================================================

# Only run main if script is executed directly (not sourced)
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  main "$@"
fi
