#!/usr/bin/env bash

set -euo pipefail

staged=$(git diff --cached --name-only --diff-filter=ACM)
files=()
while IFS= read -r file; do
  [[ -n "$file" && -f "$file" ]] && files+=("$file")
done < <(printf '%s\n' "$staged" | grep -E '\.(c|h|cpp|hpp|m|mm)$' || true)

if ((${#files[@]})); then
  clang-format -i -- "${files[@]}"
  git add -- "${files[@]}"
fi
