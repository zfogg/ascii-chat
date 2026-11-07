#!/usr/bin/env zsh

set -e

# Check if any web files are staged
if echo "$all_staged" | grep -q "^web/"; then
  echo "📝 Checking web projects..."
  cd web
  node node_modules/vite-plus/bin/vp check
  cd - > /dev/null
fi
