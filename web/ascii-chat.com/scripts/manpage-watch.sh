#!/bin/bash
# vim: ft=bash


set -o pipefail

cd "$(dirname "$0")/.."

REPO_ROOT="$(cd ../.. && pwd)"

cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build_release" --preset release

command -p ls "$REPO_ROOT"/{src,lib,include,share/man}/**/*.{c,h,1.in,5.in} | entr -rc ./scripts/manpage-build.sh

