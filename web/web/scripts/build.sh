#!/bin/bash

set -e

cd "$(dirname "$0")/.."

echo "Type checking with TypeScript..."
pnpm run type-check

echo "Formatting check..."
pnpm run format:check

echo "Linting with eslint..."
pnpm run lint

echo "Building WASM..."
pnpm run wasm:build || echo 'WASM build skipped (emscripten not available)'

echo "Building with vite..."
pnpm run vite:build

echo "✓ Build complete"
