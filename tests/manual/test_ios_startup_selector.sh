#!/usr/bin/env bash
set -euo pipefail

app="$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
plist="$(cd "$(dirname "$0")/../.." && pwd)/src/ios/Info.plist"

grep -q 'Choose a mode to start' "$app"
grep -q 'disabled(!selectedMode.isImplemented)' "$app"
grep -q 'NSMicrophoneUsageDescription' "$plist"
grep -q 'asciichat_ios_camera_start' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
grep -q 'platform_enable_keepawake' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
echo "iOS startup selector contract passed"
