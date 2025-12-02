#!/bin/bash
# Linux-specific query tool tests
# Tests: ptrace permissions, LLDB attachment on Linux
#
# Usage: ./test_query_linux.sh [--verbose]
#
# Requirements:
# - Run with --cap-add=SYS_PTRACE in Docker, or
# - ptrace_scope=0, or
# - Run as root

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
QUERY_TOOL="${PROJECT_ROOT}/.deps-cache/query-tool/ascii-query-server"
TEST_TARGET_SRC="${PROJECT_ROOT}/tests/fixtures/query_test_target.c"
TEST_TARGET="${PROJECT_ROOT}/build/tests/query_test_target"
PORT=9997

VERBOSE=0
[ "$1" = "--verbose" ] && VERBOSE=1

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

TARGET_PID=""
QUERY_PID=""

cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    [ -n "$TARGET_PID" ] && kill $TARGET_PID 2>/dev/null || true
    [ -n "$QUERY_PID" ] && kill $QUERY_PID 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

fail() {
    echo -e "${RED}FAIL: $1${NC}"
    TESTS_FAILED=$((TESTS_FAILED + 1))
}

pass() {
    echo -e "${GREEN}PASS: $1${NC}"
    TESTS_PASSED=$((TESTS_PASSED + 1))
}

skip() {
    echo -e "${YELLOW}SKIP: $1${NC}"
    TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
}

info() {
    echo -e "${BLUE}INFO: $1${NC}"
}

# Check we're on Linux
if [ "$(uname -s)" != "Linux" ]; then
    echo -e "${RED}ERROR: This script is for Linux only${NC}"
    exit 1
fi

echo -e "${BLUE}=== Linux Query Tool Tests ===${NC}"
echo ""

# Test 1: Check ptrace_scope
info "Checking ptrace_scope..."
if [ -f /proc/sys/kernel/yama/ptrace_scope ]; then
    PTRACE_SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope)
    info "ptrace_scope = $PTRACE_SCOPE"
    if [ "$PTRACE_SCOPE" = "0" ]; then
        pass "ptrace_scope is permissive (0)"
    elif [ "$PTRACE_SCOPE" = "1" ]; then
        if [ "$(id -u)" = "0" ]; then
            pass "ptrace_scope is restricted (1) but running as root"
        else
            echo -e "${YELLOW}WARNING: ptrace_scope=1 and not root. Tests may fail.${NC}"
            echo -e "${YELLOW}Fix with: echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope${NC}"
            echo -e "${YELLOW}Or run in Docker with: --cap-add=SYS_PTRACE${NC}"
        fi
    else
        echo -e "${YELLOW}WARNING: ptrace_scope=$PTRACE_SCOPE (unknown value)${NC}"
    fi
else
    info "No ptrace_scope file (non-YAMA kernel or Docker)"
    pass "No ptrace restrictions detected"
fi

# Test 2: Check query tool exists
info "Checking query tool binary..."
if [ ! -x "$QUERY_TOOL" ]; then
    echo -e "${RED}ERROR: Query tool not found at $QUERY_TOOL${NC}"
    echo -e "${YELLOW}Build with: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON && cmake --build build${NC}"
    exit 1
fi
pass "Query tool binary exists"

# Test 3: Build test target
info "Building test target..."
mkdir -p "$(dirname "$TEST_TARGET")"
if ! clang -g -O0 -o "$TEST_TARGET" "$TEST_TARGET_SRC" 2>/dev/null; then
    fail "Failed to build test target"
    exit 1
fi
pass "Test target built"

# Test 4: Start test target
info "Starting test target..."
"$TEST_TARGET" &
TARGET_PID=$!
sleep 0.5

if ! kill -0 $TARGET_PID 2>/dev/null; then
    fail "Test target failed to start"
    exit 1
fi
pass "Test target running (PID: $TARGET_PID)"

# Test 5: Attach query tool
info "Attaching query tool..."
"$QUERY_TOOL" --attach $TARGET_PID --port $PORT &
QUERY_PID=$!
sleep 2

if ! kill -0 $QUERY_PID 2>/dev/null; then
    fail "Query tool failed to start (check ptrace permissions)"
    exit 1
fi
pass "Query tool attached"

# Test 6: Check HTTP server
info "Testing HTTP server..."
RESPONSE=$(curl -s -m 5 "http://localhost:$PORT/process" 2>/dev/null || echo "CURL_FAILED")
if [ "$RESPONSE" = "CURL_FAILED" ]; then
    fail "HTTP server not responding"
else
    if echo "$RESPONSE" | grep -q '"pid"'; then
        pass "HTTP server responding with process info"
    else
        fail "HTTP server returned unexpected response"
    fi
fi

# Test 7: Test /threads endpoint
info "Testing /threads endpoint..."
RESPONSE=$(curl -s -m 5 "http://localhost:$PORT/threads" 2>/dev/null || echo "CURL_FAILED")
if echo "$RESPONSE" | grep -q '"threads"'; then
    pass "/threads endpoint working"
else
    fail "/threads endpoint failed"
fi

# Test 8: Test /stop and /continue
info "Testing process control..."
STOP_RESPONSE=$(curl -s -m 5 -X POST "http://localhost:$PORT/stop" 2>/dev/null || echo "CURL_FAILED")
if echo "$STOP_RESPONSE" | grep -q '"stopped"\|"status"'; then
    pass "/stop endpoint working"
else
    fail "/stop endpoint failed"
fi

sleep 0.5

CONTINUE_RESPONSE=$(curl -s -m 5 -X POST "http://localhost:$PORT/continue" 2>/dev/null || echo "CURL_FAILED")
if echo "$CONTINUE_RESPONSE" | grep -q '"running"\|"status"'; then
    pass "/continue endpoint working"
else
    fail "/continue endpoint failed"
fi

# Test 9: Test detach
info "Testing detach..."
DETACH_RESPONSE=$(curl -s -m 5 -X POST "http://localhost:$PORT/detach" 2>/dev/null || echo "CURL_FAILED")
if echo "$DETACH_RESPONSE" | grep -q '"detached"\|"status"'; then
    pass "Detach successful"
else
    fail "Detach failed"
fi

# Check target still running after detach
sleep 0.5
if kill -0 $TARGET_PID 2>/dev/null; then
    pass "Target survived detach"
else
    fail "Target died after detach"
fi

# Summary
echo ""
echo -e "${BLUE}=== Results ===${NC}"
echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
echo -e "Skipped: ${YELLOW}$TESTS_SKIPPED${NC}"

if [ $TESTS_FAILED -gt 0 ]; then
    exit 1
fi
echo -e "\n${GREEN}All Linux tests passed!${NC}"
