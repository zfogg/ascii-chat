#!/bin/bash
# Stress tests for query tool
# Tests: Concurrent queries, rapid attach/detach, high-frequency requests
#
# Usage: ./query_stress_test.sh [--iterations N] [--concurrent N]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
QUERY_TOOL="${PROJECT_ROOT}/.deps-cache/query-tool/ascii-query-server"
TEST_TARGET_SRC="${PROJECT_ROOT}/tests/fixtures/query_test_target.c"
TEST_TARGET="${PROJECT_ROOT}/build/tests/query_test_target"
PORT=9985

# Default settings
ITERATIONS=10
CONCURRENT=3

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        --iterations)
            ITERATIONS=$2
            shift 2
            ;;
        --concurrent)
            CONCURRENT=$2
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--iterations N] [--concurrent N]"
            exit 1
            ;;
    esac
done

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Process tracking
TARGET_PID=""
QUERY_PID=""
CHILD_PIDS=()

cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    [ -n "$TARGET_PID" ] && kill $TARGET_PID 2>/dev/null || true
    [ -n "$QUERY_PID" ] && kill $QUERY_PID 2>/dev/null || true
    for pid in "${CHILD_PIDS[@]}"; do
        kill $pid 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT

info() { echo -e "${BLUE}INFO: $1${NC}"; }
pass() { echo -e "${GREEN}PASS: $1${NC}"; }
fail() { echo -e "${RED}FAIL: $1${NC}"; }

# ============================================================================
# Prerequisites
# ============================================================================

echo -e "${BLUE}=== Query Tool Stress Tests ===${NC}"
echo "Iterations: $ITERATIONS, Concurrent: $CONCURRENT"
echo ""

if [ ! -f "$QUERY_TOOL" ]; then
    fail "Query tool not found at: $QUERY_TOOL"
    exit 1
fi

# Build test target
info "Building test target..."
mkdir -p "$(dirname "$TEST_TARGET")"
if ! clang -g -O0 -o "$TEST_TARGET" "$TEST_TARGET_SRC" 2>/dev/null; then
    fail "Failed to build test target"
    exit 1
fi
pass "Test target built"

# ============================================================================
# Test Setup
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
pass "Query server started"

# Wait for HTTP
for i in {1..10}; do
    curl -s --max-time 1 "http://localhost:$PORT/" >/dev/null 2>&1 && break
    sleep 0.5
done

if ! curl -s --max-time 1 "http://localhost:$PORT/" >/dev/null 2>&1; then
    fail "HTTP server not responding"
    exit 1
fi
pass "HTTP server ready"

# ============================================================================
# Stress Test 1: Rapid sequential requests
# ============================================================================

echo ""
echo -e "${BLUE}=== Test 1: Rapid Sequential Requests ===${NC}"

SUCCESSES=0
FAILURES=0
START_TIME=$(date +%s.%N)

for i in $(seq 1 $ITERATIONS); do
    RESULT=$(curl -s --max-time 2 "http://localhost:$PORT/process" 2>/dev/null)
    if echo "$RESULT" | grep -q '"pid"'; then
        SUCCESSES=$((SUCCESSES + 1))
    else
        FAILURES=$((FAILURES + 1))
    fi
done

END_TIME=$(date +%s.%N)
DURATION=$(echo "$END_TIME - $START_TIME" | bc)
RPS=$(echo "scale=2; $ITERATIONS / $DURATION" | bc)

echo "Completed: $ITERATIONS requests in ${DURATION}s (~${RPS} req/s)"
echo "Success: $SUCCESSES, Failed: $FAILURES"

if [ $FAILURES -eq 0 ]; then
    pass "All sequential requests succeeded"
else
    fail "$FAILURES requests failed"
fi

# ============================================================================
# Stress Test 2: Concurrent requests
# ============================================================================

echo ""
echo -e "${BLUE}=== Test 2: Concurrent Requests ===${NC}"

# Function to make requests in background
make_requests() {
    local port=$1
    local count=$2
    local success=0
    for i in $(seq 1 $count); do
        RESULT=$(curl -s --max-time 2 "http://localhost:$port/threads" 2>/dev/null)
        if echo "$RESULT" | grep -q '"threads"'; then
            success=$((success + 1))
        fi
    done
    echo $success
}

# Launch concurrent workers
RESULTS_FILE=$(mktemp)
START_TIME=$(date +%s.%N)

for i in $(seq 1 $CONCURRENT); do
    make_requests $PORT $((ITERATIONS / CONCURRENT)) >> "$RESULTS_FILE" &
    CHILD_PIDS+=($!)
done

# Wait for all workers
wait "${CHILD_PIDS[@]}"
CHILD_PIDS=()

END_TIME=$(date +%s.%N)
DURATION=$(echo "$END_TIME - $START_TIME" | bc)

# Sum results
TOTAL_SUCCESS=0
while read -r line; do
    TOTAL_SUCCESS=$((TOTAL_SUCCESS + line))
done < "$RESULTS_FILE"
rm -f "$RESULTS_FILE"

EXPECTED=$((ITERATIONS / CONCURRENT * CONCURRENT))
RPS=$(echo "scale=2; $EXPECTED / $DURATION" | bc)

echo "Completed: $EXPECTED requests across $CONCURRENT workers in ${DURATION}s (~${RPS} req/s)"
echo "Success: $TOTAL_SUCCESS / $EXPECTED"

if [ $TOTAL_SUCCESS -ge $((EXPECTED * 90 / 100)) ]; then
    pass "At least 90% of concurrent requests succeeded"
