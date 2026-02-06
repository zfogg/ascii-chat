#!/usr/bin/env bash
set -euo pipefail

# Test script for session screens (status + splash)
# Verifies both screens display logs without flashing

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BINARY="$BUILD_DIR/bin/ascii-chat"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=========================================="
echo "Session Screens Test Suite"
echo "=========================================="
echo

# Check binary exists
if [[ ! -f "$BINARY" ]]; then
    echo -e "${RED}ERROR: Binary not found at $BINARY${NC}"
    echo "Run: cmake --build build"
    exit 1
fi

# ============================================================================
# Test 1: Status Screen (Server Mode)
# ============================================================================
echo -e "${YELLOW}Test 1: Server Status Screen${NC}"
echo "Starting server with status screen..."

# Start server in background
"$BINARY" --log-level debug server --status-screen &
SERVER_PID=$!

# Wait for server to initialize
sleep 2

# Connect client to generate logs
echo "Connecting client to generate logs..."
"$BINARY" client --snapshot --snapshot-delay 0 --address localhost --port 27224 &
CLIENT_PID=$!

# Let it run for 5 seconds
sleep 5

# Kill client
kill $CLIENT_PID 2>/dev/null || true
wait $CLIENT_PID 2>/dev/null || true

# Kill server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo -e "${GREEN}✓ Status screen test complete${NC}"
echo "  Expected: Status header at top, logs scrolling below, no flashing"
echo

# ============================================================================
# Test 2: Splash Screen (Client Mode)
# ============================================================================
echo -e "${YELLOW}Test 2: Client Splash Screen${NC}"
echo "Starting client with splash screen..."

# Start server silently (no status screen)
"$BINARY" --quiet server &
SERVER_PID=$!
sleep 1

# Start client with splash (will show during connection)
echo "Client should show splash during connection..."
"$BINARY" --log-level debug client --address localhost --port 27224 --snapshot --snapshot-delay 1 &
CLIENT_PID=$!

# Let splash display for a few seconds
sleep 3

# Kill client
kill $CLIENT_PID 2>/dev/null || true
wait $CLIENT_PID 2>/dev/null || true

# Kill server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo -e "${GREEN}✓ Splash screen test complete${NC}"
echo "  Expected: Animated ASCII art at top, logs below, no flashing"
echo

# ============================================================================
# Test 3: Mirror Mode Splash
# ============================================================================
echo -e "${YELLOW}Test 3: Mirror Mode Splash Screen${NC}"
echo "Testing splash in mirror mode..."

"$BINARY" --log-level debug mirror --snapshot --snapshot-delay 2 || true

echo -e "${GREEN}✓ Mirror splash test complete${NC}"
echo "  Expected: Splash shown for ~2 seconds with initialization logs"
echo

# ============================================================================
# Summary
# ============================================================================
echo "=========================================="
echo -e "${GREEN}All tests complete!${NC}"
echo "=========================================="
echo
echo "Manual verification checklist:"
echo "  [ ] Status screen: header stays fixed, logs scroll below"
echo "  [ ] Status screen: no flashing when logs appear"
echo "  [ ] Splash screen: ASCII art at top, logs below"
echo "  [ ] Splash screen: no flashing during animation"
echo "  [ ] Both screens: terminal doesn't scroll past bottom"
echo
