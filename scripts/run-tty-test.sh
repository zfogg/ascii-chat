#!/bin/bash
#
# Run TTY detection test in different contexts to diagnose keyboard input issues

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"

# Compile test
gcc -o /tmp/tty-test scripts/test-tty-detection.c

echo "=== TTY Detection Test Results ==="
echo ""

echo "1. Direct interactive run (should show all TTYs):"
/tmp/tty-test
echo ""

echo "2. Via tmux send-keys (should show piped stdout):"
tmux new-session -d -t tty-test
tmux send-keys -t tty-test "/tmp/tty-test" Enter
sleep 1
tmux capture-pane -t tty-test -p
tmux kill-session -t tty-test
echo ""

echo "3. Via pipe (should show piped stdout):"
/tmp/tty-test | cat
echo ""

echo "4. With stdout redirected (should show piped stdout):"
/tmp/tty-test > /tmp/tty-output.txt 2>&1
cat /tmp/tty-output.txt
echo ""

# Cleanup
rm -f /tmp/tty-test /tmp/tty-output.txt
