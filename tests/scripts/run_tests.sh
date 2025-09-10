#!/bin/bash

# =============================================================================
# ASCII-Chat Test Runner Script
# =============================================================================
# This script unifies all the common test running patterns from the Makefile
# into a single, reusable script that can be called from various contexts.

set -euo pipefail

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
    -b, --build TYPE        Build type: debug, release, debug-coverage, release-coverage, sanitize (default: debug)
    -j, --jobs N            Number of parallel jobs (default: auto-detect CPU cores)
    -J, --junit             Generate JUnit XML output
    -v, --verbose           Verbose output
    -c, --coverage          Enable coverage (overrides build type)
    -f, --filter PATTERN    Filter tests by pattern with wildcard support (only works with single test binary)
    -h, --help              Show this help message

ARGUMENTS:
    TEST_NAME               Run a single test binary by name or path (e.g., test_unit_ascii_test, bin/test_unit_ascii_test, /path/to/test)

EXAMPLES:
    $0                                    # Run all tests with debug build
    $0 -t unit -b release                 # Run unit tests with release build
    $0 -t performance -J                  # Run performance tests with JUnit output
    $0 -c -t integration                  # Run integration tests with coverage
    $0 -b release-coverage -J             # Run all tests with release+coverage+JUnit
    $0 -b sanitize                        # Run with AddressSanitizer for memory checking
    $0 test_unit_ascii_test               # Run single test binary by name
    $0 bin/test_unit_ascii_test           # Run single test binary by relative path
    $0 test_unit_ascii_test -f "basic"    # Run tests matching "*basic*" (wildcards added automatically)
    $0 test_unit_ascii_test -f "*basic*"  # Same as above (asterisks stripped if provided)

BUILD TYPES:
    debug                 - Debug build with symbols, no optimization
    release               - Release build with optimizations and LTO
    debug-coverage        - Debug build with coverage instrumentation
    release-coverage      - Release build with coverage instrumentation
    sanitize              - Debug build with AddressSanitizer for memory checking

TEST TYPES:
    all                   - Run all test categories
    unit                  - Run only unit tests
    integration           - Run only integration tests
    performance           - Run only performance tests

EOF
}

function log_info() {
  echo "[INFO] $*" >&2
}

function log_error() {
  echo "[ERROR] $*" >&2
}

