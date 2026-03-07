#!/usr/bin/env zsh

set -e

# Get list of staged files
all_staged=$(git diff --cached --name-only --diff-filter=ACM || true)

# Format C/C++ files
c_files=$(echo "$all_staged" | grep -E '\.(c|h|cpp|hpp|m|mm)$' || true)
if [[ -n "$c_files" ]]; then
  for file in $c_files; do
    if [[ -f "$file" ]]; then
      clang-format -i "$file"
    fi
  done

  formatted_files=""
  for file in $c_files; do
    if [[ -f "$file" ]] && git diff --name-only | grep -q "^$file$"; then
      formatted_files="$formatted_files $file"
      git add "$file"
    fi
  done

  if [[ -n "$formatted_files" ]]; then
    echo "🎨 Code formatting applied to:$formatted_files"
  fi
fi
