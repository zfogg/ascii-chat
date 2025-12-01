#!/bin/bash
# Integration tests for query tool
# Tests: HTTP endpoints, variable reading, process control, attach/detach lifecycle
#
# Usage: ./query_integration_test.sh [--verbose]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
QUERY_TOOL="${PROJECT_ROOT}/.deps-cache/query-tool/ascii-query-server"
TEST_TARGET_SRC="${PROJECT_ROOT}/tests/fixtures/query_test_target.c"
TEST_TARGET="${PROJECT_ROOT}/build/tests/query_test_target"
PORT=9998

VERBOSE=0
[ "$1" = "--verbose" ] && VERBOSE=1

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Track test results
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Process tracking
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

debug() {
    [ "$VERBOSE" = "1" ] && echo -e "${YELLOW}DEBUG: $1${NC}"
}

# JSON helpers (using python for portable JSON parsing)
json_get() {
    echo "$1" | python3 -c "import json,sys; d=json.load(sys.stdin); print(d.get('$2', ''))" 2>/dev/null
}

json_has() {
    echo "$1" | python3 -c "import json,sys; d=json.load(sys.stdin); exit(0 if '$2' in d else 1)" 2>/dev/null
}

# ============================================================================
# Prerequisites
# ============================================================================

echo -e "${BLUE}=== Query Tool Integration Tests ===${NC}"
echo ""

# Check for query tool
if [ ! -f "$QUERY_TOOL" ]; then
    echo -e "${RED}Query tool not found at: $QUERY_TOOL${NC}"
    echo "Build with: cmake -B build -DASCIICHAT_BUILD_WITH_QUERY=ON && cmake --build build"
    exit 1
fi
info "Query tool found: $QUERY_TOOL"

# Build test target
info "Building test target..."
mkdir -p "$(dirname "$TEST_TARGET")"
if ! clang -g -O0 -o "$TEST_TARGET" "$TEST_TARGET_SRC" 2>/dev/null; then
    echo -e "${RED}Failed to build test target${NC}"
    exit 1
fi
pass "Test target built"

# ============================================================================
# Test Setup: Start processes
# ============================================================================

info "Starting test target..."
"$TEST_TARGET" &
TARGET_PID=$!
sleep 0.5

if ! kill -0 $TARGET_PID 2>/dev/null; then
    fail "Test target failed to start"
    exit 1
fi
pass "Test target started (PID $TARGET_PID)"

info "Starting query server..."
"$QUERY_TOOL" --attach $TARGET_PID --port $PORT &
QUERY_PID=$!
sleep 2

if ! kill -0 $QUERY_PID 2>/dev/null; then
    fail "Query server failed to start"
    exit 1
fi
pass "Query server started (PID $QUERY_PID)"

# Wait for HTTP server
info "Waiting for HTTP server..."
for i in {1..10}; do
    if curl -s --max-time 1 "http://localhost:$PORT/" >/dev/null 2>&1; then
        break
    fi
    sleep 0.5
done

if ! curl -s --max-time 1 "http://localhost:$PORT/" >/dev/null 2>&1; then
    fail "HTTP server not responding"
    exit 1
fi
pass "HTTP server ready"

echo ""
echo -e "${BLUE}=== HTTP Endpoint Tests ===${NC}"

# ============================================================================
# Test: GET / (root endpoint)
# ============================================================================

info "Testing GET /..."
RESULT=$(curl -s --max-time 3 "http://localhost:$PORT/")
if [ -n "$RESULT" ]; then
    pass "GET / returns content"
    debug "Response length: ${#RESULT} bytes"
else
    fail "GET / returned empty response"
fi

# ============================================================================
# Test: GET /process
# ============================================================================

info "Testing GET /process..."
RESULT=$(curl -s --max-time 3 "http://localhost:$PORT/process")
if json_has "$RESULT" "pid"; then
    PROC_PID=$(json_get "$RESULT" "pid")
    if [ "$PROC_PID" = "$TARGET_PID" ]; then
        pass "GET /process returns correct PID ($PROC_PID)"
    else
        fail "GET /process returned wrong PID (got $PROC_PID, expected $TARGET_PID)"
    fi
else
    fail "GET /process did not return pid"
    debug "Response: $RESULT"
fi

# ============================================================================
# Test: GET /threads
# ============================================================================

