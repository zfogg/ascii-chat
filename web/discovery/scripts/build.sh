#!/bin/bash

set -e

cd "$(dirname "$0")/.."

echo "Generating key files from env vars..."
bash scripts/generate-keys.sh

echo "Type checking with TypeScript..."
pnpm run type-check

echo "Formatting check..."
pnpm run format:check

echo "Linting with eslint..."
pnpm run lint

echo "Building with vite..."
pnpm run vite:build

echo "Copying index.html to 404.html..."
cp dist/index.html dist/404.html

echo "✓ Build complete"
