#!/usr/bin/env bash
set -euo pipefail

# Run this with a native ascii-chat server already listening on the selected
# host and port. It exercises the real SwiftUI -> client_main() path.
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
app_path="${1:-$repo_root/build-ios-sim-verify/bin/ascii-chat-ios-app.app}"
device_id="${IOS_SIM_DEVICE:-$(xcrun simctl list devices | sed -n 's/.*(\([0-9A-F-]\{36\}\)) (Booted).*/\1/p' | head -1)}"
screenshot_path="${IOS_SIM_SCREENSHOT:-$repo_root/build-ios-sim-verify/ios-client-smoke.png}"
server_address="${IOS_SIM_CLIENT_ADDRESS:-127.0.0.1}"
server_port="${IOS_SIM_CLIENT_PORT:-27224}"

if [[ -z "$device_id" || ! -d "$app_path" ]]; then
  echo "A booted simulator and an iOS app bundle are required" >&2
  exit 2
fi
if ! command -v magick >/dev/null 2>&1; then
  echo "ImageMagick (magick) is required for screenshot validation" >&2
  exit 2
fi

bundle_id="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app_path/Info.plist")"
xcrun simctl install "$device_id" "$app_path"
xcrun simctl terminate "$device_id" "$bundle_id" >/dev/null 2>&1 || true
xcrun simctl launch "$device_id" "$bundle_id" --auto-start-client \
  "--ios-address=$server_address" "--ios-port=$server_port" >/dev/null

settle_seconds="${IOS_SIM_SETTLE_SECONDS:-15}"
surface_mean="0"
red_control_mean="0"
for ((attempt = 1; attempt <= settle_seconds; attempt++)); do
  sleep 1
  xcrun simctl io "$device_id" screenshot "$screenshot_path" >/dev/null
  surface_mean="$(magick "$screenshot_path" -crop 100%x80%+0+100 -alpha off -colorspace gray -format '%[fx:mean]' info:)"
  red_control_mean="$(magick "$screenshot_path" -crop 25%x20%+75%+0 -alpha off \
    -fx 'r>0.35 && g<0.25 && b<0.25 ? 1 : 0' -format '%[fx:mean]' info:)"
  if awk -v mean="$surface_mean" -v red="$red_control_mean" \
    'BEGIN { exit !(mean > 0.005 && red > 0.0001) }'; then
    echo "iOS simulator client smoke passed: $screenshot_path (surface mean=$surface_mean red-control=$red_control_mean)"
    exit 0
  fi
done

echo "iOS simulator client did not reach a running render surface after ${settle_seconds}s (surface mean=$surface_mean red-control=$red_control_mean)" >&2
exit 1
