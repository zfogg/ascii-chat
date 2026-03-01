#!/bin/bash
#
# Test keyboard input in ascii-chat using Kitty terminal
# Tests whether "?" (help) and other keyboard input works in interactive mode

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"

# Build if needed
if [[ ! -f build/bin/ascii-chat ]]; then
  echo "Building ascii-chat..."
  cmake --preset default -B build >/dev/null 2>&1
  cmake --build build >/dev/null 2>&1
fi

# Test 1: Launch mirror mode and send "?" to toggle help
echo "=== Test 1: Opening help screen with '?' ==="
kitty @ launch --type tab --title "ascii-chat-test" --keep-focus \
  bash -c "cd '$_repo_root' && ./build/bin/ascii-chat --log-level info mirror"

# Give it time to start
sleep 2

# Send "?" key to toggle help
echo "Sending '?' to toggle help..."
kitty @ send-text --all "?"
sleep 1

# Send "q" to quit
echo "Sending 'q' to quit..."
kitty @ send-text --all "q"

echo "=== Test complete ==="
