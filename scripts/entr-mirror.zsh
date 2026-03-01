#!/usr/bin/env zsh
#

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"
source scripts/color.zsh
source scripts/developer-helpers.zsh

cbb --target ascii-chat

pkill -9 -x "ascii-chat" || true
sleep 0.5

find src lib include | entr -rdnc bash -c \
  "cmake --build build --target ascii-chat && ./build/bin/ascii-chat --log-level debug mirror $@"

