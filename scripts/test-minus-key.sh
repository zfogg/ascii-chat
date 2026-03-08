#!/bin/bash
#
# Test the minus key functionality with tmux and capture logs
#

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"

if [[ ! -f build/bin/ascii-chat ]]; then
  cmake --preset default -B build >/dev/null 2>&1
  cmake --build build >/dev/null 2>&1
fi

SESSION="minus-test-$$"
LOG_FILE="/tmp/minus-key-test-$$.log"

# Kill old session
tmux kill-session -t "$SESSION" 2>/dev/null || true
sleep 0.2

# Start mirror in tmux, redirect stderr to log file (where logs go)
tmux new-session -d -s "$SESSION" -x 120 -y 40 "cd '$_repo_root' && ./build/bin/ascii-chat mirror --splash-screen=false 2> '$LOG_FILE'"
sleep 3

echo "=== Testing keyboard input ==="
echo ""

# Send minus key
echo "Sending '-' key..."
tmux send-keys -t "$SESSION" "-"
sleep 1

# Send 0 key
echo "Sending '0' key..."
tmux send-keys -t "$SESSION" "0"
sleep 1

# Send ? key
echo "Sending '?' key..."
tmux send-keys -t "$SESSION" "?"
sleep 1

# Cleanup
tmux send-keys -t "$SESSION" "C-c"
sleep 1
tmux kill-session -t "$SESSION" 2>/dev/null || true

echo ""
echo "=== Keyboard input logs (what keycodes were actually received) ==="
echo ""

if [[ -f "$LOG_FILE" ]]; then
  if grep -q "KEYBOARD INPUT\|UNKNOWN KEY" "$LOG_FILE"; then
    echo "Found keyboard logs:"
    grep "KEYBOARD INPUT\|UNKNOWN KEY" "$LOG_FILE"
  else
    echo "(No keyboard input logs found in stderr)"
    echo ""
    echo "First 50 lines of log file:"
    head -50 "$LOG_FILE"
  fi
  echo ""
  echo "Full log file: $LOG_FILE"
else
  echo "ERROR: Log file not created at $LOG_FILE"
fi
