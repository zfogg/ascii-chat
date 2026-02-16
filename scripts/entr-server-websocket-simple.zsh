#!/usr/bin/env zsh

set -e

cd "$(dirname "$0")/.."
source scripts/color.zsh
source scripts/developer-helpers.zsh

#cpd

find src include lib web/web.ascii-chat.com/src | entr -dc ./scripts/entr-server-websocket-simple-build-run.zsh "$@"
