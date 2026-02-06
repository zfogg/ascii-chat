#!/usr/bin/env bash
set -euo pipefail

# Test that splash screen ASCII art stays FIXED at top
# Verifies no jumping/moving as logs appear

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BINARY="$BUILD_DIR/bin/ascii-chat"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}=========================================="
echo "Splash Screen Animation Test"
echo "Testing: ASCII art stays FIXED at top"
echo "==========================================${NC}"
echo

if [[ ! -f "$BINARY" ]]; then
    echo -e "\033[0;31mERROR: Binary not found at $BINARY${NC}"
    exit 1
fi

cleanup() {
    killall ascii-chat 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo -e "${YELLOW}Starting server in background...${NC}"
"$BINARY" --quiet server &
sleep 2

echo -e "${YELLOW}Running client with splash (watch for smooth animation)...${NC}"
echo -e "${CYAN}Expected behavior:${NC}"
echo "  - ASCII art appears at TOP of screen"
echo "  - Rainbow colors animate smoothly"
echo "  - Logs scroll below ASCII art"
echo "  - ASCII art does NOT move/jump"
echo "  - Terminal does NOT scroll past bottom"
echo
echo -e "${CYAN}Press Ctrl+C when you've verified the animation is smooth${NC}"
echo

"$BINARY" --log-level debug client -S -D 5

echo
echo -e "${GREEN}✓ Test complete${NC}"
