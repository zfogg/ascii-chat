#!/usr/bin/env bash

set -euo pipefail

staged=$(git diff --cached --name-only --diff-filter=ACM)
while IFS= read -r file; do
  [[ -n "$file" && -f "$file" ]] || continue
  clang-format -i -- "$file"
  git add -- "$file"
done < <(printf '%s\n' "$staged" | grep -E '\\.(c|h|cpp|hpp|m|mm)$' || true)
