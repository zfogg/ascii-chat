#!/usr/bin/env bash
set -euo pipefail

# Shared release publisher for Homebrew, Scoop, Chocolatey, and Npackd.
# Usage: submit.sh <homebrew|scoop|chocolatey|npackd> <version-without-v> [repository]

TARGET="${1:?usage: $0 <scoop|chocolatey|npackd> <version> [repository]}"
VERSION="${2:?usage: $0 <scoop|chocolatey|npackd> <version> [repository]}"
REPOSITORY="${3:-zfogg/ascii-chat}"
TOKEN="${PACKAGE_MANAGER_GITHUB_TOKEN:-}"
SCOOP_REPOSITORY="${SCOOP_REPOSITORY:-zfogg/ascii-chat-scoop}"
WINGET_FORK_REPOSITORY="${WINGET_FORK_REPOSITORY:-zfogg/winget-pkgs}"
CHOCOLATEY_FORK="${CHOCOLATEY_FORK_REPOSITORY:-zfogg/chocolatey-packages}"
N_PACKD_REPOSITORY="${NPACKD_REPOSITORY:-zfogg/npackd-repository}"
HOMEBREW_REPOSITORY="${HOMEBREW_REPOSITORY:-zfogg/homebrew-ascii-chat}"

if [[ -z "$TOKEN" ]]; then
  echo "PACKAGE_MANAGER_GITHUB_TOKEN is required for $TARGET submission." >&2
  exit 1
fi
export GH_TOKEN="$TOKEN"

