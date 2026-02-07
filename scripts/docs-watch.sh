#!/bin/bash
# Watch header files, C files, and doxygen docs for changes and rebuild documentation
# Requires: entr (install with: sudo pacman -S entr / brew install entr)

set -e

cd "$(dirname "$0")/.."

echo "Watching for changes to rebuild documentation..."
echo "Files being watched:"
echo "  - include/**/*.h (except generated manpage content)"
echo "  - lib/**/*.{h,c} (except generated manpage content)"
echo "  - src/**/*.{h,c} (except version.s)"
echo "  - docs/**/*.dox"
echo "  - Doxyfile"
echo ""
echo "Ignoring:"
echo "  - */manpage/content/* (generated)"
echo "  - version.h* (generated)"
echo ""
echo "Press Ctrl+C to stop"
echo ""

# Find all files to watch and pipe to entr
# Exclude generated files: version.h* and manpage content
{
  find include lib src -type f \( -name "*.h" -o -name "*.c" \) \
    -not -path "*/manpage/content/*" \
    -not -name "version.h*"
  find docs -type f -name "*.dox"
  echo "Doxyfile"
} | entr -c cmake --build build --target docs
