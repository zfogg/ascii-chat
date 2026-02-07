#!/bin/bash


set -e

cd "$(dirname "$0")/.."

# Skip man page build in Vercel (man pages are pre-built and committed)
if [ -z "$VERCEL" ]; then
  ./scripts/manpage-build.sh
  git add public/ascii-chat-man1.html public/ascii-chat-man5.html
fi

echo "Formatting code with prettier..."
bun run format
to_format=$(bun prettier . --list-different || true)
if [ -n "$to_format" ]; then
  git add $to_format
fi

echo "Building with vite..."
bun run vite build

echo "Copying index.html to 404.html..."
cp dist/index.html dist/404.html


echo "✓ Build complete"

