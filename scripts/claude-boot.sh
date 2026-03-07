#!/usr/bin/env bash

repo_root="$(dirname "$0")/.."

cd "$repo_root"


if [ "$CLAUDE_CODE_REMOTE" != "true" ]; then
  ./scripts/install-deps.sh
  cmake --preset default
fi

cmake --build build

