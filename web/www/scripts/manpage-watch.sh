#!/bin/bash
# vim: ft=bash


set -o pipefail

cd "$(dirname "$0")/.."

REPO_ROOT="$(cd ../.. && pwd)"

cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build_release" --preset release

# Watch C sources/headers for man(3) regeneration, plus man(1) and man(5) templates
# Rebuild man pages when files change
find "$REPO_ROOT"/src "$REPO_ROOT"/lib "$REPO_ROOT"/include "$REPO_ROOT"/share/man -type f \( -name "*.c" -o -name "*.h" -o -name "*.1.in" -o -name "*.5.in" \) 2>/dev/null | \
  entr -rd ./scripts/manpage-build.sh

