#!/usr/bin/env bash
set -euo pipefail

app="$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
plist="$(cd "$(dirname "$0")/../.." && pwd)/src/ios/Info.plist"

grep -q 'Choose a mode to start' "$app"
grep -q 'disabled(!mode.isImplemented)' "$app"
grep -q 'NSMicrophoneUsageDescription' "$plist"
grep -q 'NSLocalNetworkUsageDescription' "$plist"
grep -q 'asciichat_ios_camera_start' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
grep -q 'platform_enable_keepawake' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
grep -q 'asciichat_ios_audio_device_count' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
grep -q 'asciichat_ios_audio_select_device' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
grep -q 'AVAudioSessionRouteChangeNotification' "$(cd "$(dirname "$0")/../.." && pwd)/lib/audio/ios_portaudio.m"
grep -q 'ios_render_bridge_reset' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
grep -q 'preferredFrameRateRange' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/AsciiChatApp.swift"
grep -q 'ios_render_bridge_flush' "$(cd "$(dirname "$0")/../.." && pwd)/lib/platform/ios/render_bridge.c"
grep -q -- '--fps=0' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/main.c"
grep -q 'ASCIICHAT_IOS_SIMULATOR' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/CMakeLists.txt"
grep -q 'kCVPixelFormatType_32BGRA' "$(cd "$(dirname "$0")/../.." && pwd)/lib/video/webcam/macos/webcam_avfoundation.m"
grep -q 'accepts one pixel format' "$(cd "$(dirname "$0")/../.." && pwd)/lib/video/webcam/macos/webcam_avfoundation.m"
grep -q 'asciichat_ios_mode_name' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/main.c"
grep -q 'asciichat_ios_mode_available(mode)' "$(cd "$(dirname "$0")/../.." && pwd)/src/ios/main.c"
if IOS_SIM_ARCH=invalid "$(cd "$(dirname "$0")/../.." && pwd)/scripts/build-ios-simulator.sh" >/dev/null 2>&1; then
  echo "simulator architecture validation failed" >&2
  exit 1
fi
cache_dir="$(mktemp -d)"
printf 'CMAKE_OSX_ARCHITECTURES:STRING=x86_64\n' > "$cache_dir/CMakeCache.txt"
if IOS_SIM_ARCH=arm64 "$(cd "$(dirname "$0")/../.." && pwd)/scripts/build-ios-simulator.sh" "$cache_dir" >/dev/null 2>&1; then
  echo "simulator cache architecture validation failed" >&2
  rm -rf "$cache_dir"
  exit 1
fi
rm -rf "$cache_dir"
echo "iOS startup selector contract passed"
