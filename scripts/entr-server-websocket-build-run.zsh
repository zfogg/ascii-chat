#!/usr/bin/env zsh
#

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"
source scripts/color.zsh
source scripts/developer-helpers.zsh

cbb --target ascii-chat

pkill -x ascii-chat 2>/dev/null || true

exec lldb --batch -o "run" -o "bt" ./build/bin/ascii-chat -- --log-file server.log --log-level debug server 0.0.0.0 '::' --no-status-screen --websocket-port 27226
