#!/bin/bash

# =============================================================================
# ASCII-Chat Test Runner Script
# =============================================================================
# This script unifies all the common test running patterns from the Makefile
# into a single, reusable script that can be called from various contexts.

set -uo pipefail # Remove 'e' flag so we can handle errors ourselves
set -m           # Enable job control for better signal handling

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

# Print cancellation message
function print_cancellation_message() {
  echo ""
  echo "=========================================="
  echo "❌ TESTS CANCELLED BY USER (Ctrl-C)"
  echo "=========================================="
}

# Cleanup function for Ctrl-C
function handle_interrupt() {
  # Set interrupted flag
  INTERRUPTED=1

  # Disable the trap immediately to prevent re-entry
  trap - INT TERM HUP

  # Print cancellation message to the terminal directly (before redirecting output)
  {
    echo ""
    echo ""
    echo "=========================================="
    echo "❌ TESTS CANCELLED BY USER (Ctrl-C)"
    echo "=========================================="
  } >/dev/tty

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

# =============================================================================
# System and Build Utilities
# =============================================================================

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

# =============================================================================
# Test Discovery and File Management
# =============================================================================

# Get test executables for a specific category
function get_test_executables() {
  local category="$1"
  local build_type="${2:-debug}" # Optional build type parameter
  local bin_dir="$PROJECT_ROOT/bin"

  # Check if bin directory exists
  if [[ ! -d "$bin_dir" ]]; then
    log_verbose "bin directory not found at: $bin_dir"
    return
  fi

  # Handle coverage builds differently
  if [[ "$build_type" == "coverage" ]]; then
    case "$category" in
    unit)
      # For coverage, discover test source files and build coverage executables
      for test_file in "$PROJECT_ROOT/tests/unit"/*_test.c; do
        if [[ -f "$test_file" ]]; then
          test_name=$(basename "$test_file" _test.c)
          executable_name="test_unit_${test_name}_coverage"
          echo "$bin_dir/$executable_name"
        fi
      done | sort
      ;;
    integration)
      # For coverage, discover test source files and build coverage executables
      for test_file in "$PROJECT_ROOT/tests/integration"/*_test.c; do
        if [[ -f "$test_file" ]]; then
          test_name=$(basename "$test_file" _test.c)
          executable_name="test_integration_${test_name}_coverage"
          echo "$bin_dir/$executable_name"
        fi
      done | sort
      ;;
    performance)
      # For coverage, discover test source files and build coverage executables
      for test_file in "$PROJECT_ROOT/tests/performance"/*_test.c; do
        if [[ -f "$test_file" ]]; then
          test_name=$(basename "$test_file" _test.c)
          executable_name="test_performance_${test_name}_coverage"
          echo "$bin_dir/$executable_name"
        fi
      done | sort
      ;;
    all)
      # All coverage tests
      for f in "$bin_dir"/test_*_coverage; do
        [[ -f "$f" ]] && echo "$f"
      done | sort
      ;;
    *)
      log_error "Unknown test category: $category"
      return 1
      ;;
    esac
  else
    # Regular builds use standard naming
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
  fi
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

# =============================================================================
# Test Execution Functions
# =============================================================================

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
  local background_mode=""

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
      --background)
        background_mode="1"
        shift
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

  # Check for interrupt before starting
  if [[ $INTERRUPTED -eq 1 ]]; then
    return 130
  fi

  if [[ -n "$background_mode" ]]; then
    # Background mode: run the test and return immediately
    # The calling function will handle PID tracking and result collection
    if [[ -n "$log_file" ]]; then
      # Run test and append to log file without tee to avoid pipe issues
      "$test_executable" $jobs_flag $xml_flag $verbose_flag $color_flag $filter_flag >>"$log_file" 2>&1 &
    else
      "$test_executable" $jobs_flag $xml_flag $verbose_flag $color_flag $filter_flag 2>&1 &
    fi
    # Return the PID of the background process
    echo $!
    return 0
  else
    # Synchronous mode: run the test and wait for completion
    local test_exit_code

    if [[ -n "$log_file" ]]; then
      # Run test and append to log file without tee to avoid pipe issues
      "$test_executable" $jobs_flag $xml_flag $verbose_flag $color_flag $filter_flag >>"$log_file" 2>&1
      test_exit_code=$?
    else
      "$test_executable" $jobs_flag $xml_flag $verbose_flag $color_flag $filter_flag 2>&1
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
  fi
}

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

# =============================================================================
# Process Control and Execution Functions
# =============================================================================

# Function to start a test in background
function start_test_in_background() {
  local test_executable=$1
  local actual_jobs=$2
  local generate_junit=$3
  local test_log=$4
  local test_junit=$5

  # Build command line arguments for the test executable
  local jobs_flag=""
  if [[ -n "$actual_jobs" && "$actual_jobs" != "1" ]]; then
    jobs_flag="--jobs=$actual_jobs"
  fi

  local verbose_flag=""
  if [[ -n "$VERBOSE" ]]; then
    verbose_flag="--verbose"
  fi

  local color_flag="--color=always"

  # Build the command arguments array
  local cmd_args=()
  [[ -n "$jobs_flag" ]] && cmd_args+=("$jobs_flag")
  [[ -n "$verbose_flag" ]] && cmd_args+=("$verbose_flag")
  [[ -n "$color_flag" ]] && cmd_args+=("$color_flag")

  # Set test environment variables for fast test mode
  export TESTING=1
  export CRITERION_TEST=1

  # Run the test executable directly in background to maintain proper parent-child relationship
  if [[ -n "$test_log" ]]; then
    # Run test and append to log file
    "$test_executable" "${cmd_args[@]}" >>"$test_log" 2>&1 &
  else
    "$test_executable" "${cmd_args[@]}" 2>&1 &
  fi

  local pid=$!
  echo $pid # Return the PID of the background process
}

# Function to run tests in parallel with proper queue management
function run_tests_in_parallel() {
  local build_type=""
  local jobs=""
  local generate_junit=""
  local log_file=""
  local junit_file=""
  local max_parallel_tests=""
  local jobs_per_test=""
  local test_executables=()

  # Parse named arguments
  while [[ $# -gt 0 ]]; do
    case $1 in
      --build-type)
        build_type="$2"
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
      --max-parallel-tests)
        max_parallel_tests="$2"
        shift 2
        ;;
      --jobs-per-test)
        jobs_per_test="$2"
        shift 2
        ;;
      --test-executables)
        shift
        # Collect all remaining arguments as test executables
        while [[ $# -gt 0 ]]; do
          test_executables+=("$1")
          shift
        done
        ;;
      *)
        # If it doesn't start with --, treat as test executable
        test_executables+=("$1")
        shift
        ;;
    esac
  done

  local total_tests=${#test_executables[@]}
  local passed_tests=0
  local failed_tests=0
  local tests_that_started=0
  local tests_completed=0
  local failed_test_details=()
  local failed_test_details_file="/tmp/failed_tests_$$.txt"
  >"$failed_test_details_file"  # Initialize empty file

  # No arrays needed for worker pool approach

  # Queue of tests waiting to run
  local test_queue=("${test_executables[@]}")
  local queue_index=0


  log_info "🚀 Running $total_tests tests in parallel (up to $max_parallel_tests concurrent tests, $jobs_per_test jobs each)"

  # Worker function that runs tests sequentially until queue is empty
  worker_function() {
    local worker_id=$1
    local worker_log="/tmp/worker_${worker_id}_$$.log"
    local worker_passed=0
    local worker_failed=0
    local worker_started=0

    while true; do
      # Atomically get next test from queue using proper file locking
      local test_executable=""
      local test_index=-1

      # Use file locking to safely get next test
      local queue_file="/tmp/queue_index_$$.txt"
      local lock_file="/tmp/queue_lock_$$.txt"

      # Use simple file locking with retry logic for non-blocking parallel execution
      local retry_count=0
      local max_retries=10

      while [[ $retry_count -lt $max_retries ]]; do
        # Use mkdir for atomic locking (works on most filesystems)
        if mkdir "$lock_file" 2>/dev/null; then
          # We have the lock, now safely read and update queue index
          local current_index=0
          if [[ -f "$queue_file" ]]; then
            current_index=$(cat "$queue_file" 2>/dev/null || echo "0")
          fi

          # Check if we have more tests
          if [[ $current_index -lt ${#test_queue[@]} ]]; then
            test_executable="${test_queue[$current_index]}"
            test_index=$current_index
            echo $((current_index + 1)) >"$queue_file"
          fi

          # Release the lock
          rmdir "$lock_file" 2>/dev/null
          break
        else
          # Lock is held by another worker, wait briefly and retry
          sleep 0.001
          ((retry_count++))
        fi
      done

      # If no more tests, exit worker
      if [[ -z "$test_executable" ]]; then
        break
      fi

      local test_name="$(basename "$test_executable")"
      local test_log="/tmp/test_${test_name}_worker${worker_id}_$$.log"
      local test_junit=""
      local start_time=$(date +%s.%N)


      # Create separate JUnit file for this test if needed
      if [[ -n "$generate_junit" ]]; then
        test_junit="/tmp/junit_${test_name}_worker${worker_id}_$$.xml"
      fi

      # Print starting message to stderr
      echo "🚀 [TEST] Starting: $test_name" >&2
      log_verbose "Worker $worker_id starting test $((test_index + 1)) of $total_tests: $test_name"

      # Run test synchronously (not in background)
      local exit_code=0
      if [[ -n "$generate_junit" ]]; then
        # Run test with XML output and capture both XML and test output
        # Criterion sends XML to stdout when using --xml, so we need to capture it
        TESTING=1 CRITERION_TEST=1 "$test_executable" --jobs "$jobs_per_test" --xml >"$test_junit" 2>"$test_log"
        exit_code=$?
      else
        TESTING=1 CRITERION_TEST=1 "$test_executable" --jobs "$jobs_per_test" >"$test_log" 2>&1
        exit_code=$?
      fi

      local end_time=$(date +%s.%N)
      local duration=$(echo "$end_time - $start_time" | bc -l)
      local formatted_time=$(format_duration "$duration")

      # Process result
      if [[ $exit_code -eq 0 ]]; then
        ((worker_passed++))
        # Show passing tests
        echo -e "✅ [TEST] \033[32mPASSED\033[0m: $test_name ($formatted_time)" >&2
      elif [[ $exit_code -eq 130 ]] || [[ $exit_code -gt 128 ]]; then
        ((worker_failed++))
        echo -e "🛑 [TEST] \033[31mCANCELLED\033[0m: $test_name" >&2
      else
        ((worker_failed++))
        echo -e "❌ [TEST] \033[31mFAILED\033[0m: $test_name (exit code: $exit_code, $formatted_time)" >&2

        # Show failure details for failed tests
        if [[ -n "$generate_junit" ]]; then
          # In JUnit mode, immediately re-run the test to show failure details
          echo "--- Failure details for $test_name ---" >&2
          # Re-run the test without JUnit mode to get the actual failure output
          TESTING=1 CRITERION_TEST=1 "$test_executable" --jobs "$jobs_per_test" >&2 || true  # Ignore exit code
          echo "--- End failure details ---" >&2
        else
          # In non-JUnit mode, show the test log
          if [[ -f "$test_log" ]]; then
            echo "--- Failure details for $test_name ---" >&2
            # Show the complete test log to see all assertion errors
            cat "$test_log" >&2
            echo "--- End failure details ---" >&2
          else
            echo "--- No test log found for $test_name ---" >&2
            echo "Test log path: $test_log" >&2
          fi
        fi
      fi

      ((worker_started++))

      # Append test log to worker log
      if [[ -f "$test_log" ]]; then
        echo "=== Test: $test_name ===" >>"$worker_log"
        cat "$test_log" >>"$worker_log"
        rm -f "$test_log"
      fi

      # Append JUnit XML to worker log if needed
      if [[ -n "$generate_junit" ]] && [[ -f "$test_junit" ]]; then
        echo "=== JUnit XML for: $test_name ===" >>"$worker_log"
        cat "$test_junit" >>"$worker_log"

        # Also append to main JUnit file with file locking to prevent race conditions
        if [[ -f "$junit_file" ]]; then
          # Use file locking to prevent race conditions when multiple workers write simultaneously
          local junit_lock="/tmp/junit_lock_$$.txt"
          local retry_count=0
          local max_retries=10

          while [[ $retry_count -lt $max_retries ]]; do
            if mkdir "$junit_lock" 2>/dev/null; then
              # We have the lock, safely append to JUnit file
              awk '/<testsuite name=/ {in_testsuite=1} in_testsuite {print} /<\/testsuite>/ {if(in_testsuite) in_testsuite=0}' "$test_junit" >>"$junit_file"
              rmdir "$junit_lock"
              break
            else
              # Lock is held by another process, wait and retry
              sleep 0.001
              ((retry_count++))
            fi
          done

          # If we couldn't get the lock after max retries, append without locking (fallback)
          if [[ $retry_count -eq $max_retries ]]; then
            echo "Warning: Could not acquire JUnit file lock after $max_retries retries, appending without lock" >&2
            awk '/<testsuite name=/ {in_testsuite=1} in_testsuite {print} /<\/testsuite>/ {if(in_testsuite) in_testsuite=0}' "$test_junit" >>"$junit_file"
          fi
        fi

        rm -f "$test_junit"
      fi
    done

    # Write worker results to a results file
    echo "$worker_passed $worker_failed $worker_started" >"/tmp/worker_${worker_id}_results_$$.txt"

    # Append worker log to main log file
    if [[ -f "$worker_log" ]]; then
      cat "$worker_log" >>"$log_file"
      rm -f "$worker_log"
    fi
  }

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

  # Launch worker processes
  local worker_pids=()
  for ((i = 1; i <= max_parallel_tests; i++)); do
    worker_function $i &
    worker_pids+=($!)
  done

  # Wait for all workers to complete
  for worker_pid in "${worker_pids[@]}"; do
    wait "$worker_pid"
  done

  # Collect results from all workers
  for ((i = 1; i <= max_parallel_tests; i++)); do
    local results_file="/tmp/worker_${i}_results_$$.txt"
    if [[ -f "$results_file" ]]; then
      local worker_results=($(cat "$results_file"))
      passed_tests=$((passed_tests + worker_results[0]))
      failed_tests=$((failed_tests + worker_results[1]))
      tests_that_started=$((tests_that_started + worker_results[2]))
      rm -f "$results_file"
    fi
  done

  # Clean up queue and lock files
  rm -f "/tmp/queue_index_$$.txt" "/tmp/queue_lock_$$.txt" "/tmp/junit_lock_$$.txt" "$failed_test_details_file"

  # Return results to stdout (this is what gets captured)
  echo "$passed_tests $failed_tests $tests_that_started"
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
  local jobs_per_test

  # If jobs explicitly specified, use that for parallel tests
  if [[ -n "$jobs" ]] && [[ "$jobs" -gt 0 ]]; then
    max_parallel_tests=$jobs
    jobs_per_test=1 # Default to 1 if manually specified
  else
    # Always use CORES/2 parallel executables with CORES jobs each for maximum CPU utilization
    max_parallel_tests=$((total_cores / 2))
    jobs_per_test=$total_cores

    # Ensure at least 1 parallel test and 1 job per test
    if [[ $max_parallel_tests -lt 1 ]]; then
      max_parallel_tests=1
    fi
    if [[ $jobs_per_test -lt 1 ]]; then
      jobs_per_test=1
    fi
  fi

  # Return values as a space-separated string
  echo "$max_parallel_tests $jobs_per_test"
}

# =============================================================================
# Core Test Running Functions
# =============================================================================

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
  export TESTING=1        # Enable fast test mode for network/compression tests
  export CRITERION_TEST=1 # Ensure tests know they're running in test environment

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
    cat "$output_file" >>"$log_file"
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
    # Append Criterion's XML to the main JUnit file (extract only testsuite elements)
    if [[ -f "$xml_file" ]] && [[ -f "$junit_file" ]]; then
      awk '/<testsuite name=/ {in_testsuite=1} in_testsuite {print} /<\/testsuite>/ {if(in_testsuite) in_testsuite=0}' "$xml_file" >>"$junit_file"
    fi

    # Clean up temporary files
    rm -f "$xml_file" "$output_file"
  fi

  # Determine and report test result
  determine_test_failure --exit-code "$test_exit_code" --test-name "$test_name" --duration "$duration"

  return $test_exit_code
}


# =============================================================================
# Build Management Functions
# =============================================================================

function build_test_executable() {
  local test_name="$1"
  local build_type="$2"

  # Always build the test executable (make will handle if it's up-to-date)
  local make_target="bin/$test_name"
  log_info "🔨 Building test: $make_target in $build_type mode"

  # Build the specific test executable (filter out "up to date" messages)
  # Use PIPESTATUS to get make's exit code after piping through grep
  colored_make -C "$PROJECT_ROOT" BUILD_TYPE="$build_type" "$make_target" 2>&1 | grep -v "is up to date" | grep -v "Nothing to be done" || true
  local make_exit_code=${PIPESTATUS[0]}

  if [[ $make_exit_code -eq 0 ]]; then
    log_success "Successfully built $make_target"
    return 0
  else
    log_error "Failed to build test: $make_target"
    return 1
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
    return 1
  fi

  # Check if it exists after build
  if [[ ! -f "$test_executable" ]]; then
    log_error "Test executable not found after build: $test_executable"
    return 1
  fi

  # Run the single test
  if run_single_test --executable "$test_executable" --jobs "$jobs" --generate-junit "$generate_junit" --log-file "$log_file" --junit-file "$junit_file" --filter "$filter"; then
    log_success "Test completed successfully!"
    return 0
  else
    local exit_code=$?
    # Check if interrupted by signal (130 = SIGINT)
    if [[ $exit_code -eq 130 ]] || [[ $INTERRUPTED -eq 1 ]]; then
      return 130
    fi
    log_error "Test failed with exit code $exit_code"
    return $exit_code
  fi
}

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
      for ((i = 1; i < ${#positional_args[@]}; i++)); do
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

  if [[ -n "$SINGLE_TEST" ]]; then
    # Single test mode - construct full path
    all_tests_to_run=("$PROJECT_ROOT/bin/$SINGLE_TEST")
  elif [[ -n "$MULTIPLE_TEST_MODE" ]]; then
    # Multiple specific tests mode - construct full paths
    for test in "${MULTIPLE_TESTS[@]}"; do
      all_tests_to_run+=("$PROJECT_ROOT/bin/$test")
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
    echo "<testsuites name=\"ASCII-Chat Tests\">" >>"$junit_file"
    fi

  # Build all test executables
    local test_targets=()
  for test_executable in "${all_tests_to_run[@]}"; do
    local test_name=$(basename "$test_executable")
      test_targets+=("bin/$test_name")
    done

  log_info "🔨 Building ${#test_targets[@]} test executables..."
    local make_cmd="make -C $PROJECT_ROOT CSTD=c2x"
    if [[ "$BUILD_TYPE" != "default" ]]; then
      make_cmd="$make_cmd BUILD_TYPE=$BUILD_TYPE"
    fi
    make_cmd="$make_cmd ${test_targets[*]}"

  # Run make and capture output
    local make_output
    make_output=$(eval "$make_cmd" 2>&1)
    local make_exit_code=$?

    # Show non-"up to date" output
    echo "$make_output" | grep -v "is up to date" || true

    if [[ $make_exit_code -ne 0 ]]; then
      log_error "Make command failed with exit code $make_exit_code"
      exit 1
    fi

  # Run all tests using the existing parallel execution function
    local total_cores=$(detect_cpu_cores)
  local num_tests=${#all_tests_to_run[@]}
  local allocation=($(calculate_resource_allocation $num_tests $total_cores "$jobs"))
  local max_parallel_tests=${allocation[0]}
  local jobs_per_test=${allocation[1]}

  local results=$(run_tests_in_parallel --build-type "$BUILD_TYPE" --jobs "$jobs" --generate-junit "$GENERATE_JUNIT" --log-file "$log_file" --junit-file "$junit_file" --max-parallel-tests "$max_parallel_tests" --jobs-per-test "$jobs_per_test" --test-executables "${all_tests_to_run[@]}")
  local passed_tests=$(echo "$results" | cut -d' ' -f1)
  local failed_tests=$(echo "$results" | cut -d' ' -f2)
  local tests_that_started=$(echo "$results" | cut -d' ' -f3)

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
  if [[ $failed_tests -eq 0 ]]; then
    echo "✅ Tests completed: $passed_tests passed, $failed_tests failed"
      overall_failed=0
    else
    echo "❌ Tests completed: $passed_tests passed, $failed_tests failed"
      overall_failed=1
    fi
  echo "=========================================="

  # Final exit logic
  local overall_end_time=$(date +%s.%N)
  local total_duration=$(echo "$overall_end_time - $overall_start_time" | bc -l)

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
