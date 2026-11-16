#!/usr/bin/env bash
set -euo pipefail

# Shared release publisher for WinGet, Homebrew, Scoop, Chocolatey, and Npackd.
# Usage: submit.sh <winget|homebrew|scoop|chocolatey|npackd> <version-without-v> [repository]

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
TEMPLATE_ROOT="${TEMPLATE_ROOT:-deploy/package-managers}"

command -v gomplate >/dev/null || { echo "gomplate is required" >&2; exit 1; }
export VERSION REPOSITORY MSI_URL RUNTIME_X64_URL RUNTIME_ARM64_URL LIB_X64_URL LIB_ARM64_URL
export RUNTIME_MAC_X64_URL RUNTIME_MAC_ARM64_URL LIB_MAC_X64_URL LIB_MAC_ARM64_URL
export MSI_SHA256 RUNTIME_X64_SHA256 RUNTIME_ARM64_SHA256 LIB_X64_SHA256 LIB_ARM64_SHA256
export RUNTIME_MAC_X64_SHA256 RUNTIME_MAC_ARM64_SHA256 LIB_MAC_X64_SHA256 LIB_MAC_ARM64_SHA256
export DESCRIPTION="$description" DISPLAY_TITLE="$display_title"

render_template() {
  local template="$1" output="$2"
  mkdir -p "$(dirname "$output")"
  gomplate --config="" --file "$TEMPLATE_ROOT/$template" --out "$output"
}

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
    render_template homebrew/ascii-chat.rb.tmpl "$WORK_DIR/generated/Formula/ascii-chat.rb"
    render_template homebrew/libasciichat.rb.tmpl "$WORK_DIR/generated/Formula/libasciichat.rb"
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
    render_template winget/ascii-chat.yaml.tmpl "$WORK_DIR/generated/manifests/a/ascii-chat/$VERSION/ascii-chat.yaml"
    render_template winget/ascii-chat.locale.en-US.yaml.tmpl "$WORK_DIR/generated/manifests/a/ascii-chat/$VERSION/ascii-chat.locale.en-US.yaml"
    render_template winget/ascii-chat.installer.yaml.tmpl "$WORK_DIR/generated/manifests/a/ascii-chat/$VERSION/ascii-chat.installer.yaml"
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
    render_template scoop/ascii-chat.json.tmpl "$WORK_DIR/generated/bucket/ascii-chat.json"
    render_template scoop/libasciichat.json.tmpl "$WORK_DIR/generated/bucket/libasciichat.json"
    submit_pr "$SCOOP_REPOSITORY" "$SCOOP_REPOSITORY" "ascii-chat-$VERSION" "Update ascii-chat packages to $VERSION" "bucket"
    ;;
  chocolatey)
    mkdir -p "$WORK_DIR/generated/automatic/ascii-chat/tools" "$WORK_DIR/generated/automatic/libasciichat/tools"
    render_template chocolatey/ascii-chat.nuspec.tmpl "$WORK_DIR/generated/automatic/ascii-chat/ascii-chat.nuspec"
    render_template chocolatey/ascii-chat-install.ps1.tmpl "$WORK_DIR/generated/automatic/ascii-chat/tools/chocolateyInstall.ps1"
    render_template chocolatey/libasciichat.nuspec.tmpl "$WORK_DIR/generated/automatic/libasciichat/libasciichat.nuspec"
    render_template chocolatey/libasciichat-install.ps1.tmpl "$WORK_DIR/generated/automatic/libasciichat/tools/chocolateyInstall.ps1"
    submit_pr "$CHOCOLATEY_FORK" "chocolatey-community/chocolatey-packages" "ascii-chat-$VERSION" "Update ascii-chat packages to $VERSION" "automatic"
    ;;
  npackd)
    mkdir -p "$WORK_DIR/generated"
    render_template npackd/repository.xml.tmpl "$WORK_DIR/generated/repository.xml"
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
