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

headers_dir="$(mktemp -d "${TMPDIR:-/tmp}/ascii-chat-ios-headers.XXXXXX")"
trap 'rm -rf "$headers_dir"' EXIT
cp -R include/. "$headers_dir/"
mkdir -p "$headers_dir/ascii-chat-deps"
cp -R deps/ascii-chat-deps/sokol "$headers_dir/ascii-chat-deps/"

xcodebuild -create-xcframework \
  -library "$device_lib" -headers "$headers_dir" \
  -library "$simulator_lib" -headers "$headers_dir" \
  -output "$output"
