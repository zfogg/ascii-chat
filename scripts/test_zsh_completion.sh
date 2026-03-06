#!/bin/bash
set -e

SESSION="zsh_comp_$$"
cleanup() { tmux kill-session -t "$SESSION" 2>/dev/null || true; }
trap cleanup EXIT

cd "$(dirname "$(dirname "$(readlink -f "$0")")")"
cmake --build build 2>&1 | tail -1

tmux new-session -d -s "$SESSION" -x 200 -y 50 zsh
sleep 0.1

tmux send-keys -t "$SESSION" "cd $(pwd) && mkdir -p ~/.zsh/completions && cp build/share/zsh/site-functions/_ascii_chat ~/.zsh/completions/" Enter
sleep 0.1

tmux send-keys -t "$SESSION" "export fpath=(~/.zsh/completions /usr/share/zsh/site-functions \$fpath) && autoload -Uz compinit && compinit -C 2>/dev/null" Enter
sleep 0.5

echo "TEST 1: Binary level (discovery mode with options)"
tmux send-keys -t "$SESSION" "./build/bin/ascii-chat --color-mode=" Tab
sleep 0.3

echo "./build/bin/ascii-chat --color-mode=<TAB>:"
tmux capture-pane -t "$SESSION" -p -S -2
tmux send-keys -t "$SESSION" "C-u"
sleep 0.1

echo ""
echo "TEST 2: Client mode with enum option"
tmux send-keys -t "$SESSION" "./build/bin/ascii-chat client --color-mode=" Tab
sleep 0.3

echo "./build/bin/ascii-chat client --color-mode=<TAB>:"
tmux capture-pane -t "$SESSION" -p -S -2
tmux send-keys -t "$SESSION" "C-u"
sleep 0.1

echo ""
echo "TEST 3: Server mode with enum option"
tmux send-keys -t "$SESSION" "./build/bin/ascii-chat server --color-mode=" Tab
sleep 0.3

echo "./build/bin/ascii-chat server --color-mode=<TAB>:"
tmux capture-pane -t "$SESSION" -p -S -2