command -v gh >/dev/null || { echo "gh is required" >&2; exit 1; }
command -v curl >/dev/null || { echo "curl is required" >&2; exit 1; }
command -v sha256sum >/dev/null || { echo "sha256sum is required" >&2; exit 1; }
command -v sha1sum >/dev/null || { echo "sha1sum is required" >&2; exit 1; }

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
MSI_URL="https://github.com/$REPOSITORY/releases/download/v$VERSION/ascii-chat-${VERSION}-Windows-amd64.msi"
RUNTIME_X64_URL="https://github.com/$REPOSITORY/releases/download/v$VERSION/ascii-chat-${VERSION}-Windows-amd64.zip"
RUNTIME_ARM64_URL="https://github.com/$REPOSITORY/releases/download/v$VERSION/ascii-chat-${VERSION}-Windows-arm64.zip"
LIB_X64_URL="https://github.com/$REPOSITORY/releases/download/v$VERSION/libasciichat-${VERSION}-Windows-amd64.zip"
LIB_ARM64_URL="https://github.com/$REPOSITORY/releases/download/v$VERSION/libasciichat-${VERSION}-Windows-arm64.zip"
RUNTIME_MAC_X64_URL="https://github.com/$REPOSITORY/releases/download/v$VERSION/ascii-chat-${VERSION}-macOS-amd64.tar.gz"
RUNTIME_MAC_ARM64_URL="https://github.com/$REPOSITORY/releases/download/v$VERSION/ascii-chat-${VERSION}-macOS-arm64.tar.gz"
LIB_MAC_X64_URL="https://github.com/$REPOSITORY/releases/download/v$VERSION/libasciichat-${VERSION}-macOS-amd64.tar.gz"
LIB_MAC_ARM64_URL="https://github.com/$REPOSITORY/releases/download/v$VERSION/libasciichat-${VERSION}-macOS-arm64.tar.gz"
curl --fail --location --retry 3 --output "$WORK_DIR/ascii-chat.msi" "$MSI_URL"
curl --fail --location --retry 3 --output "$WORK_DIR/ascii-chat-x64.zip" "$RUNTIME_X64_URL"
curl --fail --location --retry 3 --output "$WORK_DIR/ascii-chat-arm64.zip" "$RUNTIME_ARM64_URL"
curl --fail --location --retry 3 --output "$WORK_DIR/libasciichat-x64.zip" "$LIB_X64_URL"
curl --fail --location --retry 3 --output "$WORK_DIR/libasciichat-arm64.zip" "$LIB_ARM64_URL"
curl --fail --location --retry 3 --output "$WORK_DIR/ascii-chat-macos-x64.tar.gz" "$RUNTIME_MAC_X64_URL"
curl --fail --location --retry 3 --output "$WORK_DIR/ascii-chat-macos-arm64.tar.gz" "$RUNTIME_MAC_ARM64_URL"
curl --fail --location --retry 3 --output "$WORK_DIR/libasciichat-macos-x64.tar.gz" "$LIB_MAC_X64_URL"
curl --fail --location --retry 3 --output "$WORK_DIR/libasciichat-macos-arm64.tar.gz" "$LIB_MAC_ARM64_URL"
MSI_SHA256="$(sha256sum "$WORK_DIR/ascii-chat.msi" | awk '{print toupper($1)}')"
RUNTIME_X64_SHA256="$(sha256sum "$WORK_DIR/ascii-chat-x64.zip" | awk '{print toupper($1)}')"
RUNTIME_ARM64_SHA256="$(sha256sum "$WORK_DIR/ascii-chat-arm64.zip" | awk '{print toupper($1)}')"
LIB_X64_SHA256="$(sha256sum "$WORK_DIR/libasciichat-x64.zip" | awk '{print toupper($1)}')"
LIB_ARM64_SHA256="$(sha256sum "$WORK_DIR/libasciichat-arm64.zip" | awk '{print toupper($1)}')"
MSI_SHA1="$(sha1sum "$WORK_DIR/ascii-chat.msi" | awk '{print tolower($1)}')"
RUNTIME_X64_SHA1="$(sha1sum "$WORK_DIR/ascii-chat-x64.zip" | awk '{print tolower($1)}')"
RUNTIME_ARM64_SHA1="$(sha1sum "$WORK_DIR/ascii-chat-arm64.zip" | awk '{print tolower($1)}')"
LIB_X64_SHA1="$(sha1sum "$WORK_DIR/libasciichat-x64.zip" | awk '{print tolower($1)}')"
LIB_ARM64_SHA1="$(sha1sum "$WORK_DIR/libasciichat-arm64.zip" | awk '{print tolower($1)}')"
RUNTIME_MAC_X64_SHA256="$(sha256sum "$WORK_DIR/ascii-chat-macos-x64.tar.gz" | awk '{print tolower($1)}')"
RUNTIME_MAC_ARM64_SHA256="$(sha256sum "$WORK_DIR/ascii-chat-macos-arm64.tar.gz" | awk '{print tolower($1)}')"
LIB_MAC_X64_SHA256="$(sha256sum "$WORK_DIR/libasciichat-macos-x64.tar.gz" | awk '{print tolower($1)}')"
LIB_MAC_ARM64_SHA256="$(sha256sum "$WORK_DIR/libasciichat-macos-arm64.tar.gz" | awk '{print tolower($1)}')"

description='ascii-chat turns your terminal into a tiny enchanted video-chat room: webcams, microphones, multi-person rooms, local and online media, colorful ASCII/glyph/braille rendering, encrypted communication, snapshots, discovery, and modern network transports.'
display_title='ascii-chat - Video chat in your terminal'

