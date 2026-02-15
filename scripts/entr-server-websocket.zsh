#!/usr/bin/env zsh
#

set -e

_repo_root=$(git rev-parse --show-toplevel)
cd "$_repo_root"
source scripts/color.zsh
source scripts/developer-helpers.zsh

rebuild_and_start() {
  pkill -9 lldb 2>/dev/null || true
  pkill -9 -x ascii-chat 2>/dev/null || true
  sleep 1
  rm -f build/lib/libasciichat.0.dylib build/lib/libasciichat.dylib
  cbb --target ascii-chat
  sleep 0.5
  lldb \
    -o "process handle -p true -s true -n false SIGSEGV SIGABRT SIGBUS SIGILL" \
    -o "process handle -p true -s true -n false SIGTERM SIGINT" \
    -o "run" \
    ./build/bin/ascii-chat -- --log-file server.log --log-level debug server 0.0.0.0 "::" --no-status-screen --websocket-port 27226 || true
}

# Initialize marker file
_marker_dir="/tmp/$(basename "$0")"
_marker_file="$_marker_dir/marker.$$"
mkdir -p "$_marker_dir"
touch "$_marker_file"
trap "rm -f $_marker_file" EXIT

# Start server immediately
rebuild_and_start
touch "$_marker_file"

# Run watcher loop in background
{
  while true; do
    # Find files modified after the marker
    _changed=$(find src/ lib/ include/ -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" -o -name "*.m" \) -newer "$_marker_file" 2>/dev/null)

    if [ -n "$_changed" ]; then
      clear
      echo "Changes detected:"
      echo "$_changed" | head -3
      echo ""
      rebuild_and_start
      touch "$_marker_file"
    fi
    sleep 0.1
  done
} &

# Keep script alive
wait
