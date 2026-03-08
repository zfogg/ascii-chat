#!/bin/bash
#
# Debug keyboard help screen toggle functionality with tmux
#
# Tests keyboard input in an interactive tmux session
#
# Usage:
#   scripts/debug-keyboard-help.sh

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"

if [[ ! -f build/bin/ascii-chat ]]; then
  cmake --preset default -B build >/dev/null 2>&1
  cmake --build build >/dev/null 2>&1
fi

SESSION="kbd-test-$$"

# Kill old session
tmux kill-session -t "$SESSION" 2>/dev/null || true
sleep 0.2

# Start mirror in tmux (NO PIPING - keep stdout as TTY)
tmux new-session -d -s "$SESSION" -x 120 -y 40 "cd '$_repo_root' && ./build/bin/ascii-chat mirror --splash-screen=false"
sleep 3

echo "=== Testing keyboard help screen with tmux ==="
echo ""

# Send ? key
echo "Sending '?' key..."
tmux send-keys -t "$SESSION" "?"
sleep 1.5

echo "=== Screen after '?' (should show help) ==="
tmux capture-pane -t "$SESSION" -p | head -40

echo ""
echo "Sending ESC key..."
tmux send-keys -t "$SESSION" "Escape"
sleep 1

echo "=== Screen after ESC (back to ASCII art) ==="
tmux capture-pane -t "$SESSION" -p | head -40

# Cleanup
tmux send-keys -t "$SESSION" "q"
sleep 0.2
tmux kill-session -t "$SESSION" 2>/dev/null || true

echo ""
echo "Test complete"