submit_pr() {
  local fork="$1" upstream="$2" branch="$3" title="$4" directory="$5"
  git clone --depth 1 "https://x-access-token:$TOKEN@github.com/$fork.git" "$WORK_DIR/repo"
  git -C "$WORK_DIR/repo" checkout -B "$branch"
  if [[ "$TARGET" == "chocolatey" ]]; then
    rm -rf "$WORK_DIR/repo/automatic/ascii-chat" "$WORK_DIR/repo/automatic/libasciichat"
  else
    rm -rf "$WORK_DIR/repo/$directory"
  fi
  mkdir -p "$(dirname "$WORK_DIR/repo/$directory")"
  cp -R "$WORK_DIR/generated/$directory" "$WORK_DIR/repo/$(dirname "$directory")/"
  git -C "$WORK_DIR/repo" config user.name "github-actions[bot]"
  git -C "$WORK_DIR/repo" config user.email "41898282+github-actions[bot]@users.noreply.github.com"
  git -C "$WORK_DIR/repo" add "$directory"
  if git -C "$WORK_DIR/repo" diff --cached --quiet; then
    echo "No $TARGET changes for $VERSION."
    return 0
  fi
  git -C "$WORK_DIR/repo" commit -m "$title"
  git -C "$WORK_DIR/repo" push origin "$branch"
  local head="$(cut -d/ -f1 <<< "$fork"):$branch"
  local pr="$(gh pr list --repo "$upstream" --head "$head" --state open --json number --jq '.[0].number // empty')"
  if [[ -n "$pr" ]]; then
    gh pr edit "$pr" --repo "$upstream" --title "$title" --body "Automated update for ascii-chat $VERSION."
  else
    if ! output="$(gh pr create --repo "$upstream" --head "$head" --base master --title "$title" --body "Automated update for ascii-chat $VERSION" 2>&1)"; then
      if [[ "$output" != *"already exists"* ]]; then
        echo "$output" >&2
        exit 1
      fi
    fi
  fi
}

mkdir -p "$WORK_DIR/generated"
case "$TARGET" in
  homebrew)
    mkdir -p "$WORK_DIR/generated/Formula"
    cat > "$WORK_DIR/generated/Formula/ascii-chat.rb" <<EOF
class AsciiChat < Formula
  desc "Video chat in your terminal"
  homepage "https://ascii-chat.com"
  version "$VERSION"
  license "MIT"

  if Hardware::CPU.arm?
    url "$RUNTIME_MAC_ARM64_URL"
    sha256 "$RUNTIME_MAC_ARM64_SHA256"
  else
    url "$RUNTIME_MAC_X64_URL"
    sha256 "$RUNTIME_MAC_X64_SHA256"
  end

  depends_on "gnupg"
  depends_on "openssh"
  depends_on "yt-dlp"

  def install
    bin.install "bin/ascii-chat"
    man1.install "share/man/man1/ascii-chat.1" \
      if File.exist?("share/man/man1/ascii-chat.1")
    bash_completion.install "share/bash-completion/completions/ascii-chat" \
      if File.exist?("share/bash-completion/completions/ascii-chat")
    zsh_completion.install "share/zsh/site-functions/_ascii-chat" \
      if File.exist?("share/zsh/site-functions/_ascii-chat")
    fish_completion.install "share/fish/vendor_completions.d/ascii-chat.fish" \
      if File.exist?("share/fish/vendor_completions.d/ascii-chat.fish")
  end

  test do
    assert_match "ascii-chat", shell_output("#{bin}/ascii-chat --help 2>&1")
  end
end
EOF
    cat > "$WORK_DIR/generated/Formula/libasciichat.rb" <<EOF
class Libasciichat < Formula
  desc "Reusable C library for ascii-chat applications"
  homepage "https://ascii-chat.com"
  version "$VERSION"
  license "MIT"

  if Hardware::CPU.arm?
    url "$LIB_MAC_ARM64_URL"
    sha256 "$LIB_MAC_ARM64_SHA256"
  else
    url "$LIB_MAC_X64_URL"
    sha256 "$LIB_MAC_X64_SHA256"
  end

  depends_on "ffmpeg"
  depends_on "fftw"
  depends_on "fontconfig"
  depends_on "freetype"
  depends_on "libsodium"
  depends_on "libde265"
  depends_on "libpng"
  depends_on "libwebsockets"
  depends_on "mimalloc"
  depends_on "miniupnpc"
  depends_on "nghttp2"
  depends_on "nghttp3"
  depends_on "ngtcp2"
  depends_on "openssl@3"
  depends_on "opus"
  depends_on "pcre2"
  depends_on "portaudio"
  depends_on "sqlite"
  depends_on "x265"
  depends_on "yyjson"
  depends_on "zstd"

  def install
    prefix.install Dir["*"]
  end

  test do
    assert_path_exists include/"ascii-chat/api.h"
    assert_path_exists lib/"pkgconfig/libasciichat.pc"
    assert_path_exists lib/"cmake/ascii-chat/ascii-chat-config.cmake"
  end
