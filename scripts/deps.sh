#!/usr/bin/env bash
# Install dependencies for ascii-chat
#
# This script handles platform-specific dependency installation:
# - macOS: Uses Homebrew
# - Linux: Uses apt-get, yum, or pacman
# - Windows: Directs to deps.ps1
#
# Usage:
#   ./deps.sh           # Install Debug dependencies
#   ./deps.sh -Release  # Install Release dependencies (static libraries)
#
# Note: On Unix systems, -dev packages include both static (.a) and dynamic (.so) libraries.
#       The actual static/dynamic linking is controlled by CMake based on build type.

set -e

# Parse arguments
CONFIG="Debug"
if [[ "$1" == "-Release" ]] || [[ "$1" == "--release" ]]; then
  CONFIG="Release"
fi

echo ""
echo "=== ascii-chat Dependency Installer ==="
echo ""
echo "Build configuration: $CONFIG"
if [[ "$CONFIG" == "Release" ]]; then
  echo "Note: Release builds will use static linking where available"
fi
echo ""

echo "Getting submodules"
git submodule init
git submodule update --recursive

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
  if ! command -v brew &>/dev/null; then
    echo "ERROR: Homebrew not found"
    echo "Please install Homebrew from https://brew.sh"
    exit 1
  fi

  echo "Installing dependencies via Homebrew..."
  brew install mimalloc zstd libsodium portaudio

  echo ""
  echo "Dependencies installed successfully!"
  if [[ "$CONFIG" == "Release" ]]; then
    echo "You can now run: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build"
  else
    echo "You can now run: cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build"
  fi

# Linux: Detect package manager
elif [[ "$PLATFORM" == "linux" ]]; then
  if command -v apt-get &>/dev/null; then
    echo "Detected apt-get package manager"
    echo "Installing dependencies..."
    sudo apt-get update
    sudo apt-get install -y \
      clang \
      llvm \
      cmake \
      ninja-build \
      pkg-config \
      musl-tools \
      musl-dev \
      libmimalloc-dev \
      libzstd-dev \
      zlib1g-dev \
      libsodium-dev \
      portaudio19-dev \
      doxygen \
      dpkg-dev

  elif command -v yum &>/dev/null; then
    echo "Detected yum package manager"
    echo "Installing dependencies..."
    sudo yum install -y \
      clang \
      llvm \
      musl-devel \
      cmake \
      ninja-build \
      pkg-config \
      musl-gcc \
      musl-libc-static \
      mimalloc-devel \
      libzstd-devel \
      zlib-devel \
      libsodium-devel \
      portaudio-devel \
      jack-audio-connection-kit-devel \
      doxygen \
      rpm-build

  elif command -v pacman &>/dev/null; then
    echo "Detected pacman package manager"
    echo "Installing dependencies..."
    sudo pacman -S --needed \
      pkg-config \
      clang llvm lldb ccache \
      cmake ninja make \
      musl mimalloc \
      zstd zlib libsodium portaudio

  else
    echo "ERROR: No supported package manager found (apt-get, yum, or pacman)"
    echo "Please install dependencies manually:"
    echo "  - pkg-config"
    echo "  - llvm (the binary tools and runtime libraries)"
    echo "  - * zlib (library and development headers. * this is an llvm dependency - you may already have it installed)"
    echo "  - clang (both clang and clang++)"
    echo "  - * musl (development tools. * this is only needed if you plan to do a release build)"
    echo "  - * mimalloc (library and development headers. * this is only needed if you plan to do a release build)"
    echo "  - zstd (library and development headers)"
    echo "  - libsodium (library and development headers)"
    echo "  - portaudio (library and development headers)"
    echo "  - * jack (library and development headers. * you might need this - on some Linux systems, the Portaudio build from the system package repos is linked to Jack but doesn't list Jack as a dependency so it won't be automatically installed and builds will fail without it)"
    exit 1
  fi

  echo ""
  echo "Dependencies installed successfully!"
  if [[ "$CONFIG" == "Release" ]]; then
    echo "You can now run: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build"
  else
    echo "You can now run: cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build"
  fi

# Windows: Direct to PowerShell script
elif [[ "$PLATFORM" == "windows" ]]; then
  echo "On Windows, please use the PowerShell script instead:"
  if [[ "$CONFIG" == "Release" ]]; then
    echo "  ./scripts/deps.ps1 -Release"
  else
    echo "  ./scripts/deps.ps1"
  fi
  exit 1
fi
