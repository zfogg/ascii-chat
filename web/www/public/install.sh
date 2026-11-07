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
  gum=
  for candidate in "$work/gum/gum" "$work/gum"/*/gum; do
    [ -f "$candidate" ] || continue
    [ -z "$gum" ] || fail 'Gum archive contained multiple executables.'
    gum=$candidate
  done
  [ -n "$gum" ] || fail 'Gum archive did not contain its executable.'
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
  # Exclude links from an older managed installation while selecting native tools.
  native_path=
  old_ifs=$IFS
  IFS=:
  for path_entry in $PATH; do
    [ "$path_entry" = "$prefix/bin" ] && continue
    [ "$path_entry" = "$target/bin" ] && continue
    if [ -z "$native_path" ]; then native_path=$path_entry; else native_path="$native_path:$path_entry"; fi
  done
  IFS=$old_ifs
  libc=glibc
  if [ "$os" = Linux ] && { [ -e /etc/alpine-release ] || ldd --version 2>&1 | grep -qi musl; }; then libc=musl; fi
  native_yt=0
  native_deno=0
  legacy_deno=0
  manager=
  if [ "$os" = Linux ] && command -v apk >/dev/null 2>&1; then manager=apk
  elif command -v brew >/dev/null 2>&1 && [ "$(id -u)" != 0 ]; then manager=brew
  elif [ "$os" = Linux ] && command -v apt-get >/dev/null 2>&1; then manager=apt
  elif [ "$os" = Linux ] && command -v dnf >/dev/null 2>&1; then manager=dnf
  elif [ "$os" = Linux ] && command -v yum >/dev/null 2>&1; then manager=yum
  elif [ "$os" = Linux ] && command -v zypper >/dev/null 2>&1; then manager=zypper
  elif [ "$os" = Linux ] && command -v pacman >/dev/null 2>&1; then manager=pacman
  fi
  install_native() {
    case "$manager" in
      brew) brew install "$1" ;;
      apk|apt|dnf|yum|zypper|pacman)
        if [ "$(id -u)" = 0 ]; then run_package_command() { "$@"; }
        elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then run_package_command() { sudo -n "$@"; }
        else return 1; fi
        case "$manager" in
          apk) run_package_command apk add "$1" ;;
          apt) run_package_command apt-get install -y --no-install-recommends "$1" ;;
          dnf) run_package_command dnf install -y --setopt=install_weak_deps=False "$1" ;;
          yum) run_package_command yum install -y --setopt=install_weak_deps=False "$1" ;;
          zypper) run_package_command zypper --non-interactive install "$1" ;;
          pacman) run_package_command pacman -S --needed --noconfirm "$1" ;;
        esac ;;
      *) return 1 ;;
    esac
  }
  deno_usable() {
    local output name version major minor remainder
    output=$("$1" --version 2>/dev/null) || return 1
    read -r name version remainder <<< "$output"
    [ "$name" = deno ] || return 1
    major=${version%%.*}
    version=${version#*.}
    minor=${version%%.*}
    case "$major:$minor" in *[!0-9:]*|:*|*:) return 1 ;; esac
    [ "$major" -gt 2 ] || { [ "$major" -eq 2 ] && [ "$minor" -ge 3 ]; }
  }
  find_native_tool() {
    local IFS=: search_dir candidate
    for search_dir in $PATH; do
      candidate="${search_dir:-.}/$1"
      [ -x "$candidate" ] || continue
      if [ -L "$candidate" ] && [ "$(readlink "$candidate")" = "$target/bin/$1" ]; then continue; fi
      printf '%s\n' "$candidate"
      return 0
    done
    return 1
  }
  # Try native packages first, then verify their actual runtime capabilities.
  if [ -n "$manager" ]; then
    printf 'Trying native %s package for yt-dlp.\n' "$manager"
    install_native yt-dlp || true
    candidate_deno=$(find_native_tool deno || true)
    if [ -z "$candidate_deno" ] || ! deno_usable "$candidate_deno"; then
      install_native deno || true
    fi
  fi
  native_yt_cmd=$(find_native_tool yt-dlp || true)
  native_deno_cmd=$(find_native_tool deno || true)
  if [ -n "$native_deno_cmd" ] && deno_usable "$native_deno_cmd"; then native_deno=1; fi
  if [ "$native_deno" != 1 ]; then
    if [ "$libc" = musl ]; then fail 'A native Deno is required on musl; install the deno package and rerun.'; fi
    command -v unzip >/dev/null || fail 'Required command missing: unzip (needed to install Deno).'
    case "$os/$arch" in
      Linux/amd64) deno_asset=deno-x86_64-unknown-linux-gnu.zip ;;
      Linux/arm64) deno_asset=deno-aarch64-unknown-linux-gnu.zip ;;
      macOS/amd64) deno_asset=deno-x86_64-apple-darwin.zip ;;
      macOS/arm64) deno_asset=deno-aarch64-apple-darwin.zip ;;
    esac
    deno_release=$(curl -fsSL --retry 3 --connect-timeout 20 --max-time 60 --proto '=https' --proto-redir '=https' -o /dev/null -w '%{url_effective}' https://github.com/denoland/deno/releases/latest)
    deno_tag=${deno_release##*/}
    case "$deno_tag" in ''|*[!a-zA-Z0-9._-]*) fail 'Invalid Deno release tag.' ;; esac
    deno_base="https://github.com/denoland/deno/releases/download/$deno_tag"
    download "$deno_base/$deno_asset" "$work/deno.zip"
    download "$deno_base/$deno_asset.sha256sum" "$work/deno.sha256sum"
    read -r deno_hash remainder < "$work/deno.sha256sum" || fail 'Deno checksum missing.'
    [ -n "$deno_hash" ] && [ "$(sha256 "$work/deno.zip")" = "$deno_hash" ] || fail 'Deno checksum mismatch.'
    unzip -q "$work/deno.zip" -d "$work/deno"
    [ -f "$work/deno/deno" ] || fail 'Deno archive is missing its executable.'
    mv "$work/deno/deno" "$work/new/bin/deno"
    chmod +x "$work/new/bin/deno"
    if ! deno_usable "$work/new/bin/deno"; then
      [ "$os" = Linux ] || fail 'Deno 2.3 or newer could not run on this system.'
      # Deno 2.3.3 is the last upstream release before Linux builds began
      # requiring glibc 2.27; yt-dlp currently supports Deno 2.3 and newer.
      case "$arch" in
        amd64) legacy_deno_hash=cb573ef18e06b6f17db31b7a410143abfebe0c8d1faa99cb934a7c1351c75701 ;;
        arm64) legacy_deno_hash=5d9dc69fbf324afe62f2c03c1241ae4629fecdb6032a1ded8a907ce480077f61 ;;
      esac
      printf 'Current Deno cannot run; trying Deno 2.3.3 for older Linux systems.\n' >&2
      download "https://github.com/denoland/deno/releases/download/v2.3.3/$deno_asset" "$work/deno-legacy.zip"
      [ "$(sha256 "$work/deno-legacy.zip")" = "$legacy_deno_hash" ] || fail 'Deno 2.3.3 checksum mismatch.'
      unzip -qo "$work/deno-legacy.zip" -d "$work/deno"
      mv -f "$work/deno/deno" "$work/new/bin/deno"
      chmod +x "$work/new/bin/deno"
      deno_usable "$work/new/bin/deno" || fail 'Neither current Deno nor Deno 2.3.3 could run on this system.'
      legacy_deno=1
    fi
  fi
  if [ -n "$native_yt_cmd" ]; then
    probe_path="$work/new/bin:$native_path"
    if [ "$native_deno" = 1 ]; then probe_path="$(dirname "$native_deno_cmd"):$probe_path"; fi
    yt_probe=$(PATH="$probe_path" "$native_yt_cmd" --ignore-config --verbose --simulate invalid: 2>&1 || true)
    case "$yt_probe" in
      *yt_dlp_ejs*'JS runtimes: deno'*|*yt_dlp_ejs*'JS runtimes: '*deno*) native_yt=1 ;;
    esac
  fi
  if [ "$native_yt" = 1 ]; then
    printf 'Using package-managed yt-dlp.\n'
  else
    printf 'Installing upstream yt-dlp.\n'
    # Install a verified upstream standalone yt-dlp alongside the release.
    case "$os/$arch" in
      Linux/amd64) if [ "$libc" = musl ]; then yt_asset=yt-dlp_musllinux; else yt_asset=yt-dlp_linux; fi ;;
      Linux/arm64) if [ "$libc" = musl ]; then yt_asset=yt-dlp_musllinux_aarch64; else yt_asset=yt-dlp_linux_aarch64; fi ;;
      macOS/*) yt_asset=yt-dlp_macos ;;
    esac
    yt_release=$(curl -fsSL --retry 3 --connect-timeout 20 --max-time 60 --proto '=https' --proto-redir '=https' -o /dev/null -w '%{url_effective}' https://github.com/yt-dlp/yt-dlp/releases/latest)
    yt_tag=${yt_release##*/}
    case "$yt_tag" in ''|*[!a-zA-Z0-9._-]*) fail 'Invalid yt-dlp release tag.' ;; esac
    yt_base="https://github.com/yt-dlp/yt-dlp/releases/download/$yt_tag"
    download "$yt_base/SHA2-256SUMS" "$work/yt-sums"
    download "$yt_base/$yt_asset" "$work/new/bin/yt-dlp"
    yt_hash=
    while read -r hash name; do
      name=${name#\*}
      if [ "$name" = "$yt_asset" ]; then yt_hash=$hash; break; fi
    done < "$work/yt-sums"
    [ -n "$yt_hash" ] && [ "$(sha256 "$work/new/bin/yt-dlp")" = "$yt_hash" ] || fail 'yt-dlp checksum mismatch.'
    chmod +x "$work/new/bin/yt-dlp"
    "$work/new/bin/yt-dlp" --version >/dev/null 2>&1 || fail 'The downloaded yt-dlp could not run on this system.'
  fi
  if [ "$legacy_deno" = 1 ]; then
    if [ "$native_yt" = 1 ]; then yt_verify=$native_yt_cmd; else yt_verify="$work/new/bin/yt-dlp"; fi
    yt_probe=$(PATH="$work/new/bin:$native_path" "$yt_verify" --ignore-config --verbose --simulate invalid: 2>&1 || true)
    case "$yt_probe" in
      *yt_dlp_ejs*'JS runtimes: deno'*|*yt_dlp_ejs*'JS runtimes: '*deno*) ;;
      *) fail 'The installed yt-dlp does not support Deno 2.3.3; install a compatible native runtime.' ;;
    esac
    printf 'Installed older Deno 2.3.3 for compatibility. Update it when your system supports a newer release.\n' >&2
  fi
  printf '%s\n' "$tag" > "$work/new/.ascii-chat-installer"
  if [ -d "$target" ]; then mv "$target" "$work/previous"; fi
  replaced=1
  mv "$work/new" "$target"
  ln -sfn "$target/bin/ascii-chat" "$link"
  committed=1
  # Remove only links created by an earlier run of this installer.
  for tool_name in yt-dlp deno; do
    tool_link="$prefix/bin/$tool_name"
    if [ -L "$tool_link" ] && [ "$(readlink "$tool_link")" = "$target/bin/$tool_name" ]; then
      rm -f "$tool_link"
    fi
    if [ -x "$target/bin/$tool_name" ]; then
      if [ ! -e "$tool_link" ] && [ ! -L "$tool_link" ]; then
        ln -s "$target/bin/$tool_name" "$tool_link"
      else
        printf 'Existing %s retained; ensure the intended command is on PATH.\n' "$tool_link"
      fi
    fi
  done
  "$gum" style --foreground 86 "Installed $tag → $link" 'Ready to chat: ascii-chat'
  ascii_chat_configure_path "$prefix/bin"
  printf '\nBinary: %s\n' "$link"
)
ascii_chat_install
