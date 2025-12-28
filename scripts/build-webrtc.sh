#!/bin/bash
# =============================================================================
# WebRTC Audio Processing Build Script
# =============================================================================
# This script builds WebRTC with AEC3 audio processing from source.
# It handles downloading WebRTC source, GN tool, and building the library.
#
# Usage:
#   ./scripts/build-webrtc.sh [--clean] [--debug]
#
# Environment:
#   WEBRTC_CACHE_DIR - Override cache directory (default: .deps-cache/Debug or Release)
# =============================================================================

set -e  # Exit on error

# Parse arguments
CLEAN_BUILD=false
BUILD_TYPE="Release"

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        *)
            echo "Unknown argument: $1"
            exit 1
            ;;
    esac
done

# Determine platform
if [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="mac"
    if [[ $(uname -m) == "arm64" ]]; then
        ARCH="arm64"
    else
        ARCH="x64"
    fi
else
    PLATFORM="linux"
    ARCH="x64"
fi

# Set cache directory
CACHE_DIR="${WEBRTC_CACHE_DIR:-.deps-cache/${BUILD_TYPE}}"
WEBRTC_SRC_DIR="${CACHE_DIR}/webrtc-src/src"
WEBRTC_BUILD_DIR="${CACHE_DIR}/webrtc-src/build-${BUILD_TYPE}"

echo "=========================================="
echo "WebRTC Source Build"
echo "=========================================="
echo "Platform:    ${PLATFORM}"
echo "Architecture: ${ARCH}"
echo "Build type:  ${BUILD_TYPE}"
echo "Source dir:  ${WEBRTC_SRC_DIR}"
echo "Build dir:   ${WEBRTC_BUILD_DIR}"
echo ""

# Create cache directory
mkdir -p "${CACHE_DIR}"

# Clone WebRTC if not present
if [[ ! -d "${WEBRTC_SRC_DIR}/.git" ]]; then
    echo "Cloning WebRTC source code..."
    mkdir -p "$(dirname ${WEBRTC_SRC_DIR})"
    git clone --depth 1 https://github.com/webrtc-mirror/webrtc.git "${WEBRTC_SRC_DIR}"
    echo "✓ WebRTC source cloned"
else
    echo "✓ WebRTC source found in cache"
fi

# Clean build if requested
if [[ "${CLEAN_BUILD}" == true ]]; then
    echo "Cleaning build directory..."
    rm -rf "${WEBRTC_BUILD_DIR}"
fi

# Check for GN tool, build from source if not found
GN_TOOL=$(command -v gn 2>/dev/null)
if [[ -z "${GN_TOOL}" ]]; then
    echo "GN build tool not found, building from source..."

    GN_SRC_DIR="${CACHE_DIR}/gn-src"
    GN_OUT_DIR="${GN_SRC_DIR}/out"
    GN_TOOL="${GN_OUT_DIR}/gn"

    if [[ ! -x "${GN_TOOL}" ]]; then
        echo "Cloning GN source code..."
        if [[ -d "${GN_SRC_DIR}" ]]; then
            rm -rf "${GN_SRC_DIR}"
        fi

        git clone https://gn.googlesource.com/gn "${GN_SRC_DIR}"

        echo "Building GN..."
        cd "${GN_SRC_DIR}"

        # Check for Python 3
        if ! command -v python3 &> /dev/null; then
            echo "ERROR: Python 3 is required to build GN"
            exit 1
        fi

        # Build GN using the bootstrap script
        python3 build/gen.py

        # Build with ninja
        if ! command -v ninja &> /dev/null; then
            echo ""
            echo "ERROR: ninja is required to build GN"
            echo "Install with:"
            if [[ "${PLATFORM}" == "mac" ]]; then
                echo "  brew install ninja"
            else
                echo "  sudo apt install ninja-build"
            fi
            exit 1
        fi

        ninja -C "${GN_OUT_DIR}"
        echo "✓ GN built successfully at ${GN_TOOL}"

        cd - > /dev/null
    fi
fi

# Check for ninja
if ! command -v ninja &> /dev/null; then
    echo ""
    echo "ERROR: ninja build tool not found!"
    echo ""
    echo "Install ninja with:"
    echo "  macOS: brew install ninja"
    echo "  Linux: sudo apt install ninja-build"
    exit 1
fi

# Prepare GN arguments
IS_DEBUG="false"
if [[ "${BUILD_TYPE}" == "Debug" ]]; then
    IS_DEBUG="true"
fi

GN_ARGS=(
    "target_cpu=\"${ARCH}\""
    "is_debug=${IS_DEBUG}"
    "is_component_build=false"
    "rtc_build_examples=false"
    "rtc_build_tools=false"
    "rtc_include_tests=false"
    "rtc_enable_protobuf=false"
    "rtc_enable_sctp=false"
    "rtc_enable_data_channel=false"
    "use_gold=false"
    "rtc_use_h264=false"
    "rtc_use_x11=false"
    "use_lld=false"
)

if [[ "${PLATFORM}" == "mac" ]]; then
    GN_ARGS+=("mac_deployment_target=\"11.0\"")
else
    GN_ARGS+=("use_sysroot=false")
fi

# Create build directory
mkdir -p "${WEBRTC_BUILD_DIR}"

# Generate build files with GN
echo "Generating build files with gn..."
cd "${WEBRTC_SRC_DIR}"
"${GN_TOOL}" gen "${WEBRTC_BUILD_DIR}" --args="${GN_ARGS[*]}"

# Build with ninja
echo "Building WebRTC with ninja (this may take 10-30 minutes)..."
echo ""
ninja -C "${WEBRTC_BUILD_DIR}" audio

echo ""
echo "=========================================="
echo "✓ WebRTC built successfully!"
echo "=========================================="
echo ""
echo "Library location:"
echo "  ${WEBRTC_BUILD_DIR}/obj/libwebrtc.a"
echo ""
echo "Next step:"
echo "  cmake -B build -DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
echo "  cmake --build build"
