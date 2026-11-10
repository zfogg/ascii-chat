#!/usr/bin/env bash

set -euo pipefail

mapfile -t files < <(
  git diff --cached --name-only --diff-filter=ACM |
    grep -E '^\.github/workflows/.*\.(yml|yaml)$' || true
)

if ((${#files[@]})); then
  node node_modules/vite-plus/bin/vp fmt --threads=2 --no-error-on-unmatched-pattern \
    --config .oxfmtrc.json -- "${files[@]}"
  git add -- "${files[@]}"
fi
