#!/usr/bin/env zsh
#

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"
source scripts/color.zsh
source scripts/developer-helpers.zsh

cbb --target ascii-chat


pkill -f "ascii-chat.*client" "lldb.*ascii-chat" || true

find src lib include web/web.ascii-chat.com/src | entr -rdc bash -c \
  'sleep 0.02 && cmake --build build --target ascii-chat && ./build/bin/ascii-chat --log-level debug client ws://localhost:27226'

