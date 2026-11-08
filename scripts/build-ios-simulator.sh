#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build-ios-sim}"
case "${IOS_SIM_ARCH:-x86_64}" in
  x86_64|arm64) ;;
  *) echo "IOS_SIM_ARCH must be x86_64 or arm64" >&2; exit 2 ;;
esac
cmake -S . -B "$build_dir" \
  -DBUILD_IOS_SIM=ON \
  -DCMAKE_OSX_ARCHITECTURES="${IOS_SIM_ARCH:-x86_64}"
cmake --build "$build_dir" --target ascii-chat-ios-app -j"${JOBS:-4}"
