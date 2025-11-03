#!/usr/bin/env bash
# Install dependencies for ascii-chat
#
# This script handles platform-specific dependency installation:
# - macOS: Uses Homebrew
# - Linux: Uses apt-get, yum, or pacman
# - Windows: Directs to deps.ps1
#
# Usage:
#   ./deps.sh

set -e

echo ""
echo "=== ascii-chat Dependency Installer ==="
echo ""

# Detect platform
if [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="macos"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    PLATFORM="linux"
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
    PLATFORM="windows"
else
    echo "ERROR: Unsupported platform: $OSTYPE"
    exit 1
fi

echo "Detected platform: $PLATFORM"
echo ""

# macOS: Use Homebrew
if [[ "$PLATFORM" == "macos" ]]; then
    if ! command -v brew &> /dev/null; then
        echo "ERROR: Homebrew not found"
        echo "Please install Homebrew from https://brew.sh"
        exit 1
    fi

    echo "Installing dependencies via Homebrew..."
    brew install zstd libsodium portaudio

    echo ""
    echo "Dependencies installed successfully!"
    echo "You can now run: cmake -B build && cmake --build build"

# Linux: Detect package manager
elif [[ "$PLATFORM" == "linux" ]]; then
    if command -v apt-get &> /dev/null; then
        echo "Detected apt-get package manager"
        echo "Installing dependencies..."
        sudo apt-get update
        sudo apt-get install -y \
            libzstd-dev \
            libsodium-dev \
            portaudio19-dev \
            pkg-config

    elif command -v yum &> /dev/null; then
        echo "Detected yum package manager"
        echo "Installing dependencies..."
        sudo yum install -y \
            libzstd-devel \
            libsodium-devel \
            portaudio-devel \
            pkg-config

    elif command -v pacman &> /dev/null; then
        echo "Detected pacman package manager"
        echo "Installing dependencies..."
        sudo pacman -S --needed \
            pkg-config \
            clang llvm lldb ccache \
            cmake ninja make \
            musl mimalloc \
            zstd libsodium portaudio

    else
        echo "ERROR: No supported package manager found (apt-get, yum, or pacman)"
        echo "Please install dependencies manually:"
        echo "  - zstd"
        echo "  - libsodium"
        echo "  - portaudio"
        echo "  - pkg-config"
        exit 1
    fi

    echo ""
    echo "Dependencies installed successfully!"
    echo "You can now run: cmake -B build && cmake --build build"

# Windows: Direct to PowerShell script
elif [[ "$PLATFORM" == "windows" ]]; then
    echo "On Windows, please use the PowerShell script instead:"
    echo "  ./scripts/deps.ps1"
    exit 1
fi
