#!/usr/bin/env bash
# Test crypto handshake across different transport types
# Usage: ./test_crypto_handshake.sh [tcp|websocket|all]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BINARY="$BUILD_DIR/bin/ascii-chat"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Temporary files
SERVER_LOG=$(mktemp /tmp/server_test.XXXXXX.log)
CLIENT_LOG=$(mktemp /tmp/client_test.XXXXXX.log)

# Cleanup function
cleanup() {
  echo -e "\n${YELLOW}Cleaning up...${NC}"

  # Kill any remaining test processes
  pkill -x -f "ascii-chat" 2>/dev/null || true

  # Remove temporary files
  rm -f "$SERVER_LOG" "$CLIENT_LOG"
}

trap cleanup EXIT

# Test TCP crypto handshake
test_tcp_handshake() {
  local port=27230

  echo -e "${YELLOW}Testing TCP crypto handshake...${NC}"

  # Start server in background
  timeout 10 "$BINARY" --log-level debug server --port "$port" > "$SERVER_LOG" 2>&1 &
  local server_pid=$!

  # Wait for server to start
  sleep 0.25

  # Check if server is running
  if ! kill -0 "$server_pid" 2>/dev/null; then
    echo -e "${RED}✗ Server failed to start${NC}"
    cat "$SERVER_LOG"
    return 1
  fi

  # Run client in snapshot mode (exits after 2 seconds)
  timeout 8 "$BINARY" --log-level debug client "localhost:$port" --snapshot --snapshot-delay 2 > "$CLIENT_LOG" 2>&1 || true

  # Kill server
  kill "$server_pid" 2>/dev/null || true
  wait "$server_pid" 2>/dev/null || true

  # Check results
  if grep -q "Crypto handshake completed successfully" "$SERVER_LOG"; then
    echo -e "${GREEN}✓ TCP crypto handshake: PASSED${NC}"

    # Show handshake flow
    echo -e "\n${YELLOW}Server handshake flow:${NC}"
    grep -E "(crypto|handshake|KEY_EXCHANGE|AUTH_)" "$SERVER_LOG" | grep -v "status_screen" | head -10

    echo -e "\n${YELLOW}Client handshake flow:${NC}"
    grep -E "(crypto|handshake|KEY_EXCHANGE|AUTH_|[Cc]onnect)" "$CLIENT_LOG" | head -10

    return 0
  else
    echo -e "${RED}✗ TCP crypto handshake: FAILED${NC}"
    echo -e "\n${YELLOW}Server log:${NC}"
    tail -30 "$SERVER_LOG"
    echo -e "\n${YELLOW}Client log:${NC}"
    tail -30 "$CLIENT_LOG"
    return 1
  fi
}

# Test WebSocket crypto handshake
test_websocket_handshake() {
  local port=27231

  echo -e "${YELLOW}Testing WebSocket crypto handshake...${NC}"
  echo -e "${YELLOW}Note: WebSocket client support requires full implementation${NC}"

  # Start server with WebSocket support
  timeout 10 "$BINARY" --log-level debug server --port "$port" > "$SERVER_LOG" 2>&1 &
  local server_pid=$!

  # Wait for server to start
  sleep 0.25

  # Check if server is running
  if ! kill -0 "$server_pid" 2>/dev/null; then
    echo -e "${RED}✗ Server failed to start${NC}"
    return 1
  fi

  # WebSocket client test would go here
  # For now, just verify server can handle WebSocket connections

  # Kill server
  kill "$server_pid" 2>/dev/null || true
  wait "$server_pid" 2>/dev/null || true

  echo -e "${YELLOW}⊘ WebSocket crypto handshake: SKIPPED (requires WebSocket client)${NC}"
  return 0
}

# Main execution
main() {
  local test_type="${1:-tcp}"

  echo -e "${GREEN}=== Crypto Handshake Test Suite ===${NC}"
  echo -e "Binary: $BINARY"
  echo -e "Test type: $test_type\n"

  # Check if binary exists
  if [[ ! -x "$BINARY" ]]; then
    echo -e "${RED}Error: Binary not found at $BINARY${NC}"
    echo "Run: cmake --build build"
    exit 1
  fi

  local failed=0

  case "$test_type" in
    tcp)
      test_tcp_handshake || failed=$((failed + 1))
      ;;
    websocket)
      test_websocket_handshake || failed=$((failed + 1))
      ;;
    all)
      test_tcp_handshake || failed=$((failed + 1))
      test_websocket_handshake || failed=$((failed + 1))
      ;;
    *)
      echo -e "${RED}Invalid test type: $test_type${NC}"
      echo "Usage: $0 [tcp|websocket|all]"
      exit 1
      ;;
  esac

  echo -e "\n${GREEN}=== Test Summary ===${NC}"
  if [[ $failed -eq 0 ]]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
  else
    echo -e "${RED}$failed test(s) failed${NC}"
    exit 1
  fi
}

main "$@"
