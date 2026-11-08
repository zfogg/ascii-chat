#!/usr/bin/env bash
set -euo pipefail

device_lib="${1:-build-ios/lib/libasciichat.a}"
simulator_lib="${2:-build-ios-sim/lib/libasciichat.a}"
output="${3:-build/asciichat.xcframework}"

for archive in "$device_lib" "$simulator_lib"; do
  if [[ ! -f "$archive" ]]; then
    echo "missing iOS archive: $archive" >&2
    exit 1
  fi
done

if [[ -e "$output" ]]; then
  echo "output already exists; remove it explicitly before packaging: $output" >&2
  exit 1
fi
xcodebuild -create-xcframework \
  -library "$device_lib" -headers include \
  -library "$simulator_lib" -headers include \
  -output "$output"
