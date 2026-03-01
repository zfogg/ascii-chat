#!/bin/bash
#
# Test keyboard input in ascii-chat using Kitty terminal
# Tests whether "?" (help) and other keyboard input works in interactive mode

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"

match='title:^ascii-chat-test'

session=help-test

kitty @ close-tab --match "$match" || true
tmux kill-session -t help-test || true && sleep 0.25

# Build if needed
if [[ ! -f build/bin/ascii-chat ]]; then
  echo "Building ascii-chat..."
  cmake --preset default -B build >/dev/null 2>&1
  cmake --build build >/dev/null 2>&1
fi

# Test 1: Launch mirror mode and send "?" to toggle help
echo "=== Test 1: Opening help screen with '?' ==="
kitty @ launch --type tab --title "ascii-chat-test" --keep-focus \
  zsh -ic "tmux new-session -c $_repo_root -t help-test"
sleep 3

tmux capture-pane -t "$session"

kitty @ send-text --match "$match" "./build/bin/ascii-chat mirror"
sleep 1
kitty @ send-key --match "$match" Enter
sleep 6
# Give it time to start
echo "=== Initial Mirror Display ==="
tmux capture-pane -p -t "$session"


# Send "?" key to toggle help - use tmux to send to the running process
echo "Sending '?' to toggle help..."
tmux send-keys -t "$session" "?"
sleep 0.5
echo "=== Help Screen (after pressing '?') ==="
tmux capture-pane -p -t "$session"


# Send "q" to quit
echo ""
echo "Sending 'q' to quit..."
tmux send-keys -t "$session" "q"
sleep 0.5
echo "=== After pressing 'q' ==="
tmux capture-pane -p -t "$session"


# Send Ctrl+C to force quit
echo ""
echo "Sending Ctrl+C to quit..."
tmux send-keys -t "$session" "C-c"

kitty @ close-tab --match "$match"

echo "=== Test complete ==="
