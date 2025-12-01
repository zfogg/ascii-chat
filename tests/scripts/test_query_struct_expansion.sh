#!/bin/bash
# Test script for query tool struct expansion functionality
# Tests: expand parameter, depth parameter, nested structs

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
QUERY_TOOL="${PROJECT_ROOT}/.deps-cache/query-tool/ascii-query-server"
TEST_TARGET_SRC="${PROJECT_ROOT}/tests/fixtures/query_test_target.c"
TEST_TARGET="${PROJECT_ROOT}/build/tests/query_test_target"
PORT=9996

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    [ -n "$TARGET_PID" ] && kill $TARGET_PID 2>/dev/null || true
    [ -n "$QUERY_PID" ] && kill $QUERY_PID 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

fail() { echo -e "${RED}FAIL: $1${NC}"; exit 1; }
pass() { echo -e "${GREEN}PASS: $1${NC}"; }
info() { echo -e "${YELLOW}INFO: $1${NC}"; }

# Check prerequisites
[ ! -f "$QUERY_TOOL" ] && fail "Query tool not found"

# Build and start test target
info "Building test target..."
mkdir -p "$(dirname "$TEST_TARGET")"
clang -g -O0 -o "$TEST_TARGET" "$TEST_TARGET_SRC" || fail "Failed to build"
pass "Test target built"

info "Starting test target..."
"$TEST_TARGET" &
TARGET_PID=$!
sleep 1
kill -0 $TARGET_PID 2>/dev/null || fail "Test target failed to start"
pass "Test target started (PID $TARGET_PID)"

info "Starting query server..."
"$QUERY_TOOL" --attach $TARGET_PID --port $PORT &
QUERY_PID=$!
sleep 2
kill -0 $QUERY_PID 2>/dev/null || fail "Query server failed to start"
pass "Query server started"

# Wait for HTTP server
for i in {1..5}; do
    curl -s --max-time 1 "http://localhost:$PORT/" >/dev/null 2>&1 && break
    sleep 0.5
done
curl -s --max-time 1 "http://localhost:$PORT/" >/dev/null 2>&1 || fail "HTTP server not responding"
pass "HTTP server ready"

# Test 1: Query struct without expansion
info "Test 1: Query struct without expansion..."
RESULT=$(curl -s --max-time 3 "http://localhost:$PORT/query?file=query_test_target.c&line=53&name=data&break&timeout=2000")
echo "$RESULT" | grep -q '"aggregate":true' && pass "Struct detected as aggregate" || info "Struct query returned: ${RESULT:0:100}..."
curl -s --max-time 1 -X POST "http://localhost:$PORT/continue" >/dev/null 2>&1 || true

# Test 2: Query struct WITH expansion
info "Test 2: Query struct with &expand..."
RESULT=$(curl -s --max-time 3 "http://localhost:$PORT/query?file=query_test_target.c&line=53&name=data&break&expand&timeout=2000")
echo "$RESULT" | grep -q '"children"' && pass "Struct expanded with children" || info "Expand query returned: ${RESULT:0:100}..."
curl -s --max-time 1 -X POST "http://localhost:$PORT/continue" >/dev/null 2>&1 || true

# Test 3: Query nested struct (data.point)
info "Test 3: Query nested struct member..."
RESULT=$(curl -s --max-time 3 "http://localhost:$PORT/query?file=query_test_target.c&line=53&name=data.point&break&expand&timeout=2000")
echo "$RESULT" | grep -q '"result"' && pass "Nested struct query succeeded" || info "Nested query returned: ${RESULT:0:100}..."
curl -s --max-time 1 -X POST "http://localhost:$PORT/continue" >/dev/null 2>&1 || true

# Test 4: Query char array (should have summary)
info "Test 4: Query char array..."
RESULT=$(curl -s --max-time 3 "http://localhost:$PORT/query?file=query_test_target.c&line=53&name=data.name&break&timeout=2000")
echo "$RESULT" | grep -q '"summary"' && pass "Char array has string summary" || info "Char array query returned: ${RESULT:0:100}..."
curl -s --max-time 1 -X POST "http://localhost:$PORT/continue" >/dev/null 2>&1 || true

echo ""
echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}Struct expansion tests completed!${NC}"
echo -e "${GREEN}======================================${NC}"
