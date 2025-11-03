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

echo "Removing embedded paths from: $BINARY_PATH"

# Convert Windows paths (C:\Users\... or C:/Users/...) to Git Bash paths (/c/Users/...) before resolving
# This allows the script to work with both Windows and Unix-style paths
CONVERTED_SOURCE_DIR="$SOURCE_DIR"

# Handle Windows-style paths with backslashes
if [[ "$SOURCE_DIR" =~ ^([A-Za-z]):\\(.*)$ ]]; then
    DRIVE_LETTER="${BASH_REMATCH[1]}"
    REST_OF_PATH="${BASH_REMATCH[2]}"
    CONVERTED_SOURCE_DIR="/${DRIVE_LETTER,,}/${REST_OF_PATH//\\//}"
    echo "Converted Windows backslash path: $SOURCE_DIR -> $CONVERTED_SOURCE_DIR"
# Handle Windows-style paths with forward slashes
elif [[ "$SOURCE_DIR" =~ ^([A-Za-z]):/(.*)$ ]]; then
    DRIVE_LETTER="${BASH_REMATCH[1]}"
    REST_OF_PATH="${BASH_REMATCH[2]}"
    CONVERTED_SOURCE_DIR="/${DRIVE_LETTER,,}/$REST_OF_PATH"
    echo "Converted Windows forward-slash path: $SOURCE_DIR -> $CONVERTED_SOURCE_DIR"
# Convert WSL paths (/mnt/c/...) to Git Bash paths (/c/...) before resolving
elif [[ "$SOURCE_DIR" =~ ^/mnt/([a-zA-Z])/(.*) ]]; then
    DRIVE_LETTER="${BASH_REMATCH[1]}"
    REST_OF_PATH="${BASH_REMATCH[2]}"
    CONVERTED_SOURCE_DIR="/${DRIVE_LETTER,,}/$REST_OF_PATH"
    echo "Converted WSL source path: $SOURCE_DIR -> $CONVERTED_SOURCE_DIR"
fi

# Resolve to absolute paths (try to cd into it, but don't fail if we can't)
SOURCE_PATH=$(cd "$CONVERTED_SOURCE_DIR" 2>/dev/null && pwd || echo "$CONVERTED_SOURCE_DIR")

# Validate that the source directory exists (try both the converted path and original)
if [ ! -d "$CONVERTED_SOURCE_DIR" ] && [ ! -d "$SOURCE_DIR" ]; then
    # If we can't validate the directory, warn but continue (the path removal will still work)
    echo "Warning: Could not validate source directory: $SOURCE_DIR" >&2
    echo "Warning: Attempted conversion: $CONVERTED_SOURCE_DIR" >&2
    echo "Warning: Continuing anyway - path removal will still be attempted" >&2
fi
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

