#!/bin/bash
# =============================================================================
# clang-format runner for ascii-chat
# =============================================================================
# Usage: bazel run //:format
#
# This script formats all C/C++ source files using clang-format.
# =============================================================================

set -e

# Find the repository root (where .clang-format is)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${REPO_ROOT}"

# Check for clang-format
if command -v clang-format &> /dev/null; then
    CLANG_FORMAT="clang-format"
elif command -v clang-format-18 &> /dev/null; then
    CLANG_FORMAT="clang-format-18"
elif command -v clang-format-17 &> /dev/null; then
    CLANG_FORMAT="clang-format-17"
else
    echo "Error: clang-format not found. Please install clang-format."
    exit 1
fi

echo "Using: ${CLANG_FORMAT}"
echo "Formatting source files in ${REPO_ROOT}..."

# Format all C source files
find lib src -name '*.c' -o -name '*.h' | while read -r file; do
    echo "  Formatting: ${file}"
    "${CLANG_FORMAT}" -i "${file}"
done

# Format Objective-C files (macOS webcam)
find lib -name '*.m' | while read -r file; do
    echo "  Formatting: ${file}"
    "${CLANG_FORMAT}" -i "${file}"
done

echo "Done! All files formatted."
