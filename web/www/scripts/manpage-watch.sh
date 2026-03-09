#!/bin/bash
# vim: ft=bash


set -o pipefail

cd "$(dirname "$0")/.."

REPO_ROOT="$(cd ../.. && pwd)"

cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build_release" --preset default

# Watch C sources/headers for man(3) regeneration, plus man(1) and man(5) templates
# Force doxygen to rebuild and then convert to HTML
find "$REPO_ROOT"/{src,lib,include,share/man} -type f \( -name "*.c" -o -name "*.h" -o -name "*.1.in" -o -name "*.5.in" \) 2>/dev/null | \
  entr -rc bash -c "cd '$REPO_ROOT/build_release' && doxygen Doxyfile.man3 && cd - > /dev/null && ./scripts/manpage-build.sh"

