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

# Convert WSL paths (/mnt/c/...) to Git Bash paths (/c/...) before resolving
# This allows the script to work with both WSL and Git Bash paths
CONVERTED_SOURCE_DIR="$SOURCE_DIR"
if [[ "$SOURCE_DIR" =~ ^/mnt/([a-zA-Z])/(.*) ]]; then
    DRIVE_LETTER="${BASH_REMATCH[1]}"
    REST_OF_PATH="${BASH_REMATCH[2]}"
    CONVERTED_SOURCE_DIR="/${DRIVE_LETTER,,}/$REST_OF_PATH"
    echo "Converted WSL source path: $SOURCE_DIR -> $CONVERTED_SOURCE_DIR"
fi

# Resolve to absolute paths
SOURCE_PATH=$(cd "$CONVERTED_SOURCE_DIR" 2>/dev/null && pwd || echo "$CONVERTED_SOURCE_DIR")
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
            # Replace path+separator (forward or backslash) with null bytes to maintain file size
            # Calculate length and generate null padding in perl
            local path_len=$((${#old_path} + 1))  # +1 for the trailing separator

            # Use Python for more reliable binary replacement
            # Pass path as command-line argument to avoid bash interpolation issues
            python3 -c 'import sys; old_path_str = sys.argv[1]; old_path = old_path_str.encode("latin1"); path_len = int(sys.argv[2]); file_path = sys.argv[3]; null_bytes = b"\x00" * path_len; data = open(file_path, "rb").read(); [data := data.replace(old_path + sep, null_bytes) for sep in [b"/", b"\\"] if old_path + sep in data]; open(file_path, "wb").write(data)' "$old_path" "$path_len" "$file" 2>/dev/null || true

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

# On Windows, also handle Windows-style paths (C:/Users/... format)
# Convert Git Bash path (/c/Users/...) to Windows path (C:/Users/...)
if [[ "$SOURCE_PATH" =~ ^/[a-zA-Z]/(.*) ]]; then
    # Extract drive letter and path
    DRIVE_LETTER="${SOURCE_PATH:1:1}"
    WIN_PATH="${DRIVE_LETTER^^}:${SOURCE_PATH:2}"
    replace_path "$WIN_PATH" "$TEMP_FILE"
    # Also try with backslashes
    WIN_PATH_BS="${WIN_PATH//\//\\}"
    replace_path "$WIN_PATH_BS" "$TEMP_FILE"
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
        replace_path "$HOME_PATH" "$TEMP_FILE"
    done
elif [ -n "$HOME_DIR" ] && [ "$HOME_DIR" != "$SOURCE_PATH" ]; then
    replace_path "$HOME_DIR" "$TEMP_FILE"
    # On Unix/Linux, also handle Windows-style home path if applicable
    if [[ "$HOME_DIR" =~ ^/[a-zA-Z]/(.*) ]]; then
        DRIVE_LETTER="${HOME_DIR:1:1}"
        WIN_HOME_PATH="${DRIVE_LETTER^^}:${HOME_DIR:2}"
        replace_path "$WIN_HOME_PATH" "$TEMP_FILE"
        WIN_HOME_PATH_BS="${WIN_HOME_PATH//\//\\}"
        replace_path "$WIN_HOME_PATH_BS" "$TEMP_FILE"
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
        replace_path "$BUILD_PATH" "$TEMP_FILE"
        # On Windows, also handle Windows-style build path
        if [[ "$BUILD_PATH" =~ ^/[a-zA-Z]/(.*) ]]; then
            DRIVE_LETTER="${BUILD_PATH:1:1}"
            WIN_BUILD_PATH="${DRIVE_LETTER^^}:${BUILD_PATH:2}"
            replace_path "$WIN_BUILD_PATH" "$TEMP_FILE"
            WIN_BUILD_PATH_BS="${WIN_BUILD_PATH//\//\\}"
            replace_path "$WIN_BUILD_PATH_BS" "$TEMP_FILE"
        fi
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
