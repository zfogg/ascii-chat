#!/bin/bash

set -e

cd "$(dirname "$0")/.."

vp check

echo "Building with vite..."
build_commands=("pnpm run vite:build")

# Build manpage html if not on Vercel and cmake/mandoc are available
if [ -z "$VERCEL" ] && command -v cmake &> /dev/null && command -v mandoc &> /dev/null; then
  echo "Building manpage html..."
  build_commands+=("./scripts/manpage-build.sh")
fi

concurrently "${build_commands[@]}"

echo "Copying index.html to 404.html..."
cp dist/index.html dist/404.html

echo "✓ Build complete"