end
EOF
    git clone --depth 1 "https://x-access-token:$TOKEN@github.com/$HOMEBREW_REPOSITORY.git" "$WORK_DIR/repo"
    rm -rf "$WORK_DIR/repo/Formula/ascii-chat.rb" "$WORK_DIR/repo/Formula/libasciichat.rb"
    cp "$WORK_DIR/generated/Formula/ascii-chat.rb" "$WORK_DIR/repo/Formula/ascii-chat.rb"
    cp "$WORK_DIR/generated/Formula/libasciichat.rb" "$WORK_DIR/repo/Formula/libasciichat.rb"
    git -C "$WORK_DIR/repo" config user.name "github-actions[bot]"
    git -C "$WORK_DIR/repo" config user.email "41898282+github-actions[bot]@users.noreply.github.com"
    git -C "$WORK_DIR/repo" add Formula/ascii-chat.rb Formula/libasciichat.rb
    if git -C "$WORK_DIR/repo" diff --cached --quiet; then
      echo "No Homebrew changes for $VERSION."
      exit 0
    fi
    git -C "$WORK_DIR/repo" commit -m "Update ascii-chat packages to $VERSION"
    git -C "$WORK_DIR/repo" push origin HEAD:main
    ;;
  winget)
    mkdir -p "$WORK_DIR/generated/manifests/a/ascii-chat/$VERSION"
    cat > "$WORK_DIR/generated/manifests/a/ascii-chat/$VERSION/ascii-chat.yaml" <<EOF
PackageIdentifier: ascii-chat
PackageVersion: $VERSION
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.10.0
EOF
    cat > "$WORK_DIR/generated/manifests/a/ascii-chat/$VERSION/ascii-chat.locale.en-US.yaml" <<EOF
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
Description: $description
Moniker: ascii-chat
Tags:
- video
- chat
- terminal
- ascii
ManifestType: defaultLocale
ManifestVersion: 1.10.0
EOF
    cat > "$WORK_DIR/generated/manifests/a/ascii-chat/$VERSION/ascii-chat.installer.yaml" <<EOF
PackageIdentifier: ascii-chat
PackageVersion: $VERSION
Installers:
- Architecture: x64
  InstallerType: wix
  InstallerUrl: $MSI_URL
  InstallerSha256: $MSI_SHA256
