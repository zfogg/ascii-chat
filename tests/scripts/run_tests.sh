#!/bin/bash

# =============================================================================
# ASCII-Chat Test Runner Script
# =============================================================================
# This script unifies all the common test running patterns from the Makefile
# into a single, reusable script that can be called from various contexts.

set -uo pipefail  # Remove 'e' flag so we can handle errors ourselves
set -m  # Enable job control for better signal handling

# =============================================================================
# Signal Handling
# =============================================================================

# Track if we've been interrupted
INTERRUPTED=0

# Check if wait -n is supported (bash 4.3+)
WAIT_N_SUPPORTED=0
# Check bash version - wait -n was added in bash 4.3
if [[ "${BASH_VERSINFO[0]}" -gt 4 ]] || ([[ "${BASH_VERSINFO[0]}" -eq 4 ]] && [[ "${BASH_VERSINFO[1]}" -ge 3 ]]); then
  WAIT_N_SUPPORTED=1
fi

# Cleanup function for Ctrl-C
function handle_interrupt() {
  # Disable the trap immediately to prevent re-entry
  trap - INT TERM HUP
  
  # Print cancellation message to the terminal directly (before redirecting output)
  echo "" >/dev/tty
  echo "" >/dev/tty
  echo "==========================================" >/dev/tty
  echo "❌ TESTS CANCELLED BY USER (Ctrl-C)" >/dev/tty
  echo "==========================================" >/dev/tty
  
  # Redirect all further output to /dev/null to stop printing
  exec 1>/dev/null 2>&1
  
  # Kill all child processes immediately without waiting
  # Use process group kill for instant termination
  kill -KILL -- -$$ 2>/dev/null || true
  
  # Also kill direct children and background jobs
  pkill -KILL -P $$ 2>/dev/null || true
  jobs -p | xargs -r kill -KILL 2>/dev/null || true
  
  # Exit immediately
  exit 130
}

# Set up signal handlers - use INT instead of SIGINT for better compatibility
trap handle_interrupt INT TERM HUP

# Enable job control for better process management
set -m

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
PARALLEL_TESTS="1"  # Default to parallel execution
NO_PARALLEL=""

# Test categories
TEST_CATEGORIES=("unit" "integration" "performance")

# =============================================================================
# Helper Functions
# =============================================================================

function show_help() {
  cat <<EOF
ASCII-Chat Test Runner

Usage: $0 [OPTIONS] [TEST_NAME]

OPTIONS:
    -t, --type TYPE         Test type: all, unit, integration, performance (default: all)
    -b, --build TYPE        Build type: debug, release, coverage, sanitize (default: debug)
    -j, --jobs N            Number of parallel test executables to run (default: auto-detect CPU cores)
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
    $0 -b sanitize                        # Run with AddressSanitizer for memory checking
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
    debug                 - Debug build with symbols, no optimization
    release               - Release build with optimizations and LTO
    coverage              - Debug build with coverage instrumentation
    sanitize              - Debug build with AddressSanitizer for memory checking

TEST TYPES:
    all                   - Run all test categories
    unit                  - Run only unit tests
    integration           - Run only integration tests
    performance           - Run only performance tests

EOF
}

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

# Wrapper for make command with optional grc colorization
function colored_make() {
  if command -v grc >/dev/null 2>&1; then
    grc --colour=on make "$@"
  else
    make "$@"
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

# Get test executables for a specific category
function get_test_executables() {
  local category="$1"
  local build_type="${2:-debug}"  # Optional build type parameter
  local bin_dir="$PROJECT_ROOT/bin"

  # Check if bin directory exists
  if [[ ! -d "$bin_dir" ]]; then
    log_verbose "bin directory not found at: $bin_dir"
    return
  fi

  # Tests always have the same name regardless of build type
  case "$category" in
  unit)
    # Use simple shell globbing instead of find
    for f in "$bin_dir"/test_unit_*; do
      [[ -f "$f" ]] && echo "$f"
    done | sort
    ;;
  integration)
    # Use simple shell globbing instead of find
    for f in "$bin_dir"/test_integration_*; do
      [[ -f "$f" ]] && echo "$f"
    done | sort
    ;;
  performance)
    # Use simple shell globbing instead of find
    for f in "$bin_dir"/test_performance_*; do
      [[ -f "$f" ]] && echo "$f"
    done | sort
    ;;
  all)
    # Use simple shell globbing instead of find
    for f in "$bin_dir"/test_*; do
      [[ -f "$f" ]] && echo "$f"
    done | sort
    ;;
  *)
    log_error "Unknown test category: $category"
    return 1
    ;;
  esac
}

# Build tests if they don't exist
function ensure_tests_built() {
  local build_type="$1"
  local test_type="$2"

  log_info "🔨 Ensuring tests are built with $build_type configuration..."

  cd "$PROJECT_ROOT"

  case "$build_type" in
  debug)
    colored_make tests-debug
    ;;
  release)
    colored_make tests-release
    ;;
  coverage)
    colored_make tests-coverage
    ;;
  sanitize)
    colored_make sanitize
    colored_make tests
    ;;
  *)
    log_error "Unknown build type: $build_type"
    return 1
    ;;
  esac
}

