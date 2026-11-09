#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
units=(ascii-chat-server ascii-chat-discovery-service ascii-chat-mcp-server ascii-chat-api-server)
for unit in "${units[@]}"; do
  file="$root/share/systemd/$unit.service"
  test -s "$file"
  grep -q '^Type=notify$' "$file"
  grep -q 'StandardOutput=journal' "$file"
  grep -q '^WatchdogSec=' "$file"
  grep -q "^SyslogIdentifier=ascii-chat-$unit$" "$file"
  grep -q '^ExecStart=@ASCII_CHAT_EXECUTABLE@ ' "$file"
  ! grep -Eq -- '--(color|splash-screen|no-check-update|otel-endpoint|log-file)' "$file"
  grep -Eq '^ExecStart=/usr/bin/ascii-chat (server|acds|mcp-server)( |$)' "$file"
done
test -x "$root/scripts/install-systemd-confs.sh"
"$root/scripts/install-systemd-confs.sh" --help >/dev/null
echo "systemd unit checks passed"
