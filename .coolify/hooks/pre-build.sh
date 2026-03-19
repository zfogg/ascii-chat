#!/bin/bash
set -euo pipefail

echo ">>> Starting pre-build hook"
echo ">>> Working directory: $(pwd)"
echo ">>> Git version: $(git --version 2>/dev/null || echo 'git not found')"

# Check if .git exists
if [ ! -d .git ]; then
    echo ">>> ERROR: .git directory not found"
    echo ">>> Available files:"
    ls -la | head -20
    exit 1
fi

echo ">>> Initializing git submodules..."
git submodule update --init --recursive || {
    echo ">>> ERROR: git submodule update failed"
    exit 1
}

# Verify critical submodules were initialized
if [ ! -f deps/ascii-chat-deps/bearssl/Makefile ]; then
    echo ">>> ERROR: bearssl submodule not initialized"
    exit 1
fi

echo "✓ Git submodules initialized successfully"
echo ">>> Submodule check:"
ls -la deps/ascii-chat-deps/ | head -10