ManifestType: installer
ManifestVersion: 1.10.0
EOF
    git clone --depth 1 "https://x-access-token:$TOKEN@github.com/$WINGET_FORK_REPOSITORY.git" "$WORK_DIR/repo"
    git -C "$WORK_DIR/repo" checkout -B "ascii-chat-$VERSION"
    rm -rf "$WORK_DIR/repo/manifests/a/ascii-chat/$VERSION"
    mkdir -p "$WORK_DIR/repo/manifests/a/ascii-chat"
    cp -R "$WORK_DIR/generated/manifests/a/ascii-chat/$VERSION" "$WORK_DIR/repo/manifests/a/ascii-chat/"
    git -C "$WORK_DIR/repo" config user.name "github-actions[bot]"
    git -C "$WORK_DIR/repo" config user.email "41898282+github-actions[bot]@users.noreply.github.com"
    git -C "$WORK_DIR/repo" add "manifests/a/ascii-chat/$VERSION"
    git -C "$WORK_DIR/repo" commit -m "New version: ascii-chat version $VERSION"
    git -C "$WORK_DIR/repo" push origin "ascii-chat-$VERSION"
    head="$(cut -d/ -f1 <<< "$WINGET_FORK_REPOSITORY"):ascii-chat-$VERSION"
    pr="$(gh pr list --repo microsoft/winget-pkgs --head "$head" --state open --json number --jq '.[0].number // empty')"
    title="New version: ascii-chat version $VERSION"
    if [[ -n "$pr" ]]; then
      gh pr edit "$pr" --repo microsoft/winget-pkgs --title "$title" --body "Automated update for ascii-chat $VERSION."
    else
      if ! output="$(gh pr create --repo microsoft/winget-pkgs --head "$head" --base master --title "$title" --body "Automated update for ascii-chat $VERSION" 2>&1)"; then
        if [[ "$output" != *"already exists"* ]]; then
          echo "$output" >&2
          exit 1
        fi
      fi
    fi
    ;;
  scoop)
    mkdir -p "$WORK_DIR/generated/bucket"
    cat > "$WORK_DIR/generated/bucket/ascii-chat.json" <<EOF
{
  "version": "$VERSION",
  "description": "$description",
  "homepage": "https://ascii-chat.com",
  "license": "MIT",
  "architecture": {
    "64bit": { "url": "$RUNTIME_X64_URL", "hash": "$RUNTIME_X64_SHA256" },
    "arm64": { "url": "$RUNTIME_ARM64_URL", "hash": "$RUNTIME_ARM64_SHA256" }
  },
  "extract_dir": "ascii-chat",
  "bin": "ascii-chat.exe",
  "checkver": { "github": "https://github.com/$REPOSITORY" },
  "autoupdate": {
    "url": "https://github.com/$REPOSITORY/releases/download/v\$version/ascii-chat-\$version-Windows-amd64.zip",
    "hash": { "url": "https://github.com/$REPOSITORY/releases/download/v\$version/SHA256SUMS" }
  }
}
EOF
    cat > "$WORK_DIR/generated/bucket/libasciichat.json" <<EOF
{
  "version": "$VERSION",
  "description": "The reusable C library behind ascii-chat: encrypted ACIP networking, media, audio, rendering, and cross-platform terminal primitives.",
  "homepage": "https://ascii-chat.com",
  "license": "MIT",
  "architecture": {
    "64bit": { "url": "$LIB_X64_URL", "hash": "$LIB_X64_SHA256" },
    "arm64": { "url": "$LIB_ARM64_URL", "hash": "$LIB_ARM64_SHA256" }
  },
  "extract_dir": "libasciichat",
  "checkver": { "github": "https://github.com/$REPOSITORY" }
}
EOF
    submit_pr "$SCOOP_REPOSITORY" "$SCOOP_REPOSITORY" "ascii-chat-$VERSION" "Update ascii-chat packages to $VERSION" "bucket"
    ;;
  chocolatey)
    mkdir -p "$WORK_DIR/generated/automatic/ascii-chat/tools" "$WORK_DIR/generated/automatic/libasciichat/tools"
    cat > "$WORK_DIR/generated/automatic/ascii-chat/ascii-chat.nuspec" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2015/06/nuspec.xsd">
  <metadata><id>ascii-chat</id><version>$VERSION</version><title>$display_title</title><authors>Zachary Fogg</authors><owners>Zachary Fogg</owners><license type="expression">MIT</license><projectUrl>https://ascii-chat.com</projectUrl><requireLicenseAcceptance>false</requireLicenseAcceptance><description>$description</description><tags>video chat terminal ascii webcam microphone</tags></metadata>
</package>
EOF
cat > "$WORK_DIR/generated/automatic/ascii-chat/tools/chocolateyInstall.ps1" <<EOF
\$arm64 = \$env:PROCESSOR_ARCHITEW6432 -eq 'ARM64'
\$url = if (\$arm64) { '$RUNTIME_ARM64_URL' } else { '$RUNTIME_X64_URL' }
\$hash = if (\$arm64) { '$RUNTIME_ARM64_SHA256' } else { '$RUNTIME_X64_SHA256' }
Install-ChocolateyZipPackage -PackageName 'ascii-chat' -Url \$url -UnzipLocation (Join-Path (Get-ToolsLocation) 'ascii-chat') -Checksum \$hash -ChecksumType 'sha256'
Install-BinFile -Name 'ascii-chat' -Path (Join-Path (Get-ToolsLocation) 'ascii-chat\ascii-chat.exe')
EOF
    cat > "$WORK_DIR/generated/automatic/libasciichat/libasciichat.nuspec" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2015/06/nuspec.xsd">
  <metadata><id>libasciichat</id><version>$VERSION</version><title>libasciichat</title><authors>Zachary Fogg</authors><owners>Zachary Fogg</owners><license type="expression">MIT</license><projectUrl>https://ascii-chat.com</projectUrl><requireLicenseAcceptance>false</requireLicenseAcceptance><description>Development headers, libraries, and CMake integration files for libasciichat.</description><tags>c library ascii chat development</tags></metadata>
