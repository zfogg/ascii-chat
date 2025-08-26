#!/bin/bash

# ASCII-Chat Test Runner Script
# Runs all Criterion tests and provides colored output

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="$PROJECT_DIR/bin"

echo -e "${BLUE}ASCII-Chat Test Runner${NC}"
echo -e "${BLUE}======================${NC}"
echo ""

# Check if make is available
if ! command -v make &> /dev/null; then
    echo -e "${RED}Error: make is not installed${NC}"
    exit 1
fi

# Check if we're in the right directory
if [ ! -f "$PROJECT_DIR/Makefile" ]; then
    echo -e "${RED}Error: Not in ASCII-Chat project directory${NC}"
    exit 1
fi

# Build tests
echo -e "${YELLOW}Building tests...${NC}"
cd "$PROJECT_DIR"

if ! make debug > /dev/null 2>&1; then
    echo -e "${RED}Error: Failed to build main project${NC}"
    exit 1
fi

# Check if any tests exist
TEST_EXECUTABLES=$(find "$BIN_DIR" -name "test_*" 2>/dev/null || true)

if [ -z "$TEST_EXECUTABLES" ]; then
    echo -e "${YELLOW}No test executables found. Building tests...${NC}"
    if ! make test-unit test-integration > /dev/null 2>&1; then
        echo -e "${RED}Error: Failed to build tests${NC}"
        echo "Run 'make test' to see detailed error messages."
        exit 1
    fi
    TEST_EXECUTABLES=$(find "$BIN_DIR" -name "test_*" 2>/dev/null || true)
fi

if [ -z "$TEST_EXECUTABLES" ]; then
    echo -e "${YELLOW}No tests found to run.${NC}"
    echo "Create test files in tests/unit/ or tests/integration/ directories."
    exit 0
fi

echo -e "${GREEN}Found $(echo "$TEST_EXECUTABLES" | wc -l) test executable(s)${NC}"
echo ""

# Run tests
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

for test_exe in $TEST_EXECUTABLES; do
    test_name=$(basename "$test_exe")
    echo -e "${BLUE}Running $test_name...${NC}"
    
    if "$test_exe" --verbose; then
        echo -e "${GREEN}✓ $test_name PASSED${NC}"
        ((PASSED_TESTS++))
    else
        echo -e "${RED}✗ $test_name FAILED${NC}"
        ((FAILED_TESTS++))
    fi
    ((TOTAL_TESTS++))
    echo ""
done

# Summary
echo -e "${BLUE}Test Summary${NC}"
echo -e "${BLUE}============${NC}"
echo -e "Total tests: $TOTAL_TESTS"
echo -e "${GREEN}Passed: $PASSED_TESTS${NC}"

if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "${RED}Failed: $FAILED_TESTS${NC}"
    echo ""
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
else
    echo -e "${RED}Failed: 0${NC}"
    echo ""
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
fi