#!/bin/bash

set -e

cd "$(dirname "$0")/.."

echo "Type checking with TypeScript..."
bun run type-check

echo "Formatting check with prettier..."
bun run format:check

echo "Linting with eslint..."
bun run lint

echo "Building with vite..."
bun run vite build

echo "Copying index.html to 404.html..."
cp dist/index.html dist/404.html

echo "✓ Build complete"
