#!/usr/bin/env zsh
#

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"
source scripts/color.zsh
source scripts/developer-helpers.zsh

cbb --target ascii-chat


pkill -f "ascii-chat.*server 0.0.0.0" || true

find src lib include web/web.ascii-chat.com/src | entr -rdc bash -c \
  'cmake --build build --target ascii-chat && ./build/bin/ascii-chat --log-file server.log --log-level debug server 0.0.0.0 "::" --status-screen --websocket-port 27226'