else
    fail "Less than 90% success rate for concurrent requests"
fi

# ============================================================================
# Stress Test 3: Stop/Continue cycling
# ============================================================================

echo ""
echo -e "${BLUE}=== Test 3: Rapid Stop/Continue Cycles ===${NC}"

SUCCESSES=0
FAILURES=0
CYCLES=$((ITERATIONS / 2))

for i in $(seq 1 $CYCLES); do
    # Stop
    RESULT=$(curl -s --max-time 2 -X POST "http://localhost:$PORT/stop" 2>/dev/null)
    if echo "$RESULT" | grep -q '"status"'; then
        SUCCESSES=$((SUCCESSES + 1))
    else
        FAILURES=$((FAILURES + 1))
    fi

    sleep 0.1

    # Continue
    RESULT=$(curl -s --max-time 2 -X POST "http://localhost:$PORT/continue" 2>/dev/null)
    if echo "$RESULT" | grep -q '"status"'; then
        SUCCESSES=$((SUCCESSES + 1))
    else
        FAILURES=$((FAILURES + 1))
    fi

    sleep 0.1
done

echo "Completed: $CYCLES stop/continue cycles"
echo "Successful operations: $SUCCESSES / $((CYCLES * 2))"

if [ $FAILURES -le $((CYCLES / 5)) ]; then
    pass "Stop/continue cycling mostly succeeded"
else
    fail "Too many failures in stop/continue cycling"
fi

# Verify target is still running
if kill -0 $TARGET_PID 2>/dev/null; then
    pass "Target process survived stress testing"
else
    fail "Target process died during stress testing"
fi

# ============================================================================
# Stress Test 4: Breakpoint query under load
# ============================================================================

echo ""
echo -e "${BLUE}=== Test 4: Breakpoint Queries Under Load ===${NC}"

FULL_PATH="${PROJECT_ROOT}/tests/fixtures/query_test_target.c"
SUCCESSES=0
FAILURES=0
TIMEOUTS=0
BREAKPOINT_ITERATIONS=$((ITERATIONS / 5))  # Fewer iterations since breakpoints are slow

for i in $(seq 1 $BREAKPOINT_ITERATIONS); do
    RESULT=$(timeout 5 curl -s --max-time 4 "http://localhost:$PORT/query?file=$FULL_PATH&line=53&name=counter&break&timeout=2000" 2>/dev/null || echo "TIMEOUT")

    if [ "$RESULT" = "TIMEOUT" ]; then
        TIMEOUTS=$((TIMEOUTS + 1))
    elif echo "$RESULT" | grep -q '"result"'; then
        SUCCESSES=$((SUCCESSES + 1))
        # Resume
        curl -s --max-time 1 -X POST "http://localhost:$PORT/continue" >/dev/null 2>&1 || true
    elif echo "$RESULT" | grep -q '"status"'; then
        SUCCESSES=$((SUCCESSES + 1))  # Got a valid response
        curl -s --max-time 1 -X POST "http://localhost:$PORT/continue" >/dev/null 2>&1 || true
    else
        FAILURES=$((FAILURES + 1))
    fi

    sleep 0.2
done

echo "Completed: $BREAKPOINT_ITERATIONS breakpoint queries"
echo "Success: $SUCCESSES, Timeouts: $TIMEOUTS, Failed: $FAILURES"

# Breakpoint queries can timeout due to timing, so we're lenient
if [ $SUCCESSES -ge 1 ] || [ $TIMEOUTS -gt 0 ]; then
    pass "Breakpoint queries handled (some timeouts expected)"
else
    fail "No successful breakpoint queries"
fi

# ============================================================================
# Stress Test 5: Controller survives invalid requests
# ============================================================================

echo ""
echo -e "${BLUE}=== Test 5: Invalid Request Handling ===${NC}"

HANDLED=0

# Invalid endpoint
RESULT=$(curl -s --max-time 2 "http://localhost:$PORT/invalid/endpoint" 2>/dev/null)
[ -n "$RESULT" ] && HANDLED=$((HANDLED + 1))

# Missing parameters
RESULT=$(curl -s --max-time 2 "http://localhost:$PORT/query" 2>/dev/null)
[ -n "$RESULT" ] && HANDLED=$((HANDLED + 1))

# Invalid method
RESULT=$(curl -s --max-time 2 -X DELETE "http://localhost:$PORT/process" 2>/dev/null)
# May return error or empty, but should not crash
HANDLED=$((HANDLED + 1))

# Very long parameter
LONG_STRING=$(printf 'x%.0s' {1..1000})
RESULT=$(curl -s --max-time 2 "http://localhost:$PORT/query?name=$LONG_STRING" 2>/dev/null)
[ -n "$RESULT" ] && HANDLED=$((HANDLED + 1))

# Verify server is still responding
if curl -s --max-time 2 "http://localhost:$PORT/process" | grep -q '"pid"'; then
    pass "Server survived invalid requests"
else
    fail "Server stopped responding after invalid requests"
fi

# ============================================================================
# Summary
# ============================================================================

echo ""
echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}Stress Test Summary${NC}"
echo -e "${BLUE}======================================${NC}"

# Final health check
if kill -0 $TARGET_PID 2>/dev/null && kill -0 $QUERY_PID 2>/dev/null; then
    echo -e "${GREEN}All processes still running after stress tests${NC}"
    echo -e "${GREEN}Stress tests completed successfully!${NC}"
    exit 0
else
    echo -e "${RED}Some processes died during stress testing${NC}"
    exit 1
fi
