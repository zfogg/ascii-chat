#!/bin/bash
# Watch header files, C files, and doxygen docs for changes and rebuild documentation
# Requires: entr (install with: sudo pacman -S entr / brew install entr)

set -e

cd "$(dirname "$0")/.."

DOCS_BUILD_DIR="${ASCII_CHAT_DOCS_BUILD_DIR:-build_docs}"

if [ ! -f "$DOCS_BUILD_DIR/CMakeCache.txt" ]; then
  cmake -S docs -B "$DOCS_BUILD_DIR" -DASCII_CHAT_SOURCE_DIR="$PWD"
fi

echo "Watching for changes to rebuild documentation..."
echo "Files being watched:"
echo "  - include/**/*.h (except generated manpage content)"
echo "  - lib/**/*.{h,c} (except generated manpage content)"
echo "  - src/**/*.{h,c} (except version.s)"
echo "  - docs/**/*.dox"
echo "  - docs/Doxyfile.in"
echo "  - docs/CMakeLists.txt"
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
  echo "docs/Doxyfile.in"
  echo "docs/CMakeLists.txt"
} | entr -c cmake --build "$DOCS_BUILD_DIR" --target docs
