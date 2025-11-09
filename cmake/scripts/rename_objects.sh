#!/bin/bash
# Rename object files to include their path to avoid conflicts in static libraries
# Usage: rename_objects.sh <object_dir> <source_root>

set -e

OBJ_DIR="$1"
SRC_ROOT="$2"

if [[ ! -d "$OBJ_DIR" ]]; then
    echo "Object directory not found: $OBJ_DIR"
    exit 0
fi

# Find all object files (.o / .obj) recursively
find "$OBJ_DIR" \( -name "*.o" -o -name "*.obj" \) -type f | while read -r obj_file; do
    # Get the relative path from OBJ_DIR
    rel_path="${obj_file#$OBJ_DIR/}"

    # Convert path separators to underscores
    new_name="${rel_path//\//_}"
    new_name="${new_name//\\/_}"

    # Get directory of object file
    obj_dir="$(dirname "$obj_file")"

    basename_obj="$(basename "$obj_file")"

    # Skip if already renamed (contains lib_ prefix from source path)
    if [[ "$basename_obj" == lib_* ]] || [[ "$basename_obj" == deps_* ]]; then
        continue
    fi

    # Only rename and copy to root if necessary
    if [[ "$basename_obj" != "$new_name" ]]; then
        # Copy to root of OBJ_DIR with new name (preserve original for Ninja)
        cp "$obj_file" "$OBJ_DIR/$new_name"
        echo "Renamed: $basename_obj -> $new_name"
    fi
done
