#!/bin/bash

set -e

cd "$(dirname "$0")/.."

echo "Type checking with TypeScript..."
vp run type-check

echo "Formatting check..."
vp run format:check

echo "Linting with eslint..."
vp run lint

echo "Building WASM..."
vp run wasm:build

echo "Building with vite..."
vp run vite:build

echo "✓ Build complete"
