#!/usr/bin/env bash
# Run in an Amazon Linux 2 container with curl, tar, gzip, and unzip installed.
set -eu
installer=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
export ASCII_CHAT_INSTALL_PREFIX="$sandbox/prefix"
export HOME="$sandbox/home"
export PATH="$ASCII_CHAT_INSTALL_PREFIX/bin:$PATH"
mkdir -p "$HOME"
bash "$installer"
test "$(deno --version | head -n 1 | cut -d ' ' -f 2)" = 2.3.3
probe=$(yt-dlp --ignore-config --verbose --simulate invalid: 2>&1 || true)
case "$probe" in
  *yt_dlp_ejs*'JS runtimes: deno-2.3.3'*) ;;
  *) printf 'yt-dlp did not detect its EJS scripts and compatible Deno runtime.\n' >&2; exit 1 ;;
esac
printf 'PASS: Amazon Linux 2 uses compatible Deno with yt-dlp EJS\n'
