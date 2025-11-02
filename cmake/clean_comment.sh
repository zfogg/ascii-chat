#!/bin/sh
# Clean .comment section to remove duplicate compiler version strings
# Keeps only the custom ascii-chat version string with description

set -e

BINARY="$1"
TMP=$(mktemp)
TMP_SECTION=$(mktemp)

# Extract the .comment section to a temporary file
objcopy --dump-section .comment="$TMP_SECTION" "$BINARY" 2>/dev/null || { rm -f "$TMP" "$TMP_SECTION"; exit 0; }

# Find the offset where "ascii-chat v" starts in the section
# Use grep -abo to find byte offset
OFFSET=$(grep -abo 'ascii-chat v' "$TMP_SECTION" | head -1 | cut -d: -f1)

if [ -n "$OFFSET" ]; then
    # Extract from the offset until the first null byte (includes embedded newline and UTF-8 emojis)
    # Use tr to stop at the first null byte
    dd if="$TMP_SECTION" bs=1 skip="$OFFSET" 2>/dev/null | tr -d '\0' | head -c 200 > "$TMP"

    # Find where the null byte was (string length) and truncate there
    # Use perl to extract up to first null byte
    dd if="$TMP_SECTION" bs=1 skip="$OFFSET" 2>/dev/null | perl -pe 's/\x00.*/\x00/s' > "$TMP"

    # Remove .comment section
    objcopy --remove-section .comment "$BINARY"

    # Re-add clean .comment with only the ascii-chat version string
    if [ -s "$TMP" ]; then
        objcopy --add-section .comment="$TMP" --set-section-flags .comment=noload,readonly "$BINARY"
    fi
fi

rm -f "$TMP" "$TMP_SECTION"
