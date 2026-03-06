#!/bin/bash

set -e

cd "$(dirname "$0")/.."

echo "Type checking with TypeScript..."

build_commands+=()
echo "Formatting check with prettier..."
to_format=$(bun prettier . --list-different || true)
if [ -n "$to_format" ]; then
  echo "Files need formatting:"
  echo "$to_format"
  echo "Running prettier --write..."
  build_commands=("bun run format")

fi

echo "Linting with eslint..."
build_commands=("bun run lint")

echo "Building with vite..."
build_commands=("bun run vite build")

# Build command array dynamically
if [ -z "$VERCEL" ]; then
  echo "Building manpage html..."
  build_commands+=("./scripts/manpage-build.sh")
fi

concurrently "${build_commands[@]}"

echo "Copying index.html to 404.html..."
cp dist/index.html dist/404.html

echo "Copying binary for API functions..."
if [ ! -f bin/ascii-chat-strings ]; then
  echo "ERROR: binary not found at bin/ascii-chat-strings"
  ls -la bin/ || echo "bin/ directory doesn't exist"
  exit 1
fi
mkdir -p api/bin
cp bin/ascii-chat-strings api/bin/ || exit 1
chmod +x api/bin/ascii-chat-strings
if [ ! -f api/bin/ascii-chat-strings ]; then
  echo "ERROR: binary copy failed"
  exit 1
fi
echo "Binary copied successfully to api/bin/ascii-chat-strings"

echo "✓ Build complete"

