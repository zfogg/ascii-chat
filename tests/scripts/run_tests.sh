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

# Test categories
TEST_CATEGORIES=("unit" "integration" "performance")

# =============================================================================
# Helper Functions
# =============================================================================

show_help() {
    cat << EOF
ASCII-Chat Test Runner

Usage: $0 [OPTIONS]

OPTIONS:
    -t, --type TYPE         Test type: all, unit, integration, performance (default: all)
    -b, --build TYPE        Build type: debug, release, debug-coverage, release-coverage, sanitize (default: debug)
    -j, --jobs N            Number of parallel jobs (default: auto-detect CPU cores)
    -J, --junit             Generate JUnit XML output
    -v, --verbose           Verbose output
    -c, --coverage          Enable coverage (overrides build type)
    -h, --help              Show this help message

EXAMPLES:
    $0                                    # Run all tests with debug build
    $0 -t unit -b release                 # Run unit tests with release build
    $0 -t performance -J                  # Run performance tests with JUnit output
    $0 -c -t integration                  # Run integration tests with coverage
    $0 -b release-coverage -J             # Run all tests with release+coverage+JUnit
    $0 -b sanitize                        # Run with AddressSanitizer for memory checking

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

log_info() {
    echo "[INFO] $*" >&2
}

log_error() {
    echo "[ERROR] $*" >&2
}

log_verbose() {
    if [[ -n "$VERBOSE" ]]; then
        echo "[VERBOSE] $*" >&2
    fi
}

# Detect number of CPU cores
detect_cpu_cores() {
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
            echo "4"  # fallback
            ;;
    esac
}

# Get test executables for a specific category
get_test_executables() {
    local category="$1"
    local bin_dir="$PROJECT_ROOT/bin"

    case "$category" in
        unit)
            find "$bin_dir" -name "test_unit_*" -type f 2>/dev/null | sort
            ;;
        integration)
            find "$bin_dir" -name "test_integration_*" -type f 2>/dev/null | sort
            ;;
        performance)
            find "$bin_dir" -name "test_performance_*" -type f 2>/dev/null | sort
            ;;
        all)
            find "$bin_dir" -name "test_*" -type f 2>/dev/null | sort
            ;;
        *)
            log_error "Unknown test category: $category"
            return 1
            ;;
    esac
}

# Build tests if they don't exist
ensure_tests_built() {
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

# Run a single test with proper error handling
run_single_test() {
    local test_executable="$1"
    local test_name="$(basename "$test_executable")"
    local jobs="$2"
    local generate_junit="$3"
    local log_file="$4"
    local junit_file="$5"

    log_verbose "Running test: $test_name"

    if [[ -n "$generate_junit" ]]; then
        # Generate JUnit XML for this test
        local xml_file="/tmp/${test_name}_$(date +%s%N).xml"
        local test_class="$(echo "$test_name" | sed 's/^test_//; s/_test$//; s/_/./g')"

        if "$test_executable" --jobs "$jobs" --xml="$xml_file" 2>&1 | tee -a "$log_file"; then
            log_verbose "Test passed: $test_name"
            if [[ -f "$xml_file" ]]; then
                # Transform the XML to match our naming convention
                sed -n '/<testsuite/,/<\/testsuite>/p' "$xml_file" | \
                sed -e "s/<testsuite name=\"[^\"]*\"/<testsuite name=\"$test_class\"/" \
                    -e "s/<testcase name=\"/<testcase classname=\"$test_class\" name=\"/" >> "$junit_file"
                rm -f "$xml_file"
            fi
            return 0
        else
            log_verbose "Test failed: $test_name"
            return 1
        fi
    else
        # Regular test run
        if "$test_executable" --jobs "$jobs" 2>&1 | tee -a "$log_file"; then
            log_verbose "Test passed: $test_name"
            return 0
        else
            log_verbose "Test failed: $test_name"
            return 1
        fi
    fi
}

# Run tests for a specific category
run_test_category() {
    local category="$1"
    local build_type="$2"
    local jobs="$3"
    local generate_junit="$4"
    local log_file="$5"
    local junit_file="$6"

    log_info "Running $category tests..."

    # Get test executables for this category
    local test_executables
    test_executables=($(get_test_executables "$category"))

    if [[ ${#test_executables[@]} -eq 0 ]]; then
        log_info "No $category tests found. Building tests first..."
        ensure_tests_built "$build_type" "$category"
        test_executables=($(get_test_executables "$category"))

        if [[ ${#test_executables[@]} -eq 0 ]]; then
            log_error "No $category tests found after building"
            return 1
        fi
    fi

    local failed=0
    local total_tests=${#test_executables[@]}
    local passed_tests=0

    log_info "Found $total_tests $category test(s)"

    if [[ -n "$generate_junit" ]]; then
        # Initialize JUnit XML
        echo '<?xml version="1.0" encoding="UTF-8"?>' > "$junit_file"
        echo "<testsuites name=\"ASCII-Chat $category Tests\">" >> "$junit_file"
    fi

    # Run each test
    # When generating JUnit XML, run tests sequentially to avoid file conflicts
    local actual_jobs="$jobs"
    if [[ -n "$generate_junit" ]]; then
        actual_jobs="1"
        log_verbose "Running tests sequentially for JUnit XML generation"
    fi

    for test_executable in "${test_executables[@]}"; do
        if run_single_test "$test_executable" "$actual_jobs" "$generate_junit" "$log_file" "$junit_file"; then
            ((passed_tests++))
        else
            failed=1
        fi
    done

    if [[ -n "$generate_junit" ]]; then
        echo '</testsuites>' >> "$junit_file"
    fi

    # Report results
    local failed_tests=$((total_tests - passed_tests))
    log_info "$category tests completed: $passed_tests passed, $failed_tests failed"

    if [[ $failed -eq 1 ]]; then
        log_error "Some $category tests failed!"
        return 1
    fi

    return 0
}

# =============================================================================
# Main Function
# =============================================================================

main() {
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -t|--type)
                TEST_TYPE="$2"
                shift 2
                ;;
            -b|--build)
                BUILD_TYPE="$2"
                shift 2
                ;;
            -j|--jobs)
                JOBS="$2"
                shift 2
                ;;
            -J|--junit)
                GENERATE_JUNIT="1"
                shift
                ;;
            -v|--verbose)
                VERBOSE="1"
                shift
                ;;
            -c|--coverage)
                COVERAGE="1"
                shift
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_help
                exit 1
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
        > "$log_file"
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

    for category in "${categories_to_run[@]}"; do
        if ! run_test_category "$category" "$BUILD_TYPE" "$jobs" "$GENERATE_JUNIT" "$log_file" "$junit_file"; then
            overall_failed=1
        fi
    done

    # Final report
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
