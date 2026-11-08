#!/usr/bin/env bash
set -euo pipefail

app="$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
plist="$(cd "$(dirname "$0")/../.." && pwd)/src/ios/Info.plist"

grep -q 'Choose a mode to start' "$app"
grep -q 'disabled(!selectedMode.isImplemented)' "$app"
grep -q 'NSMicrophoneUsageDescription' "$plist"
echo "iOS startup selector contract passed"
