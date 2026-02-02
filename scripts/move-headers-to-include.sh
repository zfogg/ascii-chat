#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LIB_DIR="$REPO_ROOT/lib"
INCLUDE_DIR="$REPO_ROOT/include/ascii-chat"

# Create include directory
rm -rf "$REPO_ROOT/include"
mkdir -p "$INCLUDE_DIR"

echo "Moving headers from lib/ to include/ascii-chat/..."

# Move top-level headers
for file in "$LIB_DIR"/*.h "$LIB_DIR"/*.h.in; do
    if [ -f "$file" ]; then
        filename=$(basename "$file")
        echo "  $filename"
        mv "$file" "$INCLUDE_DIR/$filename"
    fi
done

# Move subdirectory headers (preserve directory structure)
cd "$LIB_DIR"
find . -type f \( -name "*.h" -o -name "*.h.in" \) | while read -r header; do
    # Remove leading ./
    rel_path="${header#./}"
    target_dir="$INCLUDE_DIR/$(dirname "$rel_path")"

    mkdir -p "$target_dir"
    echo "  $rel_path"
    mv "$LIB_DIR/$rel_path" "$INCLUDE_DIR/$rel_path"
done

echo "✓ Headers moved to include/ascii-chat/"
echo ""
echo "Directory structure:"
find "$INCLUDE_DIR" -type d -not -path '*/\.*' | head -20
