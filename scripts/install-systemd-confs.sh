#!/usr/bin/env bash
set -euo pipefail

usage() { echo "Usage: $0 [--user] [--uninstall] [--help|-h]"; }
user=0; uninstall=0
while (($#)); do
  case "$1" in
    --user) user=1 ;;
    --uninstall) uninstall=1 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done
if [[ "$user" -eq 0 && "$EUID" -ne 0 ]]; then
  echo "run as root or pass --user" >&2; exit 1
fi
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$user" -eq 1 ]]; then dest="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"; ctl=(systemctl --user); else dest="/etc/systemd/system"; ctl=(systemctl); fi
units=(ascii-chat-server ascii-chat-discovery-service ascii-chat-mcp-server ascii-chat-api-server)
mkdir -p "$dest"
for unit in "${units[@]}"; do
  target="$dest/$unit.service"
  if [[ "$uninstall" -eq 1 ]]; then rm -f "$target"; else install -m 0644 "$root/share/systemd/$unit.service" "$target"; fi
done
"${ctl[@]}" daemon-reload
echo "$([[ "$uninstall" -eq 1 ]] && echo Removed || echo Installed) ${#units[@]} ascii-chat systemd units in $dest"
