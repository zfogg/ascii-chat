#!/usr/bin/env bash
set -euo pipefail

# Final test for splash screen fix
# Verifies splash screen has EXACTLY 6 FIXED lines + logs fill the rest

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BINARY="$BUILD_DIR/bin/ascii-chat"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}=========================================="
echo "Splash Screen Final Test"
echo "Testing: EXACTLY 6 lines + fixed log area"
echo "==========================================${NC}"
echo

if [[ ! -f "$BINARY" ]]; then
    echo -e "${RED}ERROR: Binary not found at $BINARY${NC}"
    exit 1
fi

cleanup() {
    killall ascii-chat 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo -e "${YELLOW}Killing any existing processes...${NC}"
killall ascii-chat 2>/dev/null || true
sleep 1

echo -e "${YELLOW}Starting server...${NC}"
"$BINARY" --quiet server &
sleep 2

echo -e "${YELLOW}Running client and observing splash (5 seconds)...${NC}"
echo -e "${CYAN}Watch for:${NC}"
echo "  1. ASCII art appears centered (4 logo lines)"
echo "  2. Blank line"
echo "  3. Tagline centered"
echo "  4. Logs fill remaining space below (same height every frame)"
echo "  5. ASCII art NEVER moves vertically or horizontally"
echo

timeout 5 "$BINARY" --log-level debug client || true

echo
echo -e "${GREEN}✓ Manual test complete${NC}"
echo
echo -e "${CYAN}Did you observe:${NC}"
echo "  [ ] ASCII art stayed centered (same position every frame)"
echo "  [ ] Logs filled the same height every time"
echo "  [ ] No jumping or flashing"
echo "  [ ] Terminal did NOT scroll"
echo
