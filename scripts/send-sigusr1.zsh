#!/usr/bin/env zsh

set -euo pipefail

ps aux \
  | rg "ascii-chat.*$1" | rg -v rg \
  | cut --delimiter=' ' -f5 \
  | xargs kill -USR1
