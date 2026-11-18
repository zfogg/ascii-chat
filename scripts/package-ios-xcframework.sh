#!/usr/bin/env bash
set -euo pipefail

device_lib="${1:-build-ios-device/lib/libasciichat.a}"
simulator_lib="${2:-build-ios-sim/lib/libasciichat.a}"
output="${3:-build/asciichat.xcframework}"
host_arch="$(uname -m)"
case "$host_arch" in
  arm64|aarch64) default_sim_arch="arm64" ;;
  x86_64|amd64) default_sim_arch="x86_64" ;;
  *) default_sim_arch="x86_64" ;;
esac
simulator_arch="${IOS_SIM_ARCH:-$default_sim_arch}"

case "$simulator_arch" in
  x86_64|arm64) ;;
  *) echo "IOS_SIM_ARCH must be x86_64 or arm64" >&2; exit 2 ;;
esac

for archive in "$device_lib" "$simulator_lib"; do
  if [[ ! -f "$archive" ]]; then
    echo "missing iOS archive: $archive" >&2
    exit 1
  fi
done

archive_has_architecture() {
  local archive="$1"
  local architecture="$2"
  lipo -info "$archive" 2>/dev/null | grep -Eq "(^|[[:space:]])${architecture}([[:space:]]|$)"
}

if ! archive_has_architecture "$device_lib" arm64; then
  echo "device archive does not contain arm64: $device_lib" >&2
  lipo -info "$device_lib" >&2 || true
  exit 2
fi
if ! archive_has_architecture "$simulator_lib" "$simulator_arch"; then
  echo "simulator archive does not contain $simulator_arch: $simulator_lib" >&2
  lipo -info "$simulator_lib" >&2 || true
  exit 2
fi

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
