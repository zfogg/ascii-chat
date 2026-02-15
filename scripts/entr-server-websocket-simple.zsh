#!/usr/bin/env zsh

set -e

cd "$(dirname "$0")/.."
pwd
source scripts/color.zsh
source scripts/developer-helpers.zsh

#cpd

set -x
find src include lib web/web.ascii-chat.com/src | entr -rc ./scripts/entr-server-websocket-simple-build-run.zsh "$@"
set +x
