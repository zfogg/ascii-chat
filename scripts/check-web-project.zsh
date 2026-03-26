#!/usr/bin/env zsh

set -e

# Check if any web files are staged
if echo "$all_staged" | grep -q "^web/"; then
  echo "📝 Checking web projects..."
  cd web
  vp check --fix
  cd - > /dev/null

  # Git add any files that were changed by formatting
  staged_files=$(git diff --cached --name-only --diff-filter=ACM | grep "^web/" || true)
  if [[ -n "$staged_files" ]]; then
    echo "$staged_files" | while read file; do
      if [[ -f "$file" ]] && git diff -- "$file" | grep -q .; then
        git add "$file"
        echo "  ✦ Auto-formatted: $file"
      fi
    done
  fi
fi
