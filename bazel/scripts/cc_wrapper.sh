#!/bin/bash
# Wrapper script to ensure Bazel uses clang without ccache interference
# Bazel has its own caching, so ccache is redundant
export CCACHE_DISABLE=1
exec clang "$@"
