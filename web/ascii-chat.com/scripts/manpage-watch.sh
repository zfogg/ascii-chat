#!/bin/bash
# vim: ft=bash


set -o pipefail

cd "$(dirname "$0")/.."

cmake -S ../ascii-chat -B ../ascii-chat/build_release --preset release

command -p ls ../ascii-chat/{src,lib,include,share/man}/**/*.{c,h,1.in,5.in} | entr -rc ./scripts/manpage-build.sh

