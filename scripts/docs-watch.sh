#!/bin/bash
# Watch header files, C files, and doxygen docs for changes and rebuild documentation
# Requires: entr (install with: sudo pacman -S entr / brew install entr)

set -e

cd "$(dirname "$0")/.."

echo "Watching for changes to rebuild documentation..."
echo "Files being watched:"
echo "  - include/**/*.h"
echo "  - lib/**/*.{h,c}"
echo "  - src/**/*.{h,c}"
echo "  - docs/**/*.dox"
echo "  - Doxyfile"
echo ""
echo "Press Ctrl+C to stop"
echo ""

# Find all files to watch and pipe to entr
{
  find include lib src -type f \( -name "*.h" -o -name "*.c" \)
  find docs -type f -name "*.dox"
  echo "Doxyfile"
} | entr -c cmake --build build --target docs
