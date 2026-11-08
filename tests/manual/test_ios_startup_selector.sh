#!/usr/bin/env bash
set -euo pipefail

app="$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
plist="$(cd "$(dirname "$0")/../.." && pwd)/src/ios/Info.plist"

grep -q 'Choose a mode to start' "$app"
grep -q 'disabled(!selectedMode.isImplemented)' "$app"
grep -q 'NSMicrophoneUsageDescription' "$plist"
grep -q 'asciichat_ios_camera_start' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
grep -q 'platform_enable_keepawake' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
grep -q 'ios_render_bridge_reset' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
grep -q 'asciichat_ios_mode_name' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/main.c"
if IOS_SIM_ARCH=invalid "$(cd "$(dirname "$0")/../.." && pwd)/scripts/build-ios-simulator.sh" >/dev/null 2>&1; then
  echo "simulator architecture validation failed" >&2
  exit 1
fi
echo "iOS startup selector contract passed"
