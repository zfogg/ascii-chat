#!/usr/bin/env zsh

set -e

# Function to check and format web project
check_web_project() {
  local project_dir=$1
  local project_name=$(basename "$project_dir")

  if echo "$all_staged" | grep -q "^${project_dir#./}"; then
    echo "📝 Checking $project_name..."
    cd "$project_dir"

    # Format staged files and git add them
    echo "  ✓ Formatting..."
    staged_files=$(git diff --cached --name-only --diff-filter=ACM || true)
    if [[ -n "$staged_files" ]]; then
      bun run format > /dev/null 2>&1
      # Git add any staged files that were changed by formatting
      echo "$staged_files" | while read file; do
        local_file="${file#${project_dir#./}/}"
        if [[ -f "$local_file" ]] && git diff "$file" | grep -q .; then
          git add "$file"
        fi
      done
    fi

    # Type check and lint
    echo "  ✓ Type checking and linting..."
    bun run type-check || {
      echo "  ✗ Type check failed in $project_name"
      cd - > /dev/null
      return 1
    }
    bun run lint || {
      echo "  ✗ Linting failed in $project_name"
      cd - > /dev/null
      return 1
    }

  fi
}


check_web_project "$@"
