#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build-ios-sim}"
cmake -S . -B "$build_dir" \
  -DBUILD_IOS_SIM=ON \
  -DCMAKE_OSX_ARCHITECTURES="${IOS_SIM_ARCH:-x86_64}"
cmake --build "$build_dir" --target ascii-chat-ios-app -j"${JOBS:-4}"
