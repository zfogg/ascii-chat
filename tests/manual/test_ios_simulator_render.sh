#!/usr/bin/env bash
set -euo pipefail

# Smoke-test the real app bundle, not just the CMake target. This intentionally
# drives the same SwiftUI -> asciichat_ios_main() -> render bridge path used by
# the simulator screenshots checked into development artifacts.
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
app_path="${1:-$repo_root/build-ios-sim-verify/bin/ascii-chat-ios-app.app}"
device_id="${IOS_SIM_DEVICE:-$(xcrun simctl list devices | sed -n 's/.*(\([0-9A-F-]\{36\}\)) (Booted).*/\1/p' | head -1)}"
screenshot_path="${IOS_SIM_SCREENSHOT:-$repo_root/build-ios-sim-verify/ios-mirror-smoke.png}"

if [[ -z "$device_id" ]]; then
  echo "No booted iOS simulator found; boot one or set IOS_SIM_DEVICE" >&2
  exit 2
fi
if [[ ! -d "$app_path" ]]; then
  echo "iOS app bundle not found: $app_path" >&2
  exit 2
fi
if ! command -v magick >/dev/null 2>&1; then
  echo "ImageMagick (magick) is required for screenshot validation" >&2
  exit 2
fi

bundle_id="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app_path/Info.plist")"
xcrun simctl install "$device_id" "$app_path"
xcrun simctl terminate "$device_id" "$bundle_id" >/dev/null 2>&1 || true
xcrun simctl launch "$device_id" "$bundle_id" --auto-start-mirror >/dev/null
settle_seconds="${IOS_SIM_SETTLE_SECONDS:-12}"
surface_mean="0"
red_control_mean="0"
for ((attempt = 1; attempt <= settle_seconds; attempt++)); do
  sleep 1
  xcrun simctl io "$device_id" screenshot "$screenshot_path" >/dev/null
  # Ignore the status/navigation bars and alpha channel. A healthy synthetic
  # mirror frame has a comfortably non-zero mean; this catches black-surface
  # regressions while allowing dark frames and palettes.
  surface_mean="$(magick "$screenshot_path" -crop 100%x80%+0+100 -alpha off -colorspace gray -format '%[fx:mean]' info:)"
  # The selector screen is also non-black. The running surface has the
  # semitransparent red stop control in the display corner, so require that
  # control as a second signal that the native mode actually started.
  red_control_mean="$(magick "$screenshot_path" -crop 25%x20%+75%+0 -alpha off -fx 'r>0.35 && g<0.25 && b<0.25 ? 1 : 0' -format '%[fx:mean]' info:)"
  if awk -v mean="$surface_mean" -v red="$red_control_mean" 'BEGIN { exit !(mean > 0.005 && red > 0.0001) }'; then
    echo "iOS simulator mirror smoke passed: $screenshot_path (surface mean=$surface_mean red-control=$red_control_mean)"
    exit 0
  fi
done

echo "iOS mirror screenshot did not show a running render surface after ${settle_seconds}s (surface mean=$surface_mean red-control=$red_control_mean): $screenshot_path" >&2
exit 1
