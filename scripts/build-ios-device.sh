#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build-ios}"
cmake -S . -B "$build_dir" \
  -DBUILD_IOS=ON \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build "$build_dir" --target ascii-chat-ios-app -j"${JOBS:-4}"