</package>
EOF
cat > "$WORK_DIR/generated/automatic/libasciichat/tools/chocolateyInstall.ps1" <<EOF
\$arm64 = \$env:PROCESSOR_ARCHITEW6432 -eq 'ARM64'
\$url = if (\$arm64) { '$LIB_ARM64_URL' } else { '$LIB_X64_URL' }
\$hash = if (\$arm64) { '$LIB_ARM64_SHA256' } else { '$LIB_X64_SHA256' }
Install-ChocolateyZipPackage -PackageName 'libasciichat' -Url \$url -UnzipLocation (Join-Path (Get-ToolsLocation) 'libasciichat') -Checksum \$hash -ChecksumType 'sha256'
EOF
    submit_pr "$CHOCOLATEY_FORK" "chocolatey-community/chocolatey-packages" "ascii-chat-$VERSION" "Update ascii-chat packages to $VERSION" "automatic"
    ;;
  npackd)
    mkdir -p "$WORK_DIR/generated"
    cat > "$WORK_DIR/generated/repository.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<root>
  <spec-version>3.5</spec-version>
  <package name="ascii-chat"><title>$display_title</title><url>https://ascii-chat.com</url><description>$description</description></package>
  <package name="ascii-chat-arm64"><title>$display_title (ARM64)</title><url>https://ascii-chat.com</url><description>$description</description></package>
  <package name="libasciichat"><title>libasciichat</title><url>https://ascii-chat.com</url><description>Development headers, libraries, and CMake integration files for libasciichat.</description></package>
  <package name="libasciichat-arm64"><title>libasciichat (ARM64)</title><url>https://ascii-chat.com</url><description>Development headers, libraries, and CMake integration files for libasciichat.</description></package>
  <version name="$VERSION" package="ascii-chat" type="zip"><url>$RUNTIME_X64_URL</url><hash-sum type="SHA-256">$RUNTIME_X64_SHA256</hash-sum></version>
  <version name="$VERSION" package="ascii-chat-arm64" type="zip"><url>$RUNTIME_ARM64_URL</url><hash-sum type="SHA-256">$RUNTIME_ARM64_SHA256</hash-sum></version>
  <version name="$VERSION" package="libasciichat" type="zip"><url>$LIB_X64_URL</url><hash-sum type="SHA-256">$LIB_X64_SHA256</hash-sum></version>
  <version name="$VERSION" package="libasciichat-arm64" type="zip"><url>$LIB_ARM64_URL</url><hash-sum type="SHA-256">$LIB_ARM64_SHA256</hash-sum></version>
</root>
EOF
    git clone --depth 1 "https://x-access-token:$TOKEN@github.com/$N_PACKD_REPOSITORY.git" "$WORK_DIR/repo"
    cp "$WORK_DIR/generated/repository.xml" "$WORK_DIR/repo/repository.xml"
    git -C "$WORK_DIR/repo" config user.name "github-actions[bot]"
    git -C "$WORK_DIR/repo" config user.email "41898282+github-actions[bot]@users.noreply.github.com"
    git -C "$WORK_DIR/repo" add repository.xml
    git -C "$WORK_DIR/repo" commit -m "Update ascii-chat packages to $VERSION"
    git -C "$WORK_DIR/repo" push origin HEAD:main
    ;;
  *) echo "Unknown target: $TARGET" >&2; exit 2 ;;
esac
