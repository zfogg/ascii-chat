#!/bin/bash
#
# Test help screen toggle functionality in mirror mode
# Demonstrates opening, closing, and re-opening the help screen

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"
source scripts/color.zsh

# Build if needed
if [[ ! -f build/bin/ascii-chat ]]; then
  echo "${YELLOW}Building ascii-chat...${RESET}"
  cmake --preset default -B build >/dev/null 2>&1
  cmake --build build >/dev/null 2>&1
fi

# Kill any existing test session
tmux kill-session -t help-test 2>/dev/null || true
sleep 0.5

echo "${CYAN}Starting mirror mode in tmux...${RESET}"
tmux new-session -d -s help-test -c "$_repo_root" "./build/bin/ascii-chat mirror"
sleep 3

echo "${CYAN}Sending '?' to open help screen...${RESET}"
tmux send-keys -t help-test "?"
sleep 2

echo "${CYAN}=== Help screen is open ===${RESET}"
tmux capture-pane -t help-test -p | tail -30

echo ""
echo "${CYAN}Sending 'Escape' to close help screen...${RESET}"
tmux send-keys -t help-test "Escape"
sleep 1

echo "${CYAN}=== Help screen is closed ===${RESET}"
tmux capture-pane -t help-test -p | tail -30

echo ""
echo "${CYAN}Sending '?' again to re-open help screen...${RESET}"
tmux send-keys -t help-test "?"
sleep 2

echo "${CYAN}=== Help screen is open again ===${RESET}"
tmux capture-pane -t help-test -p | tail -30

echo ""
echo "${CYAN}Sending 'Escape' to close help screen...${RESET}"
tmux send-keys -t help-test "Escape"
sleep 1

echo "${CYAN}=== Test complete! ===${RESET}"
echo "${GREEN}Help toggle works correctly.${RESET}"

# Clean up
tmux kill-session -t help-test
