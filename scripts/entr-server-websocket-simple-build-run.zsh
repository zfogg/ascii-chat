#!/usr/bin/env zsh

set -e

cd "$(dirname "$0")/.."
source scripts/color.zsh
source scripts/developer-helpers.zsh

cbb --target ascii-chat
pkill -f 'ascii-chat.*server 0.0.0.0' || true

./build/bin/ascii-chat --log-file server.log --log-level debug \
  server 0.0.0.0 :: \
  --websocket-port 27226 \
  "$@"
  #--no-status-screen "$@"
