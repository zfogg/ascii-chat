#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/git-hooks.sh [--symlink | --copy] [--force] [--all | HOOK ...]

Install hooks from git-hooks/ into Git's configured hooks directory.
With no hook names, install all hooks. Names are filenames in git-hooks/.

  --all       Install every hook in git-hooks/
  --symlink   Create symbolic links (default)
  --copy      Copy hook files instead
  -f, --force Replace existing files or symbolic links
  -h, --help  Show this help
EOF
}

mode=symlink
force=false
all=false
hooks=()
while (($#)); do
  case "$1" in
    --symlink) mode=symlink ;;
    --copy) mode=copy ;;
    -f|--force) force=true ;;
    --all) all=true ;;
    -h|--help) usage; exit 0 ;;
    --) shift; hooks+=("$@"); break ;;
    -*) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 1 ;;
    *) hooks+=("$1") ;;
  esac
  shift
done

if $all && ((${#hooks[@]})); then
  printf '%s\n' 'Error: --all cannot be combined with hook names.' >&2
  exit 1
fi
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
source_dir="$root/git-hooks"
hooks_dir=$(git -C "$root" rev-parse --git-path hooks)
case "$hooks_dir" in /*) ;; *) hooks_dir="$root/$hooks_dir" ;; esac
if $all || ((${#hooks[@]} == 0)); then
  for file in "$source_dir"/*; do
    [[ -f "$file" ]] || continue
    hooks+=("${file##*/}")
  done
fi
if ((${#hooks[@]} == 0)); then
  printf 'Error: no hooks found in %s\n' "$source_dir" >&2
  exit 1
fi

# Validate all destinations before changing any of them.
for hook in "${hooks[@]}"; do
  if [[ "$hook" == */* || "$hook" == . || "$hook" == .. || ! -f "$source_dir/$hook" ]]; then
    printf 'Error: hook not found: %s\n' "$hook" >&2
    exit 1
  fi
  destination="$hooks_dir/$hook"
  if [[ -d "$destination" && ! -L "$destination" ]]; then
    printf 'Error: destination is a directory: %s\n' "$destination" >&2
    exit 1
  fi
  if [[ -e "$destination" || -L "$destination" ]]; then
    if ! $force; then
      printf 'Error: %s already exists; pass --force to overwrite it.\n' "$destination" >&2
      exit 1
    fi
  fi
done
mkdir -p "$hooks_dir"
for hook in "${hooks[@]}"; do
  destination="$hooks_dir/$hook"
  if $force; then rm -f -- "$destination"; fi
  if [[ "$mode" == symlink ]]; then
    MSYS="${MSYS:+$MSYS }winsymlinks:nativestrict" ln -s -- "$source_dir/$hook" "$destination"
  else
    cp -- "$source_dir/$hook" "$destination"
    chmod +x "$destination"
  fi
  printf 'Installed %s (%s)\n' "$hook" "$mode"
done