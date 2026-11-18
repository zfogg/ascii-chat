#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
toolchain_root="${ASCII_CHAT_DEPS_CACHE_ROOT:-${repo_root}/.deps-cache/wsl/Ubuntu}"
gcc_root=""
for candidate in "$toolchain_root"/alpine-gcc "$toolchain_root"/*/alpine-gcc; do
  if [ -x "$candidate/usr/bin/gcc" ]; then
    gcc_root="$candidate"
    break
  fi
done

if [ -z "$gcc_root" ]; then
  echo "ascii-chat: cached Alpine GCC suite was not found under $toolchain_root" >&2
  exit 127
fi

export LD_LIBRARY_PATH="$gcc_root/usr/lib:/usr/lib/musl/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$gcc_root/usr/bin/gcc" "$@"
