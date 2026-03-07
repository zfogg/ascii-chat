#!/bin/bash

set -e

cd "$(dirname "$0")/.."

echo "Type checking with TypeScript..."
bun run type-check

echo "Formatting check with prettier..."
bun run format:check

echo "Linting with eslint..."
bun run lint

echo "Building WASM..."
bun run wasm:build || echo 'WASM build skipped (emscripten not available)'

echo "Building with vite..."
bun run vite build

echo "✓ Build complete"
