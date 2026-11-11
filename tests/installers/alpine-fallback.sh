#!/usr/bin/env bash
# Run in an Alpine container with bash, curl, tar, CA certificates, and Deno.
set -eu
installer=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
export ASCII_CHAT_INSTALL_PREFIX="$sandbox/prefix"
export HOME="$sandbox/home"
export PATH="$ASCII_CHAT_INSTALL_PREFIX/bin:$PATH"
mkdir -p "$HOME"
# Simulate an unavailable yt-dlp package while retaining native Deno.
apk() { return 1; }
export -f apk
bash "$installer"
test -x "$ASCII_CHAT_INSTALL_PREFIX/lib/ascii-chat/bin/yt-dlp"
test ! -e "$ASCII_CHAT_INSTALL_PREFIX/lib/ascii-chat/bin/deno"
yt-dlp --version
deno --version
printf 'PASS: musl yt-dlp download and native Deno fallback\n'
