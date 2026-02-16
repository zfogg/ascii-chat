#!/usr/bin/env zsh
#

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"
source scripts/color.zsh
source scripts/developer-helpers.zsh

cbb --target ascii-chat


pkill -f "ascii-chat.*server 0.0.0.0" "lldb.*ascii-chat" || true

find src lib include web/web.ascii-chat.com/src | entr -adc bash -c \
  'cmake --build build --target ascii-chat && lldb -o "process handle -p true -s true -n false SIGTERM SIGINT SIGABRT SIGSEGV" -o "run" ./build/bin/ascii-chat -- --log-file server.log --log-level debug server 0.0.0.0 "::" --no-status-screen --websocket-port 27226'