function clean_verbose_test_output() {
  tail -n+2 | grep --color=always -v -E '\[.*SKIP.*\]' | grep --color=always -v -E '\[.*RUN.*\]'
}

function exec_test_executable() {
  local test_executable=""
  local jobs=""
  local generate_junit=""
  local log_file=""
  local junit_file=""
  local filter=""
  local xml_file=""

  # Parse flags
  while [[ $# -gt 0 ]]; do
    case $1 in
      --executable)
        test_executable="$2"
        shift 2
        ;;
      --jobs)
        jobs="$2"
        shift 2
        ;;
      --generate-junit)
        generate_junit="$2"
        shift 2
        ;;
      --log-file)
        log_file="$2"
        shift 2
        ;;
      --junit-file)
        junit_file="$2"
        shift 2
        ;;
      --xml)
        xml_file="$2"
        shift 2
        ;;
      --filter)
        filter="$2"
        shift 2
        ;;
      *)
        echo "Unknown option: $1" >&2
        return 1
        ;;
    esac
  done

  # Build command line arguments
  local jobs_flag=""
  if [[ -n "$jobs" && "$jobs" != "1" ]]; then
    jobs_flag="--jobs=$jobs"
  fi

  local xml_flag=""
  if [[ -n "$xml_file" ]]; then
    xml_flag="--xml=$xml_file"
  fi

  local verbose_flag=""
  if [[ -n "$VERBOSE" ]]; then
    verbose_flag="--verbose"
  fi

  local color_flag="--color=always"

  # Add filter flag if specified (for Criterion tests)
  local filter_flag=""
  if [[ -n "$filter" ]]; then
    filter_flag="--filter=$filter"
  fi

  # Run the test with TESTING=1 environment variable
  local test_exit_code
  
  # Check for interrupt before starting
  if [[ $INTERRUPTED -eq 1 ]]; then
    return 130
  fi
  
  if [[ -n "$log_file" ]]; then
    # Run test in foreground for proper signal handling
    env TESTING=1 "$test_executable" $jobs_flag $xml_flag $verbose_flag $color_flag $filter_flag 2>&1 | tee -a "$log_file"
    test_exit_code=${PIPESTATUS[0]}
  else
    env TESTING=1 "$test_executable" $jobs_flag $xml_flag $verbose_flag $color_flag $filter_flag 2>&1
    test_exit_code=$?
  fi
  
  # If test was killed by signal, return immediately
  if [[ $test_exit_code -eq 130 ]] || [[ $test_exit_code -gt 128 ]]; then
    INTERRUPTED=1
    return $test_exit_code
  fi
  
  # Check if we were interrupted
  if [[ $INTERRUPTED -eq 1 ]]; then
    return 130
  fi
  
  return $test_exit_code
}

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
          echo "<testsuite name=\"$test_class\" tests=\"1\" failures=\"0\" errors=\"0\" time=\"${duration}.0\">" >>"$junit_file"
          echo "  <testcase classname=\"$test_class\" name=\"all\" time=\"${duration}.0\"/>" >>"$junit_file"
          echo "</testsuite>" >>"$junit_file"
        fi
        rm -f "$xml_file"
      else
        # Test passed but no XML generated - create a minimal entry
        echo "<testsuite name=\"$test_class\" tests=\"1\" failures=\"0\" errors=\"0\" time=\"${duration}.0\">" >>"$junit_file"
        echo "  <testcase classname=\"$test_class\" name=\"all\" time=\"${duration}.0\"/>" >>"$junit_file"
        echo "</testsuite>" >>"$junit_file"
      fi
      rm -f "$output_file"
    else
    # Test failed - determine failure type and generate appropriate XML
      local failure_type="TestFailure"
      local failure_msg="Test failed with exit code $test_exit_code"

      if [[ $test_exit_code -eq 124 ]]; then
        failure_type="Timeout"
        failure_msg="Test timed out after 300 seconds"
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

        echo "<testsuite name=\"$test_class\" tests=\"1\" failures=\"$failure_count\" errors=\"$error_count\" time=\"${duration}.0\">" >>"$junit_file"
        echo "  <testcase classname=\"$test_class\" name=\"all\" time=\"${duration}.0\">" >>"$junit_file"

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
      failure_msg="Test timed out after 300 seconds"
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

