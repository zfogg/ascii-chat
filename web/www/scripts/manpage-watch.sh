#!/bin/bash
# vim: ft=bash


set -o pipefail

cd "$(dirname "$0")/.."

REPO_ROOT="$(cd ../.. && pwd)"

cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build_release" --preset default

# Rebuild function: regenerate doxygen man3 + build HTML
rebuild_manpages() {
  echo "🔄 Changes detected, rebuilding man pages..."
  # Force doxygen to regenerate by running it directly
  cd "$REPO_ROOT/build_release" && doxygen Doxyfile.man3 && cd - > /dev/null
  # Then convert to HTML
  ./scripts/manpage-build.sh
}

export -f rebuild_manpages

# Watch C sources/headers for man(3) regeneration, plus man(1) and man(5) templates
command -p ls "$REPO_ROOT"/{src,lib,include,share/man}/**/*.{c,h,1.in,5.in} 2>/dev/null | entr -rc bash -c 'rebuild_manpages'

