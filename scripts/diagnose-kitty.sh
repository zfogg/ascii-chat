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

# Check if running in Claude Code subprocess
if [[ -n "$CLAUDECODE" ]]; then
  echo "⚠️  Running in Claude Code subprocess - TTY detection will show non-TTY"
  echo "    (This is expected. Real test requires running in Kitty directly)"
  echo ""
fi

echo "Running ascii-chat in mirror mode (1 second timeout)..."
echo "Expected: 'TTY Status: stdin_tty=1 stdout_tty=1 interactive=1 keyboard_enabled=1'"
echo ""

# Run without snapshot to see actual keyboard status (timeout after 2 seconds)
# Remove old log file to ensure we get fresh output
rm -f mirror.log
timeout 2 ./build/bin/ascii-chat --log-file mirror.log --log-level info mirror >/dev/null 2>&1 &
sleep 3

echo "TTY Status from ascii-chat:"
grep "TTY Status" mirror.log 2>/dev/null || echo "(No TTY Status log found)"
echo ""

# Check if keyboard is enabled
if grep -q "keyboard_enabled=1" mirror.log 2>/dev/null; then
  echo "✓ Keyboard input ENABLED"
else
  echo "✗ Keyboard input DISABLED"
  if grep -q "stdout_tty=0" mirror.log 2>/dev/null; then
    echo "  Reason: stdout_tty=0 (isatty(STDOUT_FILENO) returned false)"
  fi
  if grep -q "stdin_tty=0" mirror.log 2>/dev/null; then
    echo "  Reason: stdin_tty=0 (isatty(STDIN_FILENO) returned false)"
  fi
fi
echo ""

# Show the full TTY detection line for debugging
echo "Full status line:"
grep "TTY Status" mirror.log 2>/dev/null | head -1 || echo "(No TTY Status found)"
echo ""

# Explain the results
echo "=== Interpretation ==="
if grep -q "keyboard_enabled=1" mirror.log 2>/dev/null; then
  echo "✓ Keyboard is WORKING - you can use '?', '/', 'q' in mirror mode"
  if grep -q "stdin_tty=0.*stdout_tty=0" mirror.log 2>/dev/null; then
    echo "  (Using /dev/tty fallback - TTY detection showing non-TTY)"
  fi
else
  echo "✗ Keyboard is NOT working - keyboard input will be disabled"
fi