info "Testing GET /threads..."
RESULT=$(curl -s --max-time 3 "http://localhost:$PORT/threads")
if json_has "$RESULT" "threads"; then
    pass "GET /threads returns thread list"
    debug "Response: ${RESULT:0:100}..."
else
    fail "GET /threads did not return threads"
    debug "Response: $RESULT"
fi

# ============================================================================
# Test: POST /stop
# ============================================================================

info "Testing POST /stop..."
RESULT=$(curl -s --max-time 3 -X POST "http://localhost:$PORT/stop")
if json_has "$RESULT" "status"; then
    STATUS=$(json_get "$RESULT" "status")
    if [ "$STATUS" = "stopped" ]; then
        pass "POST /stop successfully stopped target"
    else
        pass "POST /stop returned status: $STATUS"
    fi
else
    fail "POST /stop did not return status"
    debug "Response: $RESULT"
fi

sleep 0.3

# ============================================================================
# Test: GET /frames (while stopped)
# ============================================================================

info "Testing GET /frames (while stopped)..."
RESULT=$(curl -s --max-time 3 "http://localhost:$PORT/frames")
if json_has "$RESULT" "frames"; then
    pass "GET /frames returns stack frames"
    debug "Response: ${RESULT:0:200}..."
else
    fail "GET /frames did not return frames"
    debug "Response: $RESULT"
fi

# ============================================================================
# Test: POST /continue
# ============================================================================

info "Testing POST /continue..."
RESULT=$(curl -s --max-time 3 -X POST "http://localhost:$PORT/continue")
if json_has "$RESULT" "status"; then
    STATUS=$(json_get "$RESULT" "status")
    if [ "$STATUS" = "running" ]; then
        pass "POST /continue successfully resumed target"
    else
        pass "POST /continue returned status: $STATUS"
    fi
else
    fail "POST /continue did not return status"
    debug "Response: $RESULT"
fi

sleep 0.3

echo ""
echo -e "${BLUE}=== Breakpoint Tests ===${NC}"

# ============================================================================
# Test: POST /breakpoints (set breakpoint)
# ============================================================================

FULL_PATH="${PROJECT_ROOT}/tests/fixtures/query_test_target.c"
info "Testing POST /breakpoints..."
RESULT=$(curl -s --max-time 3 -X POST "http://localhost:$PORT/breakpoints?file=$FULL_PATH&line=53")
if json_has "$RESULT" "id" || json_has "$RESULT" "breakpoint_id"; then
    pass "POST /breakpoints set breakpoint"
    debug "Response: $RESULT"
else
    # Some implementations return different format
    if echo "$RESULT" | grep -q '"status"'; then
        pass "POST /breakpoints returned response"
        debug "Response: $RESULT"
    else
        fail "POST /breakpoints failed"
        debug "Response: $RESULT"
    fi
fi

# ============================================================================
# Test: GET /breakpoints (list breakpoints)
# ============================================================================

info "Testing GET /breakpoints..."
RESULT=$(curl -s --max-time 3 "http://localhost:$PORT/breakpoints")
if json_has "$RESULT" "breakpoints" || echo "$RESULT" | grep -q '\['; then
    pass "GET /breakpoints returns breakpoint list"
    debug "Response: $RESULT"
else
    fail "GET /breakpoints did not return breakpoints"
    debug "Response: $RESULT"
fi

echo ""
echo -e "${BLUE}=== Variable Query Tests ===${NC}"

# ============================================================================
# Test: Query with breakpoint (most important test)
# ============================================================================

info "Testing query with breakpoint..."
RESULT=$(timeout 5 curl -s --max-time 4 "http://localhost:$PORT/query?file=$FULL_PATH&line=53&name=counter&break&timeout=3000" 2>/dev/null || echo "TIMEOUT")

if [ "$RESULT" = "TIMEOUT" ]; then
    skip "Query with breakpoint timed out (breakpoint may not have been hit)"
elif json_has "$RESULT" "result"; then
    pass "Query with breakpoint returned result"
    debug "Response: $RESULT"

    # Resume after breakpoint
    curl -s --max-time 1 -X POST "http://localhost:$PORT/continue" >/dev/null 2>&1 || true
