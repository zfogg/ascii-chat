#!/bin/bash

set -e

cd "$(dirname "$0")/.."

echo "Building with vite..."
bun run vite build

echo "Copying index.html to 404.html..."
cp dist/index.html dist/404.html

echo "✓ Build complete"
