#!/bin/bash

set -e

cd "$(dirname "$0")/.."

echo "Type checking with TypeScript..."
bun run tsc --noEmit

echo "Formatting check with prettier..."
to_format=$(bun prettier . --list-different || true)
if [ -n "$to_format" ]; then
  echo "Files need formatting:"
  echo "$to_format"
  echo "Running prettier --write..."
  bun run format
fi

echo "Linting with eslint..."
bun run lint

echo "Building with vite..."
bun run vite build

echo "Copying index.html to 404.html..."
cp dist/index.html dist/404.html

echo "✓ Build complete"