# Run a single test with proper error handling
function run_single_test() {
  local test_executable=""
  local jobs=""
  local generate_junit=""
  local log_file=""
  local junit_file=""
  local filter=""

  # Parse flags
  while [[ $# -gt 0 ]]; do
    case $1 in
      --executable)
        test_executable="$2"
        shift 2
        ;;
      --jobs)
        jobs="$2"
        shift 2
        ;;
      --generate-junit)
        generate_junit="$2"
        shift 2
        ;;
      --log-file)
        log_file="$2"
        shift 2
        ;;
      --junit-file)
        junit_file="$2"
        shift 2
        ;;
      --filter)
        filter="$2"
        shift 2
        ;;
      *)
        echo "Unknown option: $1" >&2
        return 1
        ;;
    esac
  done

  # Ensure we have the full path
  if [[ ! "$test_executable" = /* ]]; then
    test_executable="$PROJECT_ROOT/bin/$test_executable"
  fi
  local test_name="$(basename "$test_executable")"

  # Enable colored output for Criterion tests
  export TERM=${TERM:-xterm-256color}
  export CLICOLOR=1
  export CLICOLOR_FORCE=1
  export CRITERION_USE_COLORS=always
  export TESTING=1

  # Check if already interrupted before starting
  if [[ $INTERRUPTED -eq 1 ]]; then
    return 130
  fi
  
  echo "🚀 [TEST] Starting: $test_name"
  local test_start_time=$(date +%s.%N)

  if [[ -n "$generate_junit" ]]; then
    # Generate JUnit XML for this test
    local xml_file="/tmp/${test_name}_$(date +%s%N).xml"
    local test_class="$(echo "$test_name" | sed 's/^test_//; s/_/./g')"
    local output_file="/tmp/${test_name}_output_$(date +%s%N).txt"

    # Run test with timeout and capture output
    local test_exit_code=0
    # Run test with optional timeout
    # Use gtimeout on macOS (GNU coreutils), timeout on Linux
    local timeout_cmd=""
    if command -v gtimeout >/dev/null 2>&1; then
      # macOS with GNU coreutils installed
      timeout_cmd="gtimeout --preserve-status 300"
    elif command -v timeout >/dev/null 2>&1; then
      # Check if it's GNU timeout with --preserve-status support
      if timeout --version 2>/dev/null | grep -q "GNU\|coreutils"; then
        timeout_cmd="timeout --preserve-status 300"
      else
        # BSD timeout
        timeout_cmd="timeout 300"
      fi
    fi

    if [[ -n "$timeout_cmd" ]]; then
      # Export function so timeout can use it via bash -c
      export -f exec_test_executable
      export VERBOSE
      export TESTING
      export INTERRUPTED
      $timeout_cmd bash -c "exec_test_executable --executable '$test_executable' --xml '$xml_file' --filter '$filter' --log-file '$log_file'" | tee "$output_file"
    else
      # No timeout available, run directly
      exec_test_executable --executable "$test_executable" --xml "$xml_file" --filter "$filter" --log-file "$log_file" | tee "$output_file"
    fi
    test_exit_code=$?

    # Output to log file
    cat "$output_file" >> "$log_file"
  else
    # Not generating JUnit, but still log output
    exec_test_executable --executable "$test_executable" --filter "$filter" --log-file "$log_file"
    test_exit_code=$?
  fi

  # Check if we were interrupted during test execution
  if [[ $INTERRUPTED -eq 1 ]]; then
    return 130
  fi

  local test_end_time=$(date +%s.%N)
      local duration=$(echo "$test_end_time - $test_start_time" | bc -l)

  if [[ -n "$generate_junit" ]]; then
    # Generate JUnit XML
    generate_junit_xml --test-name "$test_name" --test-class "$test_class" --duration "$duration" --exit-code "$test_exit_code" --xml-file "$xml_file" --output-file "$output_file" --junit-file "$junit_file"
  fi

  # Determine and report test result
  determine_test_failure --exit-code "$test_exit_code" --test-name "$test_name" --duration "$duration"

  return $test_exit_code
}

# Run tests for a specific category
function run_test_category() {
  local category="$1"
  local build_type="$2"
  local jobs="$3"
  local generate_junit="$4"
  local log_file="$5"
  local junit_file="$6"

  # Set up trap to close XML properly on exit
  if [[ -n "$generate_junit" ]]; then
    trap "echo '</testsuites>' >> '$junit_file' 2>/dev/null || true" EXIT INT TERM
  fi

  echo ""
  echo "=========================================="
  echo "🔧 Starting $category tests..."
  echo "=========================================="
  local category_start_time=$(date +%s.%N)

  # First, determine what test executables should exist based on source files
  local test_names_to_build=()
  log_verbose "Looking for test source files in $PROJECT_ROOT/tests/$category/"
  
  case "$category" in
    unit)
      # Find all unit test source files and convert to executable names
      for f in "$PROJECT_ROOT/tests/unit/"*_test.c; do
        if [[ -f "$f" ]]; then
          local basename=$(basename "$f" .c)
          local test_name="test_unit_${basename%_test}"
          test_names_to_build+=("$test_name")
        fi
      done
      ;;
    integration)
      # Find all integration test source files and convert to executable names
      for f in "$PROJECT_ROOT/tests/integration/"*_test.c; do
        if [[ -f "$f" ]]; then
          local basename=$(basename "$f" .c)
          local test_name="test_integration_${basename%_test}"
          test_names_to_build+=("$test_name")
        fi
      done
      ;;
    performance)
      # Find all performance test source files and convert to executable names
      for f in "$PROJECT_ROOT/tests/performance/"*_test.c; do
        if [[ -f "$f" ]]; then
          local basename=$(basename "$f" .c)
          local test_name="test_performance_${basename%_test}"
          test_names_to_build+=("$test_name")
        fi
      done
      ;;
  esac
  
  if [[ ${#test_names_to_build[@]} -eq 0 ]]; then
    log_error "No $category test source files found in $PROJECT_ROOT/tests/$category/"
    return 1
  fi
  
  # Always build all tests for this category (make will skip if already built)
  log_info "🔨 Building ${#test_names_to_build[@]} $category test(s) in $build_type mode..."
  
  # Add bin/ prefix to all test names
  local make_targets=()
  for test_name in "${test_names_to_build[@]}"; do
    make_targets+=("bin/$test_name")
  done
  
  # Build all test executables at once with the specified build type
  # Filter out "up to date" messages
  if colored_make -C "$PROJECT_ROOT" BUILD_TYPE="$build_type" -j$(detect_cpu_cores) "${make_targets[@]}" 2>&1 | grep -v "is up to date" | grep -v "Nothing to be done"; then
    # Check if make actually succeeded (grep returns 1 if no lines match, which could mean all were filtered)
    if colored_make -C "$PROJECT_ROOT" BUILD_TYPE="$build_type" -j$(detect_cpu_cores) "${make_targets[@]}" >/dev/null 2>&1; then
      local make_exit_code=0
    else
      local make_exit_code=1
    fi
  else
    local make_exit_code=${PIPESTATUS[0]}
  fi
  
  if [[ $make_exit_code -ne 0 ]]; then
    log_error "Failed to build $category tests"
    return 1
  else
    log_success "Successfully built $category tests"
  fi
  
  # Now get the list of built executables
  log_verbose "Getting list of test executables for category: $category, build_type: $build_type"
  local test_executables=($(get_test_executables "$category" "$build_type"))
  log_verbose "Found test executables array with ${#test_executables[@]} elements"
  
  if [[ ${#test_executables[@]} -eq 0 ]]; then
    log_error "No $category tests found after building 😕"
    log_error "Contents of bin directory:"
    ls -la "$PROJECT_ROOT/bin/" 2>/dev/null || echo "bin/ directory does not exist"
    log_error "Trying to debug get_test_executables directly:"
    get_test_executables "$category" "$build_type" | head -5
    return 1
  fi

  local failed=0
  local total_tests=${#test_executables[@]}
  local passed_tests=0
  local failed_tests=0
  local tests_that_started=0

  log_info "📦 Found $total_tests $category test(s)"

  if [[ -n "$generate_junit" ]]; then
    # Initialize JUnit XML
    echo '<?xml version="1.0" encoding="UTF-8"?>' >"$junit_file"
    echo "<testsuites name=\"ASCII-Chat $category Tests\">" >>"$junit_file"
  fi

  # Run each test
  # Determine if we should run tests in parallel
  local actual_jobs="$jobs"
  local parallel_execution=0
  
  # Enable parallel execution by default unless explicitly disabled
  if [[ -z "$NO_PARALLEL" ]]; then
    parallel_execution=1
  else
    log_verbose "Running tests sequentially (parallel execution disabled)"
  fi

  # Arrays to track parallel test execution
  local test_pids=()
  local test_names=()
  local test_logs=()
  local test_junit_files=()  # Track individual JUnit XML files
  local test_start_times=()  # Track test start times for duration calculation
  
  # Use jobs parameter for max parallel tests, default to CPU cores
  local max_parallel_tests=$jobs
  if [[ -z "$max_parallel_tests" ]] || [[ "$max_parallel_tests" -lt 1 ]]; then
    max_parallel_tests=$(detect_cpu_cores)
  fi

  # Function to format duration nicely
  format_duration() {
    local duration=$1
    # Convert to milliseconds
    local ms=$(echo "$duration * 1000" | bc -l | cut -d. -f1)
    
    if [[ $ms -lt 1000 ]]; then
      echo "${ms}ms"
    elif [[ $ms -lt 60000 ]]; then
      # Show in seconds with 2 decimal places
      printf "%.2fs" $(echo "scale=2; $duration" | bc -l)
    else
      # Show in minutes and seconds
      local mins=$((ms / 60000))
      local secs=$(echo "scale=2; ($ms % 60000) / 1000" | bc -l)
      printf "%dm%.2fs" $mins $secs
    fi
  }
  
  # Function to wait for a test to complete
  wait_for_test() {
    local pid=$1
    local test_name=$2
    local test_log=$3
    local test_junit=$4
    local start_time=$5
    
    if wait $pid 2>/dev/null; then
      local exit_code=$?
      local end_time=$(date +%s.%N)
      local duration=$(echo "$end_time - $start_time" | bc -l)
      local formatted_time=$(format_duration "$duration")
      
      if [[ $exit_code -eq 0 ]]; then
        ((passed_tests++))
        echo -e "✅ [TEST] \033[32mPASSED\033[0m: $(basename "$test_name") ($formatted_time)"
      else
        ((failed_tests++))
        failed=1
        echo -e "❌ [TEST] \033[31mFAILED\033[0m: $(basename "$test_name") (exit code: $exit_code, $formatted_time)"
      fi
    else
      # Process was killed or interrupted
      ((failed_tests++))
      failed=1
      echo -e "🛑 [TEST] \033[31mCANCELLED\033[0m: $(basename "$test_name")"
    fi
    
    # Append test log to main log file
    if [[ -f "$test_log" ]]; then
      cat "$test_log" >> "$log_file"
      rm -f "$test_log"
    fi
    
    # Append JUnit XML to main JUnit file (extract testsuite elements)
    if [[ -n "$generate_junit" ]] && [[ -f "$test_junit" ]]; then
      # Extract just the testsuite element(s) and append to main file
      sed -n '/<testsuite/,/<\/testsuite>/p' "$test_junit" >> "$junit_file" 2>/dev/null || true
      rm -f "$test_junit"
    fi
  }

  # Function to kill all running test processes
  kill_all_test_processes() {
    local pids_to_kill=("$@")
    if [[ ${#pids_to_kill[@]} -gt 0 ]]; then
      # When interrupted, immediately kill with SIGKILL to stop output
      if [[ $INTERRUPTED -eq 1 ]]; then
        # Immediate SIGKILL in parallel - no graceful termination
        for pid in "${pids_to_kill[@]}"; do
          (
            pkill -KILL -P $pid 2>/dev/null || true
            kill -KILL $pid 2>/dev/null || true
          ) &
        done
        wait
      else
        # Normal termination - try graceful first
        # Send SIGTERM for all processes in parallel
        for pid in "${pids_to_kill[@]}"; do
          if kill -0 $pid 2>/dev/null; then
            (
              pkill -TERM -P $pid 2>/dev/null || true
              kill -TERM $pid 2>/dev/null || true
            ) &
          fi
        done
        
        # Wait for background SIGTERM sends to complete
        wait
        
        # Give all processes 1 second to terminate gracefully
        timeout 1 bash -c '
          pids=($@)
          for pid in "${pids[@]}"; do
            while kill -0 $pid 2>/dev/null; do
              sleep 0.05
              break
            done
          done
        ' -- "${pids_to_kill[@]}" 2>/dev/null || true
        
        # Force kill any remaining processes with SIGKILL in parallel
        for pid in "${pids_to_kill[@]}"; do
          if kill -0 $pid 2>/dev/null; then
            (
              pkill -KILL -P $pid 2>/dev/null || true
              kill -KILL $pid 2>/dev/null || true
            ) &
          fi
        done
        
        # Wait for all SIGKILL operations to complete
        wait
      fi
    fi
  }

  if [[ $parallel_execution -eq 1 ]]; then
    log_info "🚀 Running tests in parallel (up to $max_parallel_tests concurrent tests)"
    
    # Parallel execution mode
    for test_executable in "${test_executables[@]}"; do
      # Check if we've been interrupted
      if [[ $INTERRUPTED -eq 1 ]]; then
        break
      fi
      
      ((tests_that_started++))
      local test_name="$(basename "$test_executable")"
      local test_log="/tmp/test_${test_name}_$$.log"
      local test_junit=""
      local start_time=$(date +%s.%N)
      
      # Create separate JUnit file for this test if needed
      if [[ -n "$generate_junit" ]]; then
        test_junit="/tmp/junit_${test_name}_$$.xml"
      fi
      
      # Print starting message
      echo "🚀 [TEST] Starting: $test_name"
      log_verbose "Starting test $tests_that_started of $total_tests: $test_name (parallel)"
      
      # Start test in background with proper signal handling and output redirection
      if [[ -n "$generate_junit" ]]; then
        (
          trap 'exit 130' INT TERM
          # Check if interrupted before starting
          [[ $INTERRUPTED -eq 1 ]] && exit 130
          run_single_test --executable "$test_executable" --jobs "$actual_jobs" \
            --generate-junit "$generate_junit" --log-file "$test_log" --junit-file "$test_junit"
        ) >>"$test_log" 2>&1 &
      else
        (
          trap 'exit 130' INT TERM
          # Check if interrupted before starting
          [[ $INTERRUPTED -eq 1 ]] && exit 130
          run_single_test --executable "$test_executable" --jobs "$actual_jobs" \
            --generate-junit "" --log-file "$test_log" --junit-file ""
        ) >>"$test_log" 2>&1 &
      fi
      
      local test_pid=$!
      test_pids+=($test_pid)
      test_names+=("$test_executable")
      test_logs+=("$test_log")
      test_junit_files+=("$test_junit")
      test_start_times+=("$start_time")
      
      # If we've reached max parallel tests, wait for one to complete
      if [[ ${#test_pids[@]} -ge $max_parallel_tests ]]; then
        # Wait for any child process to complete
        if [[ $WAIT_N_SUPPORTED -eq 1 ]]; then
          wait -n
          local wait_result=$?
        else
          # Fallback for older bash - wait for first PID to complete
          wait ${test_pids[0]}
          local wait_result=$?
        fi
        
        # Check for interrupt
        if [[ $INTERRUPTED -eq 1 ]]; then
          [[ ${#test_pids[@]} -gt 0 ]] && kill_all_test_processes "${test_pids[@]}"
          break
        fi
        
        # Find which PID completed
        local completed_idx=""
        for idx in "${!test_pids[@]}"; do
          if ! kill -0 ${test_pids[$idx]} 2>/dev/null; then
            completed_idx=$idx
            break
          fi
        done
        
        if [[ -n "$completed_idx" ]]; then
          wait_for_test "${test_pids[$completed_idx]}" "${test_names[$completed_idx]}" "${test_logs[$completed_idx]}" "${test_junit_files[$completed_idx]}" "${test_start_times[$completed_idx]}"
          
          # Remove completed test from arrays
          unset test_pids[$completed_idx]
          unset test_names[$completed_idx]
          unset test_logs[$completed_idx]
          unset test_junit_files[$completed_idx]
          unset test_start_times[$completed_idx]
          
          # Rebuild arrays to remove gaps (handle empty arrays)
          test_pids=(${test_pids[@]+"${test_pids[@]}"})
          test_names=(${test_names[@]+"${test_names[@]}"})
          test_logs=(${test_logs[@]+"${test_logs[@]}"})
          test_junit_files=(${test_junit_files[@]+"${test_junit_files[@]}"})
          test_start_times=(${test_start_times[@]+"${test_start_times[@]}"})
        fi
      fi
    done
    
    # If interrupted, kill all remaining test processes
    if [[ $INTERRUPTED -eq 1 ]]; then
      echo ""
      echo "🛑 Killing remaining test processes..."
      [[ ${#test_pids[@]} -gt 0 ]] && kill_all_test_processes "${test_pids[@]}"
    else
      # Wait for all remaining tests to complete
      while [[ ${#test_pids[@]} -gt 0 ]]; do
        # Wait for any remaining process to complete
        if [[ $WAIT_N_SUPPORTED -eq 1 ]]; then
          wait -n
        else
          # Fallback - wait for first PID in array
          if [[ ${#test_pids[@]} -gt 0 ]]; then
            wait ${test_pids[0]}
          fi
        fi
        
        # Check for interrupt
        if [[ $INTERRUPTED -eq 1 ]]; then
          [[ ${#test_pids[@]} -gt 0 ]] && kill_all_test_processes "${test_pids[@]}"
          break
        fi
        
        # Find which PID completed
        local completed_idx=""
        for idx in "${!test_pids[@]}"; do
          if ! kill -0 ${test_pids[$idx]} 2>/dev/null; then
            completed_idx=$idx
            break
          fi
        done
        
        if [[ -n "$completed_idx" ]]; then
          wait_for_test "${test_pids[$completed_idx]}" "${test_names[$completed_idx]}" "${test_logs[$completed_idx]}" "${test_junit_files[$completed_idx]}" "${test_start_times[$completed_idx]}"
          
          # Remove completed test from arrays
          unset test_pids[$completed_idx]
          unset test_names[$completed_idx]
          unset test_logs[$completed_idx]
          unset test_junit_files[$completed_idx]
          unset test_start_times[$completed_idx]
          
          # Rebuild arrays to remove gaps (handle empty arrays)
          test_pids=(${test_pids[@]+"${test_pids[@]}"})
          test_names=(${test_names[@]+"${test_names[@]}"})
          test_logs=(${test_logs[@]+"${test_logs[@]}"})
          test_junit_files=(${test_junit_files[@]+"${test_junit_files[@]}"})
          test_start_times=(${test_start_times[@]+"${test_start_times[@]}"})
        fi
      done
    fi
  else
    # Sequential execution mode (original code)
    for test_executable in "${test_executables[@]}"; do
      # Check if we've been interrupted
      if [[ $INTERRUPTED -eq 1 ]]; then
        break
      fi
      
      ((tests_that_started++))
      log_verbose "Running test $tests_that_started of $total_tests: $(basename "$test_executable")"
      if run_single_test --executable "$test_executable" --jobs "$actual_jobs" --generate-junit "$generate_junit" --log-file "$log_file" --junit-file "$junit_file"; then
        ((passed_tests++))
      else
        local exit_code=$?
        
        # Check if interrupted by Ctrl-C
        if [[ $exit_code -eq 130 ]]; then
          echo ""
          echo "🛑 Tests cancelled by user (Ctrl-C)"
          INTERRUPTED=1
          break
        fi
        
        failed=1
        # Continue running other tests even if one fails/crashes
        log_warning "Test failed with exit code $exit_code, continuing with remaining tests..."
      fi
    done
  fi

  if [[ -n "$generate_junit" ]]; then
    # Remove trap and close XML properly
    trap - EXIT INT TERM
    echo '</testsuites>' >>"$junit_file"
  fi

  # Report results
  local category_end_time=$(date +%s.%N)
  local category_duration=$(echo "$category_end_time - $category_start_time" | bc -l)
  
  # Calculate failed tests properly (in parallel mode, failed_tests is already set)
  if [[ $parallel_execution -eq 0 ]]; then
    failed_tests=$((tests_that_started - passed_tests))
  fi

  echo ""
  echo "=========================================="
  
  # Check if no tests actually ran
  if [[ $tests_that_started -eq 0 ]]; then
    echo "❌ CRITICAL: No $category tests were executed!"
    echo "=========================================="
    return 3  # Special return code for "no tests ran at all"
  elif [[ $passed_tests -eq 0 ]] && [[ $total_tests -gt 0 ]]; then
    echo "❌ All $category tests failed to run"
    echo "=========================================="
    return 2  # Special return code for "all tests failed to run"
  elif [[ $failed_tests -eq 0 ]]; then
    echo "✅ $category tests completed: $passed_tests passed, $failed_tests failed"
    echo "⏱️ $category execution time: ${category_duration}s"
  else
    echo "❌ $category tests completed: $passed_tests passed, $failed_tests failed"
    echo "⏱️ $category execution time: ${category_duration}s"
  fi
  echo "=========================================="

  if [[ $tests_that_started -eq 0 ]]; then
    log_error "CRITICAL: No tests were executed in $category category!"
    return 3  # No tests ran at all
  elif [[ $failed -eq 1 ]]; then
    if [[ $passed_tests -gt 0 ]]; then
      log_error "Some $category tests failed!"
      return 1  # Some tests failed but some passed
    else
      # All tests failed
      return 2  # All tests failed to run
    fi
  else
    log_success "All $category tests passed!"
    return 0  # All tests passed
  fi
}

# =============================================================================
# Build Test Executable
# =============================================================================

function build_test_executable() {
  local test_name="$1"
  local build_type="$2"
  
  # Always build the test executable (make will handle if it's up-to-date)
  local make_target="bin/$test_name"
  log_info "🔨 Building test: $make_target in $build_type mode"
  
  # Build the specific test executable (filter out "up to date" messages)
  if colored_make -C "$PROJECT_ROOT" BUILD_TYPE="$build_type" "$make_target" 2>&1 | grep -v "is up to date" | grep -v "Nothing to be done"; then
    log_success "Successfully built $make_target"
    return 0
  else
    # Check if make actually failed or just had nothing to do
    if colored_make -C "$PROJECT_ROOT" BUILD_TYPE="$build_type" "$make_target" >/dev/null 2>&1; then
      # Make succeeded but output was filtered - it was already up to date
      return 0
    else
      log_error "Failed to build test: $make_target"
      return 1
    fi
  fi
}

# =============================================================================
# Single Test Binary Execution
# =============================================================================

function run_single_test_binary() {
  local test_name="$1"
  local build_type="$2"
  local jobs="$3"
  local generate_junit="$4"
  local log_file="$5"
  local junit_file="$6"
  local filter="$7"

  log_info "🎯 Running single test binary: $test_name"

  # Determine the test executable path
  local test_executable
  if [[ "$test_name" == /* ]]; then
    # Absolute path
    test_executable="$test_name"
  elif [[ "$test_name" == bin/* ]]; then
    # Relative path starting with bin/
    test_executable="$PROJECT_ROOT/$test_name"
  else
    # Just the test name - handle various formats
    # If it doesn't start with test_, add it
    if [[ "$test_name" != test_* ]]; then
      test_name="test_$test_name"
    fi
    # Test executables always have the same name
    test_executable="$PROJECT_ROOT/bin/$test_name"
  fi

  # Build the test executable
  local test_binary_name="$(basename "$test_executable")"
  if ! build_test_executable "$test_binary_name" "$build_type"; then
    exit 1
  fi
  
  # Check if it exists after build
  if [[ ! -f "$test_executable" ]]; then
    log_error "Test executable not found after build: $test_executable"
    exit 1
  fi

  # Run the single test
  if run_single_test --executable "$test_executable" --jobs "$jobs" --generate-junit "$generate_junit" --log-file "$log_file" --junit-file "$junit_file" --filter "$filter"; then
    log_success "Test completed successfully!"
    return 0
  else
    local exit_code=$?
    log_error "Test failed with exit code $exit_code"
    return 1
  fi
}



# =============================================================================
# Main Function
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
  elif [[ ${#positional_args[@]} -eq 2 ]]; then
    # Two arguments - first is test type, second is test name
    local type_arg="${positional_args[0]}"
    local name_arg="${positional_args[1]}"
    
    # Check if first argument is a valid test type
    if [[ "$type_arg" =~ ^(unit|integration|performance)$ ]]; then
      # Construct the full test name for single test
      SINGLE_TEST="test_${type_arg}_${name_arg}"
    else
      log_error "Invalid test type: $type_arg (must be unit, integration, or performance)"
      exit 1
    fi
  elif [[ ${#positional_args[@]} -gt 2 ]]; then
    # Three or more arguments - first is test type, rest are test names
    local type_arg="${positional_args[0]}"
    
    # Check if first argument is a valid test type
    if [[ "$type_arg" =~ ^(unit|integration|performance)$ ]]; then
      # Multiple tests to run - store them in an array
      MULTIPLE_TESTS=()
      for ((i=1; i<${#positional_args[@]}; i++)); do
        local test_name="${positional_args[i]}"
        # Construct the full test name
        MULTIPLE_TESTS+=("test_${type_arg}_${test_name}")
      done
      # Set a flag to indicate we're running multiple specific tests
      MULTIPLE_TEST_MODE=1
    else
      log_error "Invalid test type: $type_arg (must be unit, integration, or performance)"
      exit 1
    fi
  fi

  # Validate arguments
  if [[ ! "$TEST_TYPE" =~ ^(all|unit|integration|performance)$ ]]; then
    log_error "Invalid test type: $TEST_TYPE"
    exit 1
  fi

  if [[ ! "$BUILD_TYPE" =~ ^(debug|release|coverage|sanitize)$ ]]; then
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
  jobs=$(detect_cpu_cores)
  log_info "⚡ Using $jobs parallel jobs"

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

  # Run tests
  local overall_failed=0
  local overall_start_time=$(date +%s.%N)

  if [[ -n "$SINGLE_TEST" ]]; then
    # Run single test binary
    if ! run_single_test_binary "$SINGLE_TEST" "$BUILD_TYPE" "$jobs" "$GENERATE_JUNIT" "$log_file" "$junit_file" "$FILTER"; then
      overall_failed=1
    fi
  elif [[ -n "$MULTIPLE_TEST_MODE" ]]; then
    # Run multiple specific test binaries
    log_info "🎯 Running ${#MULTIPLE_TESTS[@]} specific tests"
    
    if [[ -n "$GENERATE_JUNIT" ]]; then
      # Initialize JUnit XML for multiple tests
      echo '<?xml version="1.0" encoding="UTF-8"?>' >"$junit_file"
      echo "<testsuites name=\"ASCII-Chat Multiple Tests\">" >>"$junit_file"
    fi
    
    local passed_count=0
    local failed_count=0
    
    for test_name in "${MULTIPLE_TESTS[@]}"; do
      echo ""
      echo "=========================================="
      echo "🔧 Running: $test_name"
      echo "=========================================="
      
      # Check if test exists before trying to run it
      local test_path
      if [[ "$test_name" != test_* ]]; then
        test_name="test_$test_name"
      fi
      
      # Test executables always have the same name
      test_path="$PROJECT_ROOT/bin/$test_name"
      
      # Build the test executable
      if ! build_test_executable "$test_name" "$BUILD_TYPE"; then
        exit 1
      fi
      
      # Check if it exists after build
      if [[ ! -f "$test_path" ]]; then
        log_error "Test executable not found after build: $test_path"
        exit 1
      fi
      
      if run_single_test_binary "$test_name" "$BUILD_TYPE" "$jobs" "$GENERATE_JUNIT" "$log_file" "$junit_file" "$FILTER"; then
        ((passed_count++))
      else
        ((failed_count++))
        overall_failed=1
      fi
    done
    
    if [[ -n "$GENERATE_JUNIT" ]]; then
      echo '</testsuites>' >>"$junit_file"
    fi
    
    echo ""
    echo "=========================================="
    if [[ $failed_count -eq 0 ]]; then
      echo "✅ Multiple tests completed: $passed_count passed, $failed_count failed"
    else
      echo "❌ Multiple tests completed: $passed_count passed, $failed_count failed"
    fi
    echo "=========================================="
  else
    # Run test categories
  local all_categories_failed_completely=1
  local no_tests_ran=0
  for category in "${categories_to_run[@]}"; do
    # Check if we've been interrupted
    if [[ $INTERRUPTED -eq 1 ]]; then
      break
    fi
    
    run_test_category "$category" "$BUILD_TYPE" "$jobs" "$GENERATE_JUNIT" "$log_file" "$junit_file"
    local category_result=$?
    
    if [[ $category_result -eq 0 ]]; then
      # At least one category had all tests pass
      all_categories_failed_completely=0
    elif [[ $category_result -eq 1 ]]; then
      # Some tests passed, some failed
      overall_failed=1
      all_categories_failed_completely=0
    elif [[ $category_result -eq 2 ]]; then
      # All tests in this category failed to run
      overall_failed=1
    elif [[ $category_result -eq 3 ]]; then
      # No tests ran at all in this category
      overall_failed=1
      no_tests_ran=1
    fi
  done
  fi

  # Check if we were interrupted
  if [[ $INTERRUPTED -eq 1 ]]; then
    # Don't show normal final report - interrupt handler already printed cancellation message
    exit 130
  fi

  local overall_end_time=$(date +%s.%N)
  local total_duration=$(echo "$overall_end_time - $overall_start_time" | bc -l)

  # Final report
  # Special handling for when ALL tests in ALL categories failed to run
  if [[ -z "$SINGLE_TEST" ]] && [[ -z "$MULTIPLE_TEST_MODE" ]]; then
    # Category mode - check if all categories had all tests fail to run
    if [[ $all_categories_failed_completely -eq 1 ]]; then
      # All categories had ALL tests fail to run (no tests passed anywhere)
      # Already printed "All X tests failed to run" for each category
      # Don't show execution time or logs
      exit 1
    fi
  fi
  
  # Normal final report
  echo ""
  echo "=========================================="
  echo "⏱️ TOTAL EXECUTION TIME: ${total_duration}s"
  echo "=========================================="

  if [[ $overall_failed -eq 0 ]]; then
    log_success "All tests completed successfully! 🎉"
    log_info "📋 View test logs: cat $log_file"
  else
    log_error "Some tests failed!"
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
