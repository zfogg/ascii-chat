#!/usr/bin/env bash
# Shared trace command helper for debug scripts
# Usage: trace_command.sh <output_file> <command> [args...]

output_file="$1"
shift

if [[ "$OSTYPE" == "darwin"* ]]; then
  # Check if dtrace can run (requires elevated privileges on modern macOS)
  if dtrace -l >/dev/null 2>&1; then
    dtrace -c "$*" -o "$output_file"
  else
    # dtrace blocked by SIP, just run command without tracing, redirect to file
    "$@" > "$output_file" 2>&1
  fi
else
  strace -f -o "$output_file" "$@"
fi
