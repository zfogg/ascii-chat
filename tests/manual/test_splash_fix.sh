#!/usr/bin/env bash
set -euo pipefail

# Comprehensive test for splash screen log display fix
# Tests that both server and client display splash screens with logs
# scrolling properly without scrolling the terminal

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BINARY="$BUILD_DIR/bin/ascii-chat"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}=========================================="
echo "Splash Screen Fix Integration Test"
echo "==========================================${NC}"
echo

# Check binary exists
if [[ ! -f "$BINARY" ]]; then
    echo -e "${RED}ERROR: Binary not found at $BINARY${NC}"
    echo "Run: cmake --build build"
    exit 1
fi

# Cleanup function
cleanup() {
    echo
    echo -e "${YELLOW}Cleaning up...${NC}"
    killall ascii-chat 2>/dev/null || true
    sleep 1
}

# Set trap for cleanup
trap cleanup EXIT INT TERM

# Step 1: Kill any existing instances
echo -e "${YELLOW}Step 1: Killing any existing ascii-chat processes...${NC}"
killall ascii-chat 2>/dev/null || true
sleep 1
echo -e "${GREEN}✓ Cleanup complete${NC}"
echo

# Step 2: Start server with status screen and 10 second timeout
echo -e "${YELLOW}Step 2: Starting server with status screen (10s timeout)...${NC}"
echo -e "${CYAN}Command: $BINARY --log-level debug server${NC}"
echo -e "${CYAN}(Status screen is enabled by default)${NC}"
echo
timeout 10 "$BINARY" --log-level debug server &
SERVER_PID=$!
echo -e "${GREEN}✓ Server started (PID: $SERVER_PID)${NC}"
echo

# Wait for server to initialize
echo -e "${YELLOW}Waiting 2 seconds for server initialization...${NC}"
sleep 2
echo -e "${GREEN}✓ Server ready${NC}"
echo

# Step 3: Run client with splash screen in snapshot mode
echo -e "${YELLOW}Step 3: Running client with splash screen (snapshot -S, 2s delay -D 2)...${NC}"
echo -e "${CYAN}Command: $BINARY --log-level debug client -S -D 2${NC}"
echo
echo -e "${CYAN}======== CLIENT OUTPUT (with splash + logs) ========${NC}"
"$BINARY" --log-level debug client -S -D 2
CLIENT_EXIT=$?
echo -e "${CYAN}=====================================================${NC}"
echo

if [ $CLIENT_EXIT -eq 0 ]; then
    echo -e "${GREEN}✓ Client completed successfully${NC}"
else
    echo -e "${RED}✗ Client exited with code $CLIENT_EXIT${NC}"
fi
echo

# Wait a moment for server to process
sleep 1

# Step 4: Check server is still running
echo -e "${YELLOW}Step 4: Checking server status...${NC}"
if kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${GREEN}✓ Server still running (will timeout in ~5 seconds)${NC}"
else
    echo -e "${YELLOW}⚠ Server already stopped${NC}"
fi
echo

# Step 5: Wait for server timeout
echo -e "${YELLOW}Step 5: Waiting for server timeout...${NC}"
wait $SERVER_PID 2>/dev/null || true
echo -e "${GREEN}✓ Server timeout complete${NC}"
echo

# Final summary
echo -e "${CYAN}=========================================="
echo "Test Complete!"
echo "==========================================${NC}"
echo
echo -e "${GREEN}Verification Checklist:${NC}"
echo "  ${CYAN}SERVER (status screen):${NC}"
echo "    [ ] Status box appeared at top with server info"
echo "    [ ] Logs scrolled below status box"
echo "    [ ] No flashing (screen stayed stable)"
echo "    [ ] Terminal did not scroll past bottom"
echo
echo "  ${CYAN}CLIENT (splash screen):${NC}"
echo "    [ ] Rainbow animated ASCII art appeared"
echo "    [ ] Debug logs appeared below ASCII art"
echo "    [ ] No flashing during animation"
echo "    [ ] Terminal did not scroll past bottom"
echo "    [ ] Logs scrolled smoothly as client connected"
echo
echo -e "${YELLOW}If all checkboxes would be checked, the splash screen fix is working!${NC}"
echo
