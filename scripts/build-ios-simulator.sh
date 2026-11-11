#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build-ios-sim}"
sim_arch="${IOS_SIM_ARCH:-x86_64}"
case "$sim_arch" in
  x86_64|arm64) ;;
  *) echo "IOS_SIM_ARCH must be x86_64 or arm64" >&2; exit 2 ;;
esac
if [[ -f "$build_dir/CMakeCache.txt" ]] && ! grep -q "^CMAKE_OSX_ARCHITECTURES:STRING=$sim_arch" "$build_dir/CMakeCache.txt"; then
  echo "$build_dir is configured for a different simulator architecture; choose a fresh build directory" >&2
  exit 2
fi
cmake -S . -B "$build_dir" -G Ninja \
  -DBUILD_IOS_SIM=ON \
  -DCMAKE_OSX_ARCHITECTURES="$sim_arch"
cmake --build "$build_dir" --target ascii-chat-ios-app -j"${JOBS:-4}"
