#!/usr/bin/env bash
set -euo pipefail

app_path="${1:?usage: $0 path/to/ascii-chat-ios-app.app [architecture]}"
expected_arch="${2:-}"

if [[ ! -d "$app_path" ]]; then
  echo "iOS app bundle does not exist: $app_path" >&2
  exit 1
fi

executable="$app_path/ascii-chat-ios-app"
info="$app_path/Info.plist"
for required in "$executable" "$info" "$app_path/AppIcon.png" "$app_path/LaunchScreen.storyboard" "$app_path/ca-certificates.pem"; do
  if [[ ! -f "$required" ]]; then
    echo "iOS bundle is missing: $required" >&2
    exit 1
  fi
done

bundle_id="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$info")"
[[ "$bundle_id" == "com.ascii-chat.ios" ]] || {
  echo "unexpected bundle identifier: $bundle_id" >&2
  exit 1
}

icon_dimensions="$(sips -g pixelWidth -g pixelHeight "$app_path/AppIcon.png" 2>/dev/null)"
grep -q 'pixelWidth: 1024' <<<"$icon_dimensions"
grep -q 'pixelHeight: 1024' <<<"$icon_dimensions"

ca_bundle="$app_path/ca-certificates.pem"
if [[ ! -s "$ca_bundle" ]] || ! grep -q -- '-----BEGIN CERTIFICATE-----' "$ca_bundle" ||
   ! grep -q -- '-----END CERTIFICATE-----' "$ca_bundle"; then
  echo "iOS bundle CA resource is empty or is not PEM certificate data: $ca_bundle" >&2
  exit 1
fi

if [[ -n "$expected_arch" ]]; then
  file "$executable" | grep -q "Mach-O.*$expected_arch"
fi

echo "iOS app bundle validation passed: $app_path${expected_arch:+ ($expected_arch)}"
