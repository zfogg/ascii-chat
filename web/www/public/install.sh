#!/usr/bin/env bash
# Offer persistent PATH setup without consuming a piped installer on stdin.
# Shell code below must retain literal variable references until startup.
# shellcheck disable=SC2016
ascii_chat_configure_path() (
  bin_dir=$1
  case ":$PATH:" in *":$bin_dir:"*) return 0 ;; esac
  # sudo must not edit root's profiles on behalf of the invoking user.
  if [ -z "${HOME:-}" ] || [ -n "${SUDO_USER:-}" ] || ! { exec 3<>/dev/tty; } 2>/dev/null; then
    printf '\nAdd this to your shell profile:\n  export PATH=%q:"$PATH"\nThen reload your shell with exec $SHELL, or open a new terminal.\n' "$bin_dir"
    return 0
  fi
  updated=0
  for shell_name in bash zsh; do
    shell_path=$(command -v "$shell_name") || continue
    profiles=()
    # Check the PATH after each shell reads its existing startup files.
    # The directory is an argument, never interpolated into shell code.
    probe='case ":$PATH:" in *":$1:"*) exit 0 ;; *) exit 1 ;; esac'
    if ! "$shell_path" -ic "$probe" ascii-chat "$bin_dir" </dev/null >/dev/null 2>&1; then
      if [ "$shell_name" = bash ]; then
        profiles+=("$HOME/.bashrc")
      else
        profiles+=("${ZDOTDIR:-$HOME}/.zshrc")
      fi
    fi
    if ! "$shell_path" -lic "$probe" ascii-chat "$bin_dir" </dev/null >/dev/null 2>&1; then
      if [ "$shell_name" = bash ]; then
        login_profile="$HOME/.bash_profile"
        for candidate in "$HOME/.bash_profile" "$HOME/.bash_login" "$HOME/.profile"; do
          if [ -f "$candidate" ]; then login_profile=$candidate; break; fi
        done
        profiles+=("$login_profile")
      elif [ "${#profiles[@]}" = 0 ]; then
        profiles+=("${ZDOTDIR:-$HOME}/.zshrc")
      fi
    fi
    [ "${#profiles[@]}" -gt 0 ] || continue
    # A guarded block is also safe when a profile is sourced more than once.
    printf -v quoted_dir '%q' "$bin_dir"
    marker="# ascii-chat PATH: $quoted_dir"
    pending=()
    for profile in "${profiles[@]}"; do
      if [ ! -f "$profile" ] || ! grep -Fqx -- "$marker" "$profile"; then
        pending+=("$profile")
      fi
    done
    [ "${#pending[@]}" -gt 0 ] || continue
    printf '\nAdd %s to PATH for %s? [Y/n] ' "$bin_dir" "$shell_name" >&3
    answer=
    IFS= read -r answer <&3 || continue
    case "$answer" in ''|y|Y|yes|YES|Yes) ;; *) continue ;; esac
    for profile in "${pending[@]}"; do
      # Do not follow a profile symlink or replace a non-regular file.
      if [ -L "$profile" ] || { [ -e "$profile" ] && [ ! -f "$profile" ]; }; then
        printf 'Skipping %s: update this profile manually.\n' "$profile" >&3
        continue
      fi
      if ! {
        printf '\n%s\n' "$marker"
        printf 'case ":$PATH:" in\n  *":"%s":"*) ;;\n  *) export PATH=%s:"$PATH" ;;\nesac\n' "$quoted_dir" "$quoted_dir"
      } >> "$profile"; then
        printf 'Could not update %s; add the directory manually.\n' "$profile" >&3
        continue
      fi
      printf 'Updated %s\n' "$profile" >&3
      updated=1
    done
  done
  if [ "$updated" = 1 ]; then
    printf '\nPATH configuration changed. Reload your shell with:\n  exec $SHELL\nOr open a new terminal to use the updated PATH.\n'
  fi
  printf '\nFor this terminal, run:\n  export PATH=%q:"$PATH"\n' "$bin_dir"
)

# Download the latest release. Requires Bash 3.2+, curl, and tar.
# ASCII_CHAT_INSTALL_PREFIX overrides ~/.local (or /usr/local when root).
# ASCII_CHAT_VERSION pins a release, e.g. 0.12.17 (v0.12.17 also works).
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
  if [ "$(id -u)" = 0 ]; then
    prefix=${ASCII_CHAT_INSTALL_PREFIX:-/usr/local}
  else
    prefix=${ASCII_CHAT_INSTALL_PREFIX:-${HOME}/.local}
  fi
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
  if [[ "$tag" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then tag="v$tag"; fi
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
  ascii_chat_configure_path "$prefix/bin"
)
ascii_chat_install
