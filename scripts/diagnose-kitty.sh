#!/bin/bash
#
# Diagnose Kitty terminal TTY issues with ascii-chat keyboard input

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"

echo "=== Kitty Terminal Diagnostic ==="
echo ""
echo "Kitty info:"
echo "  TERM: $TERM"
echo "  KITTY_WINDOW_ID: ${KITTY_WINDOW_ID:-not set}"
echo "  KITTY_SOCKET_NAME: ${KITTY_SOCKET_NAME:-not set}"
echo "  KITTY: ${KITTY:-not set}"
echo ""

# Compile TTY test if needed
if [[ ! -f /tmp/tty-test ]]; then
  gcc -o /tmp/tty-test scripts/test-tty-detection.c
fi

echo "TTY detection in Kitty (should show TTY for all):"
/tmp/tty-test
echo ""

# Build ascii-chat
if [[ ! -f build/bin/ascii-chat ]]; then
  echo "Building ascii-chat..."
  cmake --preset default -B build >/dev/null 2>&1
  cmake --build build >/dev/null 2>&1
fi

echo "Running ascii-chat with debug logging..."
echo "Expected: 'TTY Status: stdin_tty=1 stdout_tty=1 interactive=1 keyboard_enabled=1'"
echo ""

# Run with log output
./build/bin/ascii-chat --log-level info --snapshot --snapshot-delay 0 mirror 2>&1 | grep -E "TTY Status|stdout_tty"

echo ""
echo "If stdout_tty=0, then isatty(STDOUT_FILENO) is returning false"
echo "This would explain why keyboard input is disabled."
