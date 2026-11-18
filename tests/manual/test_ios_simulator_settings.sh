#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
app_path="${1:-$repo_root/build-ios-sim-verify/bin/ascii-chat-ios-app.app}"
device_id="${IOS_SIM_DEVICE:-$(xcrun simctl list devices | sed -n 's/.*(\([0-9A-F-]\{36\}\)) (Booted).*/\1/p' | head -1)}"
screenshot_path="${IOS_SIM_SCREENSHOT:-$repo_root/build-ios-sim-verify/ios-settings-smoke.png}"

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
xcrun simctl launch "$device_id" "$bundle_id" --auto-open-settings >/dev/null

settle_seconds="${IOS_SIM_SETTLE_SECONDS:-5}"
mean="0"
for ((attempt = 1; attempt <= settle_seconds; attempt++)); do
  sleep 1
  xcrun simctl io "$device_id" screenshot "$screenshot_path" >/dev/null
  mean="$(magick "$screenshot_path" -alpha off -colorspace gray -format '%[fx:mean]' info:)"
  if awk -v value="$mean" 'BEGIN { exit !(value > 0.03) }'; then
    echo "iOS simulator settings smoke passed: $screenshot_path (mean=$mean)"
    exit 0
  fi
done

echo "iOS settings screen remained blank after ${settle_seconds}s (mean=$mean): $screenshot_path" >&2
exit 1
