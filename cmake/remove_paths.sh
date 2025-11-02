#!/bin/bash
# Fast binary path removal script for Linux
# Replaces absolute paths with relative paths in-place
# Usage: ./remove_paths.sh <binary_path> <source_dir> [build_dir]

set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 <binary_path> <source_dir> [build_dir]" >&2
    exit 1
fi

BINARY_PATH="$1"
SOURCE_DIR="$2"
BUILD_DIR="${3:-}"

# Check if binary exists
if [ ! -f "$BINARY_PATH" ]; then
    echo "Error: Binary not found: $BINARY_PATH" >&2
    exit 1
fi

# Validate source directory
if [ ! -d "$SOURCE_DIR" ]; then
    echo "Error: Source directory not found: $SOURCE_DIR" >&2
    exit 1
fi

echo "Removing embedded paths from: $BINARY_PATH"

# Resolve to absolute paths
SOURCE_PATH=$(cd "$SOURCE_DIR" && pwd)
echo "Source directory: $SOURCE_PATH"
if [ -n "$BUILD_DIR" ]; then
    echo "Build directory: $BUILD_DIR"
fi

# Get home directory
HOME_DIR="${HOME:-}"

# Create temporary file for replacements
TEMP_FILE="${BINARY_PATH}.tmp"
cp "$BINARY_PATH" "$TEMP_FILE"

# Count replacements
TOTAL_REPLACEMENTS=0

# Function to replace paths in binary
# Uses null-terminated replacement to avoid corruption
replace_path() {
    local old_path="$1"
    local file="$2"

    # Check if path exists in binary
    if strings "$file" | grep -qF "$old_path"; then
        # Count occurrences
        local count=$(strings "$file" | grep -cF "$old_path" || true)

        if [ "$count" -gt 0 ]; then
            echo "  Replacing $count occurrence(s) of: $old_path"

            # Use perl for binary-safe in-place replacement
            # Replace path+slash with null bytes to maintain file size
            # Calculate length and generate null padding in perl
            local path_len=$((${#old_path} + 1))  # +1 for the trailing slash
            perl -pi -e 's|\Q'"$old_path"'\E/|"\x00" x '"$path_len"'|ge' "$file" 2>/dev/null || true

            TOTAL_REPLACEMENTS=$((TOTAL_REPLACEMENTS + count))
        fi
    fi
}

# First, remove any .deps-cache paths (from dependency builds)
# These appear in static libraries from third-party deps
# Do this FIRST before removing source paths
DEPS_CACHE_PATH_1="${SOURCE_PATH}/.deps-cache"
DEPS_CACHE_PATH_2="${SOURCE_PATH}/.deps-cache-musl"
if strings "$TEMP_FILE" | grep -qE "\.deps-cache(-musl)?"; then
    echo "  Removing dependency cache paths"
    # Remove the full deps-cache path, leaving just .deps-cache/...
    replace_path "$DEPS_CACHE_PATH_1" "$TEMP_FILE"
    replace_path "$DEPS_CACHE_PATH_2" "$TEMP_FILE"
fi

# Replace source directory paths (both forward and backslash)
replace_path "$SOURCE_PATH" "$TEMP_FILE"

# Replace home directory paths if different from source
if [ -n "$HOME_DIR" ] && [ "$HOME_DIR" != "$SOURCE_PATH" ]; then
    replace_path "$HOME_DIR" "$TEMP_FILE"
fi

# Replace build directory if specified
if [ -n "$BUILD_DIR" ]; then
    BUILD_PATH=$(cd "$BUILD_DIR" 2>/dev/null && pwd || echo "$BUILD_DIR")
    if [ "$BUILD_PATH" != "$SOURCE_PATH" ]; then
        replace_path "$BUILD_PATH" "$TEMP_FILE"
    fi
fi

# Verify file size didn't change (important for binary integrity)
ORIG_SIZE=$(stat -c%s "$BINARY_PATH")
NEW_SIZE=$(stat -c%s "$TEMP_FILE")

if [ "$ORIG_SIZE" -ne "$NEW_SIZE" ]; then
    echo "Error: Binary size changed from $ORIG_SIZE to $NEW_SIZE bytes - would corrupt binary!" >&2
    rm -f "$TEMP_FILE"
    exit 1
fi

if [ "$TOTAL_REPLACEMENTS" -gt 0 ]; then
    # Replace original with modified version
    mv "$TEMP_FILE" "$BINARY_PATH"
    echo "Made $TOTAL_REPLACEMENTS replacements - successfully removed embedded paths from binary"
else
    rm -f "$TEMP_FILE"
    echo "No embedded paths found to remove"
fi

exit 0
