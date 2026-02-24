#!/usr/bin/env zsh

set -euo pipefail

set -x
ps aux \
  | rg "ascii-chat.*$1" | rg -v rg \
  | cut --delimiter=' ' -f5 \
  | xargs kill -USR1
