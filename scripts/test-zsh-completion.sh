#!/bin/bash
# Test zsh completion in tmux to see actual "corrections (errors: N)" messages

set -e

cd "$(dirname "$(dirname "$(realpath "$0")")")" || exit 1

echo "Testing zsh completion in tmux..."
echo "This will show actual zsh completion errors with messages"
echo ""

# Kill any existing test session
tmux kill-session -t zsh-completion-test 2>/dev/null || true
sleep 0.2

# Create new session
tmux new-session -d -s zsh-completion-test -x 200 -y 50

# Send setup commands
tmux send-keys -t zsh-completion-test "cd '$PWD'" Enter
sleep 0.2

tmux send-keys -t zsh-completion-test "fpath=(./build/share/zsh/site-functions \$fpath); compinit -U; clear" Enter
sleep 0.5

# Test 1: ascii-chat --sho<tab>
echo "=== TEST 1: ascii-chat --sho<tab> ==="
tmux send-keys -t zsh-completion-test "./build/bin/ascii-chat --sho" Tab
sleep 0.2
tmux send-keys -t zsh-completion-test "Escape"
tmux send-keys -t zsh-completion-test "C-u"
tmux capture-pane -t zsh-completion-test -p | rg corrections \
  && echo "🔴 CORRECTIONS FOUND! TEST FAILED"
tmux capture-pane -t zsh-completion-test -p | rg audio \
  && echo "🟢 AUDIO FOUND! TEST PASSED" \
  || echo "🔴 NO 'audio' options found! TEST FAILED"
tmux capture-pane -t zsh-completion-test -p

# clear
tmux send-keys -t zsh-completion-test "clear" Enter
sleep 0.1

echo ""
echo "=== TEST 2: ascii-chat discovery --<tab> ==="
tmux send-keys -t zsh-completion-test "./build/bin/ascii-chat discovery --" Tab
sleep 0.2
tmux send-keys -t zsh-completion-test "Escape"
tmux send-keys -t zsh-completion-test "C-u"
tmux capture-pane -t zsh-completion-test -p

# clear
tmux send-keys -t zsh-completion-test "clear" Enter
sleep 0.1

echo ""
echo "=== TEST 3: ascii-chat client --<tab> ==="
tmux send-keys -t zsh-completion-test "./build/bin/ascii-chat client --" Tab
sleep 0.2
tmux send-keys -t zsh-completion-test "Escape"
tmux send-keys -t zsh-completion-test "C-u"
tmux capture-pane -t zsh-completion-test -p

echo ""
echo "Cleaning up..."
tmux kill-session -t zsh-completion-test 2>/dev/null || true

echo "Done. Check output above for '-- corrections (errors: N) --' messages"
