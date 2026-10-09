#!/usr/bin/env bash
# Download the latest release. Requires Bash 3.2+, curl, and tar.
# ASCII_CHAT_INSTALL_PREFIX overrides ~/.local (or /usr/local when root).
# ASCII_CHAT_VERSION pins a release, e.g. v0.12.17.
ascii_chat_install() (
  set -eu
  umask 022
  fail() { printf 'ascii-chat: %s\n' "$*" >&2; exit 1; }
  for cmd in curl tar mktemp; do
    command -v "$cmd" >/dev/null || fail "Required command missing: $cmd"
  done
  case "$(uname -s)" in
    Linux) os=Linux; gum_os=Linux ;;
    Darwin) os=macOS; gum_os=Darwin ;;
    *) fail 'Use install.ps1 on Windows. Supported Unix systems: Linux and macOS.' ;;
  esac
  case "$(uname -m)" in
    x86_64|amd64) arch=amd64; gum_arch=x86_64 ;;
    arm64|aarch64) arch=arm64; gum_arch=arm64 ;;
    *) fail 'Supported architectures: x86_64 and ARM64.' ;;
  esac
  prefix=${ASCII_CHAT_INSTALL_PREFIX:-${HOME}/.local}
  if [ "$(id -u)" = 0 ]; then prefix=${ASCII_CHAT_INSTALL_PREFIX:-/usr/local}; fi
  case "$prefix" in /*) ;; *) fail 'ASCII_CHAT_INSTALL_PREFIX must be an absolute path.' ;; esac
  mkdir -p "$prefix/lib" "$prefix/bin"
  prefix=$(cd "$prefix" && pwd -P)
  target="$prefix/lib/ascii-chat"
  link="$prefix/bin/ascii-chat"
  if [ -e "$target" ] || [ -L "$target" ]; then
    [ ! -L "$target" ] && [ -f "$target/.ascii-chat-installer" ] || fail "Refusing to replace unmanaged directory: $target"
  fi
  if [ -e "$link" ] || [ -L "$link" ]; then
    [ -L "$link" ] && [ "$(readlink "$link")" = "$target/bin/ascii-chat" ] || fail "Existing command is not managed by this installer: $link"
  fi
  work=$(mktemp -d "$prefix/lib/.ascii-chat-install.XXXXXX")
  replaced=0
  committed=0
  # Called indirectly by the EXIT trap.
  # shellcheck disable=SC2329
  cleanup() {
    result=$?
    trap - EXIT
    if [ "$replaced" = 1 ] && [ "$committed" = 0 ]; then
      rm -rf "$target"
      if [ -d "$work/previous" ]; then mv "$work/previous" "$target"; fi
    fi
    rm -rf "$work"
    exit "$result"
  }
  trap cleanup EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  download() { curl --fail --silent --show-error --location --retry 3 --connect-timeout 20 --max-time 300 --proto '=https' --proto-redir '=https' "$1" -o "$2"; }
  sha256() {
    if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d ' ' -f 1
    else shasum -a 256 "$1" | cut -d ' ' -f 1; fi
  }

  # Gum is private to this invocation and is removed by the EXIT trap.
  case "$gum_os/$gum_arch" in
    Linux/x86_64) gum_hash=d842e06d93dbed90af48cb8dd10698db6f22e331fc40346bb37bbc753109edc2 ;;
    Linux/arm64) gum_hash=8ebf8b54ec1e8c81f2bb58b59ff9b70998186a4d11375f0cf357b80e0ccfa1d5 ;;
    Darwin/x86_64) gum_hash=5374966c7c7199ea879fcaa525ddc6d447a098d3d35496e430a9a1ef38d30485 ;;
    Darwin/arm64) gum_hash=4777a69b1170b8db23c95d5889fb32186cfda1a3ac950d339aa17e3513633890 ;;
  esac
  download "https://github.com/charmbracelet/gum/releases/download/v2.0.2/gum_2.0.2_${gum_os}_${gum_arch}.tar.gz" "$work/gum.tar.gz"
  [ "$(sha256 "$work/gum.tar.gz")" = "$gum_hash" ] || fail 'Gum checksum mismatch.'
  mkdir "$work/gum"
  tar -xzf "$work/gum.tar.gz" -C "$work/gum"
  gum=$(find "$work/gum" -type f -name gum -print)
  [ -f "$gum" ] || fail 'Gum archive did not contain its executable.'
  "$gum" style --border rounded --padding '1 2' --border-foreground 86 'ascii-chat' "Install for $os / $arch"

  tag=${ASCII_CHAT_VERSION:-}
  if [ -z "$tag" ]; then
    release_url=$(curl -fsSL --retry 3 --connect-timeout 20 --max-time 60 --proto '=https' --proto-redir '=https' -o /dev/null -w '%{url_effective}' https://github.com/zfogg/ascii-chat/releases/latest)
    tag=${release_url##*/}
  fi
  case "$tag" in v[0-9]*) ;; *) fail "Invalid release tag: $tag" ;; esac
  case "$tag" in *[!a-zA-Z0-9._-]*) fail 'Invalid release tag characters.' ;; esac
  archive="ascii-chat-${tag#v}-${os}-${arch}.tar.gz"
  printf 'Downloading %s\n' "$archive"
  download "https://github.com/zfogg/ascii-chat/releases/download/$tag/$archive" "$work/release.tar.gz"
  mkdir "$work/new"
  tar -xzf "$work/release.tar.gz" -C "$work/new"
  [ -x "$work/new/bin/ascii-chat" ] || fail 'Release archive is missing bin/ascii-chat.'
  "$work/new/bin/ascii-chat" --version
  printf '%s\n' "$tag" > "$work/new/.ascii-chat-installer"
  if [ -d "$target" ]; then mv "$target" "$work/previous"; fi
  replaced=1
  mv "$work/new" "$target"
  ln -sfn "$target/bin/ascii-chat" "$link"
  committed=1
  "$gum" style --foreground 86 "Installed $tag → $link" 'Ready to chat: ascii-chat'
  # Print a literal $PATH for the user's shell profile.
  # shellcheck disable=SC2016
  case ":$PATH:" in
    *":$prefix/bin:"*) ;;
    *) printf '\nAdd this to your shell profile, then open a new terminal:\n  export PATH=%q:"$PATH"\n' "$prefix/bin" ;;
  esac
)
ascii_chat_install
