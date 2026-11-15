#!/usr/bin/env bash
set -euo pipefail

# Generate a WinGet manifest from the MSI attached to a published GitHub release
# and open/update a PR in the publisher's fork of microsoft/winget-pkgs.

VERSION="${1:?usage: $0 <version-without-v> [repository]}"
REPOSITORY="${2:-zfogg/ascii-chat}"
FORK_REPOSITORY="${WINGET_FORK_REPOSITORY:-zfogg/winget-pkgs}"
TOKEN="${WINGET_GITHUB_TOKEN:-}"

if [[ -z "$TOKEN" ]]; then
  echo "WINGET_GITHUB_TOKEN is not configured; skipping WinGet PR submission."
  exit 0
fi

command -v gh >/dev/null || { echo "gh is required" >&2; exit 1; }
command -v sha256sum >/dev/null || { echo "sha256sum is required" >&2; exit 1; }

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
MSI="$WORK_DIR/ascii-chat.msi"
MANIFEST_DIR="$WORK_DIR/manifests/a/ascii-chat/$VERSION"
ASSET_URL="https://github.com/$REPOSITORY/releases/download/v$VERSION/ascii-chat-${VERSION}-Windows-amd64.msi"

curl --fail --location --retry 3 --output "$MSI" "$ASSET_URL"
HASH="$(sha256sum "$MSI" | awk '{print toupper($1)}')"
mkdir -p "$MANIFEST_DIR"

cat > "$MANIFEST_DIR/ascii-chat.yaml" <<EOF
PackageIdentifier: ascii-chat
PackageVersion: $VERSION
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.10.0
EOF

cat > "$MANIFEST_DIR/ascii-chat.locale.en-US.yaml" <<EOF
PackageIdentifier: ascii-chat
PackageVersion: $VERSION
PackageLocale: en-US
Publisher: Zachary Fogg
PublisherUrl: https://github.com/zfogg
PublisherSupportUrl: https://github.com/zfogg/ascii-chat/issues
PackageName: ascii-chat
PackageUrl: https://ascii-chat.com
License: MIT
LicenseUrl: https://github.com/zfogg/ascii-chat/blob/master/LICENSE.txt
ShortDescription: Video chat in your terminal
Description: ascii-chat turns your terminal into a tiny enchanted video-chat room. Bring webcams and microphones, invite multiple people, stream local files or online media, and watch live video become colorful ASCII, glyphs, or braille art. It includes encrypted communication, audio capture and playback, renderer and palette controls, snapshots, recording-friendly output, discovery, and modern network transports including TCP, WebSocket, WebRTC, and QUIC.
Moniker: ascii-chat
Tags:
- video
- chat
- terminal
- ascii
ManifestType: defaultLocale
ManifestVersion: 1.10.0
EOF

cat > "$MANIFEST_DIR/ascii-chat.installer.yaml" <<EOF
PackageIdentifier: ascii-chat
PackageVersion: $VERSION
Installers:
- Architecture: x64
  InstallerType: wix
  InstallerUrl: $ASSET_URL
  InstallerSha256: $HASH
ManifestType: installer
ManifestVersion: 1.10.0
EOF

git clone --depth 1 "https://x-access-token:$TOKEN@github.com/$FORK_REPOSITORY.git" "$WORK_DIR/winget-pkgs"
cd "$WORK_DIR/winget-pkgs"
BRANCH="ascii-chat-$VERSION"
git checkout -b "$BRANCH"
TARGET="manifests/a/ascii-chat/$VERSION"
rm -rf "manifests/a/ascii-chat/$VERSION"
mkdir -p "$(dirname "$TARGET")"
cp -R "$MANIFEST_DIR" "$TARGET"
git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git add "$TARGET"
git commit -m "New version: ascii-chat version $VERSION"
git push --force origin "$BRANCH"

HEAD="$(cut -d/ -f1 <<< "$FORK_REPOSITORY"):$BRANCH"
TITLE="New version: ascii-chat version $VERSION"
BODY="Automated manifest update for ascii-chat $VERSION.

Release: https://github.com/$REPOSITORY/releases/tag/v$VERSION"
EXISTING_PR="$(gh pr list --repo microsoft/winget-pkgs --head "$HEAD" --state open --json number --jq '.[0].number // empty')"
if [[ -n "$EXISTING_PR" ]]; then
  gh pr edit "$EXISTING_PR" --repo microsoft/winget-pkgs --title "$TITLE" --body "$BODY"
  echo "Updated existing WinGet PR #$EXISTING_PR"
else
  gh pr create \
    --repo microsoft/winget-pkgs \
    --head "$HEAD" \
    --base master \
    --title "$TITLE" \
    --body "$BODY"
fi
