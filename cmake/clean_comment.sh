#!/bin/sh
# Clean .comment section to remove duplicate compiler version strings
# Keeps only the custom ascii-chat version string

set -e

BINARY="$1"
TMP=$(mktemp)

# Extract custom version string
readelf -p .comment "$BINARY" 2>/dev/null | grep 'ascii-chat:' | sed 's/^.*\]//' | head -1 | sed 's/^[[:space:]]*//' > "$TMP" || true

# Remove .comment section
objcopy --remove-section=.comment "$BINARY"

# Re-add clean .comment with only version string
if [ -s "$TMP" ]; then
    objcopy --add-section .comment="$TMP" --set-section-flags .comment=noload,readonly "$BINARY"
fi

rm -f "$TMP"
