#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build-ios-device}"
xcrun --sdk iphoneos --show-sdk-path >/dev/null
if [[ -f "$build_dir/CMakeCache.txt" ]] && grep -q '^BUILD_IOS_SIM:BOOL=ON' "$build_dir/CMakeCache.txt"; then
  echo "$build_dir is configured for the simulator; choose a fresh device build directory" >&2
  exit 2
fi
if [[ -f "$build_dir/CMakeCache.txt" ]] && ! grep -q '^CMAKE_OSX_ARCHITECTURES:STRING=arm64' "$build_dir/CMakeCache.txt"; then
  echo "$build_dir is configured for a non-arm64 architecture; choose a fresh device build directory" >&2
  exit 2
fi
cmake -S . -B "$build_dir" -G Ninja \
  -DBUILD_IOS=ON \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build "$build_dir" --target ascii-chat-ios-app -j"${JOBS:-4}"
