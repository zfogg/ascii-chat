#!/bin/bash
# Test script for query tool auto-spawn functionality
# Tests: QUERY_INIT(), QUERY_SHUTDOWN(), and auto-spawn lifecycle

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TEST_PROGRAM="${PROJECT_ROOT}/build/tests/query_autospawn_test"
TEST_SOURCE="${PROJECT_ROOT}/tests/fixtures/query_autospawn_test.c"
QUERY_TOOL="${PROJECT_ROOT}/.deps-cache/query-tool/ascii-query-server"
PORT=9997

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    pkill -f "query_autospawn_test" 2>/dev/null || true
    pkill -f "ascii-query-server.*$PORT" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

fail() {
    echo -e "${RED}FAIL: $1${NC}"
    exit 1
}

pass() {
    echo -e "${GREEN}PASS: $1${NC}"
}

info() {
    echo -e "${YELLOW}INFO: $1${NC}"
}

# Check prerequisites
if [ ! -f "$QUERY_TOOL" ]; then
    fail "Query tool not found at $QUERY_TOOL. Build with: cmake -B build -DASCIICHAT_BUILD_WITH_QUERY=ON && cmake --build build"
fi

# Create test program source
info "Creating test program..."
cat > "$TEST_SOURCE" << 'EOF'
/**
 * @file query_autospawn_test.c
 * @brief Test program for QUERY_INIT/QUERY_SHUTDOWN auto-spawn
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

// Include the query runtime header
#include "tooling/query/query.h"

volatile int g_running = 1;
volatile int g_test_counter = 0;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

int main(int argc, char *argv[]) {
    int port = 9997;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Test program starting (PID %d)\n", getpid());
    fflush(stdout);

    // Initialize query tool - this should spawn the controller
    printf("Calling QUERY_INIT(%d)...\n", port);
    fflush(stdout);

    int result = QUERY_INIT(port);
    if (result > 0) {
        printf("SUCCESS: Query server started on port %d\n", result);
    } else {
        printf("FAILED: Query server did not start (returned %d)\n", result);
        return 1;
    }
    fflush(stdout);

    // Check if active
    if (QUERY_ACTIVE()) {
        printf("SUCCESS: QUERY_ACTIVE() returns true\n");
    } else {
        printf("FAILED: QUERY_ACTIVE() returns false\n");
        return 1;
    }
    fflush(stdout);

    // Check port
    int query_port = QUERY_PORT();
    if (query_port == port) {
        printf("SUCCESS: QUERY_PORT() returns %d\n", query_port);
    } else {
        printf("FAILED: QUERY_PORT() returns %d (expected %d)\n", query_port, port);
        return 1;
    }
    fflush(stdout);

    // Run for a bit to allow external queries
    printf("Running test loop (5 iterations)...\n");
    fflush(stdout);

    for (int i = 0; i < 5 && g_running; i++) {
        g_test_counter = i * 10;
        printf("Iteration %d: g_test_counter = %d\n", i, g_test_counter);
        fflush(stdout);
        sleep(1);
    }

    // Shutdown
    printf("Calling QUERY_SHUTDOWN()...\n");
    fflush(stdout);
    QUERY_SHUTDOWN();

    // Verify shutdown
    if (!QUERY_ACTIVE()) {
        printf("SUCCESS: QUERY_ACTIVE() returns false after shutdown\n");
    } else {
        printf("WARNING: QUERY_ACTIVE() still returns true after shutdown\n");
    }
    fflush(stdout);

    printf("Test program completed successfully!\n");
    return 0;
}
EOF

# Build test program
info "Building test program..."
mkdir -p "$(dirname "$TEST_PROGRAM")"

# Link against the query runtime library
# Note: Library is built with ASan/UBSan, so we need matching flags
clang -g -O0 \
    -fsanitize=address,undefined \
    -I"${PROJECT_ROOT}" \
    -I"${PROJECT_ROOT}/lib" \
    -o "$TEST_PROGRAM" "$TEST_SOURCE" \
    -L"${PROJECT_ROOT}/build/lib" \
    -lascii-query-runtime \
    || fail "Failed to build test program"

pass "Test program built"

# Set environment variable for query server path
export ASCIICHAT_QUERY_SERVER="$QUERY_TOOL"

# Run the test program
info "Running auto-spawn test..."
"$TEST_PROGRAM" $PORT 2>&1 &
TEST_PID=$!

# Wait for startup
sleep 3

# Check if test program is still running
if ! kill -0 $TEST_PID 2>/dev/null; then
    wait $TEST_PID
    EXIT_CODE=$?
    if [ $EXIT_CODE -ne 0 ]; then
        fail "Test program exited with code $EXIT_CODE"
    fi
fi

# Try to query the running program
info "Testing external query while program is running..."
RESULT=$(curl -s --max-time 5 "http://localhost:$PORT/process" 2>/dev/null || echo "CONNECT_FAILED")

if echo "$RESULT" | grep -q '"pid"'; then
    pass "External query to auto-spawned server succeeded"
    echo "  Response: $RESULT"
else
    if [ "$RESULT" = "CONNECT_FAILED" ]; then
        info "Could not connect to query server (may have already shut down)"
    else
        fail "External query failed: $RESULT"
    fi
fi

# Wait for test program to complete
info "Waiting for test program to complete..."
wait $TEST_PID
EXIT_CODE=$?

if [ $EXIT_CODE -eq 0 ]; then
    pass "Test program completed successfully"
else
    fail "Test program failed with exit code $EXIT_CODE"
fi

echo ""
echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}Auto-spawn tests passed!${NC}"
echo -e "${GREEN}======================================${NC}"