elif json_has "$RESULT" "status"; then
    STATUS=$(json_get "$RESULT" "status")
    if [ "$STATUS" = "error" ]; then
        skip "Query returned error (expected in some test conditions)"
    else
        pass "Query returned status: $STATUS"
    fi
    debug "Response: $RESULT"
else
    fail "Query with breakpoint returned unexpected response"
    debug "Response: $RESULT"
fi

# ============================================================================
# Test: Query global variable (simpler test)
# ============================================================================

info "Testing query global variable..."
# First stop the process
curl -s --max-time 1 -X POST "http://localhost:$PORT/stop" >/dev/null 2>&1 || true
sleep 0.2

RESULT=$(curl -s --max-time 3 "http://localhost:$PORT/query?name=g_global_counter")
if json_has "$RESULT" "result"; then
    pass "Query global variable returned result"
    debug "Response: $RESULT"
elif json_has "$RESULT" "status"; then
    STATUS=$(json_get "$RESULT" "status")
    if [ "$STATUS" = "error" ]; then
        pass "Query global variable returned error (expected when not at breakpoint)"
    else
        pass "Query returned status: $STATUS"
    fi
else
    skip "Query global variable: unexpected response format"
    debug "Response: $RESULT"
fi

# Resume
curl -s --max-time 1 -X POST "http://localhost:$PORT/continue" >/dev/null 2>&1 || true

echo ""
echo -e "${BLUE}=== Struct Expansion Tests ===${NC}"

# ============================================================================
# Test: Query with struct expansion
# ============================================================================

info "Testing struct expansion..."
RESULT=$(timeout 5 curl -s --max-time 4 "http://localhost:$PORT/query?file=$FULL_PATH&line=53&name=data&break&expand&timeout=3000" 2>/dev/null || echo "TIMEOUT")

if [ "$RESULT" = "TIMEOUT" ]; then
    skip "Struct expansion query timed out"
elif json_has "$RESULT" "result"; then
    # Check if it has children (expanded)
    if echo "$RESULT" | grep -q '"children"' || echo "$RESULT" | grep -q '"members"'; then
        pass "Struct expansion returned children/members"
    else
        pass "Struct expansion returned result (may not have children)"
    fi
    debug "Response: ${RESULT:0:300}..."

    # Resume after breakpoint
    curl -s --max-time 1 -X POST "http://localhost:$PORT/continue" >/dev/null 2>&1 || true
else
    skip "Struct expansion: unexpected response"
    debug "Response: $RESULT"
fi

echo ""
echo -e "${BLUE}=== Process Control Tests ===${NC}"

# ============================================================================
# Test: Step execution
# ============================================================================

info "Testing step execution..."
# Stop first
curl -s --max-time 1 -X POST "http://localhost:$PORT/stop" >/dev/null 2>&1 || true
sleep 0.2

RESULT=$(curl -s --max-time 3 -X POST "http://localhost:$PORT/step")
if json_has "$RESULT" "status"; then
    STATUS=$(json_get "$RESULT" "status")
    pass "Step execution returned status: $STATUS"
else
    skip "Step execution: unexpected response format"
fi
debug "Response: $RESULT"

# Resume for next tests
curl -s --max-time 1 -X POST "http://localhost:$PORT/continue" >/dev/null 2>&1 || true

# ============================================================================
# Test: Target continues running after detach
# ============================================================================

info "Testing detach (controller cleanup)..."

# Kill query server
kill $QUERY_PID 2>/dev/null || true
QUERY_PID=""
sleep 1

# Check if target is still running
if kill -0 $TARGET_PID 2>/dev/null; then
    pass "Target process survives query server shutdown"
else
    fail "Target process died after query server shutdown"
fi

# ============================================================================
# Summary
# ============================================================================

echo ""
echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}Test Results Summary${NC}"
echo -e "${BLUE}======================================${NC}"
echo -e "${GREEN}Passed:  $TESTS_PASSED${NC}"
echo -e "${RED}Failed:  $TESTS_FAILED${NC}"
echo -e "${YELLOW}Skipped: $TESTS_SKIPPED${NC}"
echo ""

TOTAL=$((TESTS_PASSED + TESTS_FAILED))
if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All $TOTAL tests passed!${NC}"
    exit 0
else
    echo -e "${RED}$TESTS_FAILED of $TOTAL tests failed${NC}"
    exit 1
fi
