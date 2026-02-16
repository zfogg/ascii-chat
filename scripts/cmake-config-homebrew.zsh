#!/bin/zsh
#
# CMake Configuration Script for Homebrew LLVM
#
# Configures CMake build system using Homebrew-installed LLVM compiler
# Works on both Intel and ARM (Apple Silicon) architectures
#
# Usage:
#   ./scripts/cmake-config-homebrew.zsh [OPTIONS]
#
# Options:
#   --preset <name>        CMake preset to use (default: 'default')
#   -B <dir>               Build directory (default: 'build')
#   -h, --help             Show this help message
#
# Examples:
#   ./scripts/cmake-config-homebrew.zsh
#   ./scripts/cmake-config-homebrew.zsh --preset dev -B build_dev
#   ./scripts/cmake-config-homebrew.zsh -B build_custom
#

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
PRESET="default"
BUILD_DIR="build"
SCRIPT_DIR="$(cd "$(dirname "${0:A}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset)
            PRESET="$2"
            shift 2
            ;;
        -B)
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            cat << 'HELP'
CMake Configuration Script for Homebrew LLVM

Configures CMake build system using Homebrew-installed LLVM compiler
Works on both Intel and ARM (Apple Silicon) architectures

Usage:
  ./scripts/cmake-config-homebrew.zsh [OPTIONS]

Options:
  --preset <name>        CMake preset to use (default: 'default')
  -B <dir>               Build directory (default: 'build')
  -h, --help             Show this help message

Examples:
  ./scripts/cmake-config-homebrew.zsh
  ./scripts/cmake-config-homebrew.zsh --preset dev -B build_dev
  ./scripts/cmake-config-homebrew.zsh -B build_custom
HELP
            exit 0
            ;;
        *)
            echo -e "${RED}Error: Unknown option '$1'${NC}" >&2
            echo "Use --help for usage information" >&2
            exit 1
            ;;
    esac
done

# Detect architecture
ARCH=$(uname -m)
case "$ARCH" in
    arm64|aarch64)
        DETECTED_ARCH="ARM (Apple Silicon)"
        CMAKE_OSX_ARCH="arm64"
        ;;
    x86_64)
        DETECTED_ARCH="Intel"
        CMAKE_OSX_ARCH="x86_64"
        ;;
    *)
        echo -e "${RED}Error: Unsupported architecture: $ARCH${NC}" >&2
        exit 1
        ;;
esac

echo -e "${BLUE}CMake Configuration for Homebrew LLVM${NC}"
echo "========================================"
echo -e "Architecture detected: ${GREEN}$DETECTED_ARCH${NC} ($ARCH)"
echo -e "CMake preset: ${GREEN}$PRESET${NC}"
echo -e "Build directory: ${GREEN}$BUILD_DIR${NC}"
echo ""

# Find Homebrew LLVM installation
echo "Locating Homebrew LLVM..."

BREW_PREFIX=$(brew --prefix 2>/dev/null || echo "/opt/homebrew")
LLVM_DIR=""

# Try to find LLVM in common Homebrew locations
for llvm_path in "$BREW_PREFIX/opt/llvm" "$BREW_PREFIX/Cellar/llvm"*; do
    if [[ -d "$llvm_path" && -f "$llvm_path/bin/clang" ]]; then
        LLVM_DIR="$llvm_path"
        break
    fi
done

# If not found by common paths, try 'brew list llvm'
if [[ -z "$LLVM_DIR" ]]; then
    if command -v brew &> /dev/null; then
        LLVM_CELLAR=$(brew list llvm 2>/dev/null | grep -E "bin/clang$" | head -1 | sed 's|/bin/clang$||' || echo "")
        if [[ -n "$LLVM_CELLAR" ]]; then
            LLVM_DIR="$LLVM_CELLAR"
        fi
    fi
fi

if [[ -z "$LLVM_DIR" || ! -f "$LLVM_DIR/bin/clang" ]]; then
    echo -e "${RED}Error: Homebrew LLVM not found!${NC}" >&2
    echo ""
    echo "To install Homebrew LLVM, run:"
    echo "  brew install llvm"
    echo ""
    exit 1
fi

LLVM_VERSION=$("$LLVM_DIR/bin/clang" --version 2>/dev/null | head -1 | grep -oE "[0-9]+\.[0-9]+\.[0-9]+" || echo "unknown")
echo -e "Found: ${GREEN}$LLVM_DIR${NC}"
echo -e "Version: ${GREEN}$LLVM_VERSION${NC}"
echo ""

# Verify CMake is available
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}Error: CMake not found!${NC}" >&2
    echo "To install CMake, run:"
    echo "  brew install cmake"
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -1)
echo -e "Using: ${GREEN}$CMAKE_VERSION${NC}"
echo ""

# Create build directory if it doesn't exist
if [[ ! -d "$BUILD_DIR" ]]; then
    echo "Creating build directory: $BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi

# Configure CMake with Homebrew LLVM
echo -e "${YELLOW}Configuring CMake...${NC}"
echo ""

# Set environment variables for CMake
export CC="$LLVM_DIR/bin/clang"
export CXX="$LLVM_DIR/bin/clang++"
export AR="$LLVM_DIR/bin/llvm-ar"
export RANLIB="$LLVM_DIR/bin/llvm-ranlib"
export LD_LIBRARY_PATH="$LLVM_DIR/lib:${LD_LIBRARY_PATH:-}"

# Add Homebrew-installed LLVM tools to PATH
export PATH="$LLVM_DIR/bin:$PATH"

# CMake invocation
CMAKE_ARGS=(
    "--preset" "$PRESET"
    "-B" "$BUILD_DIR"
)

# Add architecture-specific flag for multi-architecture builds (if needed)
CMAKE_ARGS+=(
    "-DCMAKE_OSX_ARCHITECTURES=$CMAKE_OSX_ARCH"
)

# Run CMake
echo "Command: cmake ${CMAKE_ARGS[@]}"
echo ""

if cmake "${CMAKE_ARGS[@]}"; then
    echo ""
    echo -e "${GREEN}✓ CMake configuration successful!${NC}"
    echo ""
    echo "Next steps:"
    echo "  1. Build: cmake --build $BUILD_DIR"
    echo "  2. Run:   ./build/bin/ascii-chat --help"
    echo ""
    echo "Available presets:"
    echo "  • default - Debug with sanitizers (recommended for development)"
    echo "  • dev     - Debug without sanitizers (faster builds)"
    echo "  • release - Release build (optimized)"
    echo ""
    echo "To reconfigure with a different preset:"
    echo "  ./scripts/cmake-config-homebrew.zsh --preset dev -B build"
    echo ""
else
    echo ""
    echo -e "${RED}✗ CMake configuration failed!${NC}" >&2
    exit 1
fi
