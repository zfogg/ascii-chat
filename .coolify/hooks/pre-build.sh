#!/bin/bash
set -euo pipefail

echo ">>> Starting pre-build hook"
echo ">>> Working directory: $(pwd)"
echo ">>> Git version: $(git --version 2>/dev/null || echo 'git not found')"

# Check if .git exists
if [ -d .git ]; then
    echo ">>> Using git submodule update (git repo detected)"
    git submodule update --init --recursive || {
        echo ">>> ERROR: git submodule update failed"
        exit 1
    }
else
    echo ">>> No .git directory (Coolify build context). Cloning submodules directly..."

    # Ensure deps directory exists
    mkdir -p deps/ascii-chat-deps

    # Clone submodules directly from GitHub
    if [ ! -d deps/ascii-chat-deps/bearssl ]; then
        echo ">>> Cloning bearssl..."
        git clone --depth 1 https://github.com/zfogg/bearssl.git deps/ascii-chat-deps/bearssl || exit 1
    fi

    if [ ! -d deps/ascii-chat-deps/libsodium-bcrypt-pbkdf ]; then
        echo ">>> Cloning libsodium-bcrypt-pbkdf..."
        git clone --depth 1 https://github.com/zfogg/libsodium-bcrypt-pbkdf.git deps/ascii-chat-deps/libsodium-bcrypt-pbkdf || exit 1
    fi

    if [ ! -d deps/ascii-chat-deps/sokol ]; then
        echo ">>> Cloning sokol..."
        git clone --depth 1 https://github.com/floooh/sokol.git deps/ascii-chat-deps/sokol || exit 1
    fi

    if [ ! -d deps/ascii-chat-deps/uthash ]; then
        echo ">>> Cloning uthash..."
        git clone --depth 1 https://github.com/troydhanson/uthash.git deps/ascii-chat-deps/uthash || exit 1
    fi

    echo "✓ Submodules cloned successfully"
fi

# Verify critical submodules were initialized
if [ ! -f deps/ascii-chat-deps/bearssl/Makefile ]; then
    echo ">>> ERROR: bearssl submodule not found after initialization"
    exit 1
fi

echo "✓ Submodule check passed"
echo ">>> Submodule directory listing:"
ls -la deps/ascii-chat-deps/ | head -10
