#!/bin/bash
#
# Debug keyboard help screen toggle functionality
#
# This script helps debug why the '?' key isn't opening the keyboard help screen.
# It runs mirror mode with:
# - --splash-screen=false (skip splash to see output immediately)
# - --log-level debug (verbose logging)
# - --grep filtering for keyboard and help-related messages
#
# Usage:
#   scripts/debug-keyboard-help.sh           # Run with debug output
#   scripts/debug-keyboard-help.sh -v        # Extra verbose (all logs)
#   scripts/debug-keyboard-help.sh --no-grep # No log filtering
#

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"

# Parse arguments
VERBOSE=false
USE_GREP=true
EXTRA_FLAGS=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -v|--verbose) VERBOSE=true; shift ;;
    --no-grep) USE_GREP=false; shift ;;
    *) EXTRA_FLAGS="$EXTRA_FLAGS $1"; shift ;;
  esac
done

# Build if needed
if [[ ! -f build/bin/ascii-chat ]]; then
  echo "Building ascii-chat..."
  cmake --preset default -B build >/dev/null 2>&1
  cmake --build build >/dev/null 2>&1
fi

echo ""
echo "===== DEBUG KEYBOARD HELP SCREEN ====="
echo ""
echo "IMPORTANT:"
echo "  - Keyboard input requires a TTY (terminal)"
echo "  - Running with --log-file causes piped output which disables keyboard"
echo "  - For interactive testing, run WITHOUT --log-file:"
echo ""
echo "    ./build/bin/ascii-chat mirror --splash-screen=false"
echo ""
echo "Instructions (for TTY mode):"
echo "  1. Press '?' to open the keyboard help screen"
echo "  2. Press ESC to close the help screen"
echo "  3. Press 'q' or Ctrl+C to exit the test"
echo ""
echo "This test uses --log-file which disables keyboard in non-TTY environments."
echo "Showing logs for analysis:"
echo ""

# Create temp log file for capture
LOG_FILE="/tmp/debug-keyboard-help-$$.log"

# Build command with global flags before the mode
CMD="./build/bin/ascii-chat --log-level debug --log-file $LOG_FILE"

# Add grep filtering if not verbose
if [[ "$VERBOSE" == "true" ]]; then
  echo "[VERBOSE MODE] Showing all debug logs..."
  echo ""
else
  echo "[FILTERED MODE] Showing keyboard and help screen logs..."
  echo "(Use -v flag for all logs)"
  echo "(Logs written to: $LOG_FILE)"
  echo ""
  # Filter for keyboard and help-related messages
  CMD="$CMD --grep '/KEYBOARD|help|KEY_|toggle/i'"
fi

# Add mode and other flags
CMD="$CMD mirror --splash-screen=false $EXTRA_FLAGS"

echo "Command: $CMD"
echo ""
echo "===== START OUTPUT ====="
echo ""

# Run the command
eval "$CMD"

echo ""
echo "===== END OUTPUT ====="
echo ""

# Show relevant log file contents if not verbose
if [[ "$VERBOSE" != "true" && -f "$LOG_FILE" ]]; then
  echo ""
  echo "===== KEYBOARD-RELATED LOGS FROM $LOG_FILE ====="
  echo ""
  grep -E "KEYBOARD|help|KEY_|toggle|initialized" "$LOG_FILE" || echo "(No matching logs found)"
  echo ""
  echo "===== ALL LOGS AVAILABLE AT ====="
  echo "$LOG_FILE"
fi

echo ""
echo "Test complete!"
