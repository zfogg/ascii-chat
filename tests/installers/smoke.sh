#!/usr/bin/env bash
# Live release integration test; accepts the path to install.sh.
set -eu
installer=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
export ASCII_CHAT_INSTALL_PREFIX="$sandbox/prefix with spaces"
unset ASCII_CHAT_VERSION
bash "$installer"
binary="$ASCII_CHAT_INSTALL_PREFIX/bin/ascii-chat"
"$binary" --version
test -L "$binary"
test -f "$ASCII_CHAT_INSTALL_PREFIX/lib/ascii-chat/.ascii-chat-installer"
touch "$ASCII_CHAT_INSTALL_PREFIX/lib/ascii-chat/obsolete-file"
touch "$ASCII_CHAT_INSTALL_PREFIX/unrelated-file"
# A pipe must behave the same as a downloaded script, including on Bash 3.2.
cat "$installer" | bash
test ! -e "$ASCII_CHAT_INSTALL_PREFIX/lib/ascii-chat/obsolete-file"
test -f "$ASCII_CHAT_INSTALL_PREFIX/unrelated-file"
test -z "$(find "$ASCII_CHAT_INSTALL_PREFIX/lib" -name '.ascii-chat-install.*' -print)"
before=$("$binary" --version)
export ASCII_CHAT_VERSION=v0.0.0-installer-missing-release
if bash "$installer"; then echo 'Missing release unexpectedly succeeded' >&2; exit 1; fi
test "$("$binary" --version)" = "$before"
test -z "$(find "$ASCII_CHAT_INSTALL_PREFIX/lib" -name '.ascii-chat-install.*' -print)"
unset ASCII_CHAT_VERSION
export ASCII_CHAT_INSTALL_PREFIX="$sandbox/unmanaged"
mkdir -p "$ASCII_CHAT_INSTALL_PREFIX/bin"
printf 'keep me\n' > "$ASCII_CHAT_INSTALL_PREFIX/bin/ascii-chat"
if bash "$installer"; then echo 'Unmanaged binary was overwritten' >&2; exit 1; fi
test "$(cat "$ASCII_CHAT_INSTALL_PREFIX/bin/ascii-chat")" = 'keep me'
printf 'PASS: install, piped upgrade, cleanup, failed download, unmanaged file preservation\n'