function log_verbose() {
  if [[ -n "$VERBOSE" ]]; then
    echo "[VERBOSE] $*" >&2
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
  local bin_dir="$PROJECT_ROOT/bin"

  # Check if bin directory exists
  if [[ ! -d "$bin_dir" ]]; then
    log_verbose "bin directory not found at: $bin_dir"
    return
  fi

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

  log_info "Ensuring tests are built with $build_type configuration..."

  cd "$PROJECT_ROOT"

  case "$build_type" in
  debug)
    make tests-debug
    ;;
  release)
    make tests-release
    ;;
  debug-coverage)
    make tests-debug-coverage
    ;;
  release-coverage)
    make tests-release-coverage
    ;;
  sanitize)
    make sanitize
    make tests
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
  local jobs="$(detect_cpu_cores)"
  local xml_file=""
  local color="always"
  local verbose="1"

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
      --xml)
        xml_file="$2"
        shift 2
        ;;
      --color)
        color="$2"
        shift 2
        ;;
      --verbose)
        verbose="1"
        shift 2
        ;;
      *)
        echo "Unknown option: $1" >&2
        return 1
        ;;
    esac
  done

  if [[ -n "$jobs" ]]; then
    local jobs_flag="--jobs=$jobs"
  else
    local jobs_flag=""
  fi

  if [[ -n "$xml_file" ]]; then
    local xml_flag="--xml=$xml_file"
  else
    local xml_flag=""
  fi

  local color_flag="--color=$color"

  if [[ -n $verbose ]]; then
    local verbose_flag="--verbose"
  else
    local verbose_flag=""
  fi

  echo "DEBUG: TESTING=$TESTING" >&2
  env TESTING=1 $test_executable $jobs_flag $xml_flag $verbose_flag $color_flag 2>&1 | clean_verbose_test_output
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
    echo "[TEST] PASSED: $test_name (${duration}s)"
    return 0
  else
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

    echo "[TEST] FAILED: $test_name (${duration}s, exit code: $test_exit_code)"
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

  echo "[TEST] Starting: $test_name (TESTING=$TESTING)"
  local test_start_time=$(date +%s.%N)

  if [[ -n "$generate_junit" ]]; then
    # Generate JUnit XML for this test
    local xml_file="/tmp/${test_name}_$(date +%s%N).xml"
    local test_class="$(echo "$test_name" | sed 's/^test_//; s/_test$//; s/_/./g')"
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
      $timeout_cmd exec_test_executable --executable "$test_executable" --xml "$xml_file" --filter "$filter" --log-file "$log_file" | tee "$output_file"
    else
      # No timeout available, run directly
      exec_test_executable --executable "$test_executable" --xml "$xml_file" --filter "$filter" --log-file "$log_file" | tee "$output_file"
    fi
    test_exit_code=$?

    # Output to log file
    cat "$output_file" >> "$log_file"
  else
    exec_test_executable --executable "$test_executable" --filter "$filter" --log-file "$log_file"
    test_exit_code=$?
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
  echo "Starting $category tests..."
  echo "=========================================="
  local category_start_time=$(date +%s.%N)

  # Get test executables for this category
  local test_executables
  log_verbose "Looking for $category tests in: $PROJECT_ROOT/bin"
  log_verbose "Using find pattern: test_${category}_*"
  test_executables=($(get_test_executables "$category"))
  log_verbose "Found ${#test_executables[@]} test executables"

  if [[ ${#test_executables[@]} -eq 0 ]]; then
    log_info "No $category tests found in $PROJECT_ROOT/bin"
    log_info "Looking for any test executables:"
    ls -la "$PROJECT_ROOT/bin/" 2>/dev/null | grep test_ || echo "  No test files in bin/"
    log_info "Direct find command result:"
    find "$PROJECT_ROOT/bin" -name "test_${category}_*" -type f 2>&1

    log_info "Building tests with $build_type configuration..."
    ensure_tests_built "$build_type" "$category"
    test_executables=($(get_test_executables "$category"))

    if [[ ${#test_executables[@]} -eq 0 ]]; then
      log_error "No $category tests found after building"
      log_error "Contents of bin directory:"
      ls -la "$PROJECT_ROOT/bin/" 2>/dev/null || echo "bin/ directory does not exist"
      return 1
    fi
  fi

  local failed=0
  local total_tests=${#test_executables[@]}
  local passed_tests=0

  log_info "Found $total_tests $category test(s)"

  if [[ -n "$generate_junit" ]]; then
    # Initialize JUnit XML
    echo '<?xml version="1.0" encoding="UTF-8"?>' >"$junit_file"
    echo "<testsuites name=\"ASCII-Chat $category Tests\">" >>"$junit_file"
  fi

  # Run each test
  # When generating JUnit XML, run tests sequentially to avoid file conflicts
  local actual_jobs="$jobs"
  if [[ -n "$generate_junit" ]]; then
    actual_jobs="1"
    log_verbose "Running tests sequentially for JUnit XML generation"
  fi

  for test_executable in "${test_executables[@]}"; do
    if run_single_test --executable "$test_executable" --jobs "$actual_jobs" --generate-junit "$generate_junit" --log-file "$log_file" --junit-file "$junit_file"; then
      ((passed_tests++))
    else
      local exit_code=$?
      failed=1
      # Continue running other tests even if one fails/crashes
      log_info "Test failed with exit code $exit_code, continuing with remaining tests..."
    fi
  done

  if [[ -n "$generate_junit" ]]; then
    # Remove trap and close XML properly
    trap - EXIT INT TERM
    echo '</testsuites>' >>"$junit_file"
  fi

  # Report results
  local category_end_time=$(date +%s.%N)
  local category_duration=$(echo "$category_end_time - $category_start_time" | bc -l)
  local failed_tests=$((total_tests - passed_tests))

  echo ""
  echo "=========================================="
  echo "$category tests completed: $passed_tests passed, $failed_tests failed"
  echo "$category execution time: ${category_duration}s"
  echo "=========================================="

  if [[ $failed -eq 1 ]]; then
    log_error "Some $category tests failed!"
    return 1
  fi

  return 0
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

  log_info "Running single test binary: $test_name"

  # Determine the test executable path
  local test_executable
  if [[ "$test_name" == /* ]]; then
    # Absolute path
    test_executable="$test_name"
  elif [[ "$test_name" == bin/* ]]; then
    # Relative path starting with bin/
    test_executable="$PROJECT_ROOT/$test_name"
  else
    # Just the test name, assume it's in bin/
    test_executable="$PROJECT_ROOT/bin/$test_name"
  fi

  if [[ ! -f "$test_executable" ]]; then
    log_error "Test executable not found: $test_executable"
    log_error "Please run 'make' first to build the test"
    return 1
  fi

  # Run the single test
  if run_single_test --executable "$test_executable" --jobs "$jobs" --generate-junit "$generate_junit" --log-file "$log_file" --junit-file "$junit_file" --filter "$filter"; then
    log_info "Test completed successfully"
    return 0
  else
    local exit_code=$?
    log_error "Test failed with exit code $exit_code"
    return 1
  fi
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
  # Capture output to both stdout and log file
  env TESTING=1 "$test_executable" $jobs_flag $xml_flag $verbose_flag $color_flag $filter_flag 2>&1 | tee -a "$log_file"

  return ${PIPESTATUS[0]}
}

# =============================================================================
# Main Function
# =============================================================================

function main() {
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
      # If it doesn't start with -, it's a test name
      if [[ -z "$SINGLE_TEST" ]]; then
        SINGLE_TEST="$1"
      else
        log_error "Multiple test names specified: $SINGLE_TEST and $1"
        exit 1
      fi
      shift
      ;;
    esac
  done

  # Validate arguments
  if [[ ! "$TEST_TYPE" =~ ^(all|unit|integration|performance)$ ]]; then
    log_error "Invalid test type: $TEST_TYPE"
    exit 1
  fi

  if [[ ! "$BUILD_TYPE" =~ ^(debug|release|debug-coverage|release-coverage|sanitize)$ ]]; then
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
      BUILD_TYPE="debug-coverage"
      ;;
    release)
      BUILD_TYPE="release-coverage"
      ;;
    esac
    log_info "Coverage requested, using build type: $BUILD_TYPE"
  fi

  # Detect CPU cores
  local jobs
  jobs=$(detect_cpu_cores)
  log_info "Using $jobs parallel jobs"

  # Setup logging
  local log_file="/tmp/test_logs.txt"
  local junit_file="$PROJECT_ROOT/junit.xml"

  if [[ -n "$GENERATE_JUNIT" ]]; then
    log_info "JUnit XML output will be generated: $junit_file"
    rm -f "$junit_file"
  else
    log_info "Test logs will be saved to: $log_file"
    >"$log_file"
  fi

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
  else
    # Run test categories
  for category in "${categories_to_run[@]}"; do
    if ! run_test_category "$category" "$BUILD_TYPE" "$jobs" "$GENERATE_JUNIT" "$log_file" "$junit_file"; then
      overall_failed=1
    fi
  done
  fi

  local overall_end_time=$(date +%s.%N)
  local total_duration=$(echo "$overall_end_time - $overall_start_time" | bc -l)

  # Final report
  echo ""
  echo "=========================================="
  echo "TOTAL EXECUTION TIME: ${total_duration}s"
  echo "=========================================="

  if [[ $overall_failed -eq 0 ]]; then
    log_info "All tests completed successfully!"
  else
    log_error "Some tests failed!"
    if [[ -z "$GENERATE_JUNIT" ]]; then
      log_info "View test logs: cat $log_file"
    fi
    exit 1
  fi

  if [[ -z "$GENERATE_JUNIT" ]]; then
    log_info "View test logs: cat $log_file"
  fi
}

# =============================================================================
# Script Entry Point
# =============================================================================

# Only run main if script is executed directly (not sourced)
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  main "$@"
fi