# Function to remove paths from binary
# Uses null-terminated replacement to avoid corruption
remove_string_from_file() {
    local old_path="$1"
    local file="$2"

    # Use Python to search the binary directly (more reliable than strings command)
    # Python searches raw binary data, not just null-terminated strings
    local count=$(python3 -c "
import sys
old_path_str = sys.argv[1]
old_path = old_path_str.encode('latin1')
file_path = sys.argv[2]

with open(file_path, 'rb') as f:
    data = f.read()

# Count occurrences with both separators
count_with_slash = data.count(old_path + b'/')
count_with_backslash = data.count(old_path + b'\\\\')
total = count_with_slash + count_with_backslash
print(total)
" "$old_path" "$file" 2>/dev/null || echo "0")

    if [ "$count" -gt 0 ]; then
        echo "  Replacing $count occurrence(s) of: $old_path"

        # Use Python for more reliable binary replacement
        # Pass path as command-line argument to avoid bash interpolation issues
        # Note: In bash double quotes, need \\\\ to get a single backslash to Python
        # Calculate path length including separator - both / and \ are 1 byte
        if ! python3 -c "
import sys

old_path_str = sys.argv[1]
old_path = old_path_str.encode('latin1')
file_path = sys.argv[2]

# Read file data
with open(file_path, 'rb') as f:
    data = f.read()

# Replace path with both forward slash and backslash separators
# Each separator is 1 byte, so path + sep = len(old_path) + 1 bytes
for sep in [b'/', b'\\\\']:
    search_path = old_path + sep
    path_len = len(search_path)  # Length includes the separator
    null_bytes = b'\x00' * path_len

    if search_path in data:
        data = data.replace(search_path, null_bytes)

# Write modified data back
with open(file_path, 'wb') as f:
    f.write(data)
" "$old_path" "$file" 2>&1; then
            echo "  Warning: Python replacement failed for: $old_path" >&2
        fi

        TOTAL_REPLACEMENTS=$((TOTAL_REPLACEMENTS + count))
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
    remove_string_from_file "$DEPS_CACHE_PATH_1" "$TEMP_FILE"
    remove_string_from_file "$DEPS_CACHE_PATH_2" "$TEMP_FILE"
fi

# Replace source directory paths (both forward and backslash)
remove_string_from_file "$SOURCE_PATH" "$TEMP_FILE"

# On Windows, also handle Windows-style paths (C:/Users/... format)
# Convert Git Bash path (/c/Users/...) to Windows path (C:/Users/...)
if [[ "$SOURCE_PATH" =~ ^/[a-zA-Z]/(.*) ]]; then
    # Extract drive letter and path
    DRIVE_LETTER="${SOURCE_PATH:1:1}"
    WIN_PATH="${DRIVE_LETTER^^}:${SOURCE_PATH:2}"
    remove_string_from_file "$WIN_PATH" "$TEMP_FILE"
    # Also try with backslashes
    WIN_PATH_BS="${WIN_PATH//\//\\}"
    remove_string_from_file "$WIN_PATH_BS" "$TEMP_FILE"
fi

# Replace home directory paths if different from source
# On Windows, extract home directory from source path (handles both /c/... and /mnt/c/...)
if [[ "$SOURCE_PATH" =~ ^/[a-zA-Z]/Users/([^/]+) ]]; then
    # Extract username and construct home directory paths
    USERNAME="${BASH_REMATCH[1]}"
    DRIVE_LETTER="${SOURCE_PATH:1:1}"
    # Try different home directory formats (forward slash, backslash, and Git Bash format)
    HOME_PATHS=(
        "${DRIVE_LETTER^^}:/Users/${USERNAME}"
        "${DRIVE_LETTER^^}:\\Users\\${USERNAME}"
        "/${DRIVE_LETTER,,}/Users/${USERNAME}"
    )

    echo "Detected home directory paths to remove:"
    for HOME_PATH in "${HOME_PATHS[@]}"; do
        # Always try to replace home directory paths (they're different from source path)
        remove_string_from_file "$HOME_PATH" "$TEMP_FILE"
    done
elif [ -n "$HOME_DIR" ] && [ "$HOME_DIR" != "$SOURCE_PATH" ]; then
    remove_string_from_file "$HOME_DIR" "$TEMP_FILE"
    # On Unix/Linux, also handle Windows-style home path if applicable
    if [[ "$HOME_DIR" =~ ^/[a-zA-Z]/(.*) ]]; then
        DRIVE_LETTER="${HOME_DIR:1:1}"
        WIN_HOME_PATH="${DRIVE_LETTER^^}:${HOME_DIR:2}"
        remove_string_from_file "$WIN_HOME_PATH" "$TEMP_FILE"
        WIN_HOME_PATH_BS="${WIN_HOME_PATH//\//\\}"
        remove_string_from_file "$WIN_HOME_PATH_BS" "$TEMP_FILE"
    fi
fi

# Replace build directory if specified
if [ -n "$BUILD_DIR" ]; then
    BUILD_PATH=$(cd "$BUILD_DIR" 2>/dev/null && pwd || echo "$BUILD_DIR")

    # Convert WSL paths (/mnt/c/...) to Git Bash paths (/c/...) for consistency
    if [[ "$BUILD_PATH" =~ ^/mnt/([a-zA-Z])/(.*) ]]; then
        DRIVE_LETTER="${BASH_REMATCH[1]}"
        REST_OF_PATH="${BASH_REMATCH[2]}"
        BUILD_PATH="/${DRIVE_LETTER,,}/$REST_OF_PATH"
    fi

    if [ "$BUILD_PATH" != "$SOURCE_PATH" ]; then
        remove_string_from_file "$BUILD_PATH" "$TEMP_FILE"
        # On Windows, also handle Windows-style build path
        if [[ "$BUILD_PATH" =~ ^/[a-zA-Z]/(.*) ]]; then
            DRIVE_LETTER="${BUILD_PATH:1:1}"
            WIN_BUILD_PATH="${DRIVE_LETTER^^}:${BUILD_PATH:2}"
            remove_string_from_file "$WIN_BUILD_PATH" "$TEMP_FILE"
            WIN_BUILD_PATH_BS="${WIN_BUILD_PATH//\//\\}"
            remove_string_from_file "$WIN_BUILD_PATH_BS" "$TEMP_FILE"
        fi
    fi
fi

# Verify file size didn't change (important for binary integrity)
if [ ! -f "$TEMP_FILE" ]; then
    echo "Error: Temporary file not found: $TEMP_FILE" >&2
    exit 1
fi

ORIG_SIZE=$(stat -c%s "$BINARY_PATH" 2>/dev/null || echo "0")
NEW_SIZE=$(stat -c%s "$TEMP_FILE" 2>/dev/null || echo "0")

if [ "$ORIG_SIZE" = "0" ] || [ "$NEW_SIZE" = "0" ]; then
    echo "Error: Failed to get file sizes (orig=$ORIG_SIZE, new=$NEW_SIZE)" >&2
    rm -f "$TEMP_FILE"
    exit 1
fi

if [ "$ORIG_SIZE" -ne "$NEW_SIZE" ]; then
    echo "Error: Binary size changed from $ORIG_SIZE to $NEW_SIZE bytes - would corrupt binary!" >&2
    rm -f "$TEMP_FILE"
    exit 1
fi

if [ "$TOTAL_REPLACEMENTS" -gt 0 ]; then
    # Replace original with modified version
    # On Windows, mv might fail if the file is in use, so try copying instead
    if cp "$TEMP_FILE" "$BINARY_PATH" 2>/dev/null; then
        rm -f "$TEMP_FILE"
        echo "Made $TOTAL_REPLACEMENTS replacements - successfully removed embedded paths from binary"
    elif mv "$TEMP_FILE" "$BINARY_PATH" 2>/dev/null; then
        echo "Made $TOTAL_REPLACEMENTS replacements - successfully removed embedded paths from binary"
    else
        echo "Error: Failed to update $BINARY_PATH (both cp and mv failed)" >&2
        echo "Temporary file left at: $TEMP_FILE" >&2
        exit 1
    fi
else
    rm -f "$TEMP_FILE"
    echo "No embedded paths found to remove"
fi

exit 0
