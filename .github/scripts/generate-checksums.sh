#!/bin/bash
# Generate checksums for release artifacts
# Usage: generate-checksums.sh <tarball> <platform> <version>

set -e

TARBALL="$1"
PLATFORM="$2"
VERSION="$3"

if [[ ! -f "$TARBALL" ]]; then
    echo "Error: Tarball '$TARBALL' not found"
    exit 1
fi

echo "Generating checksums for $TARBALL ($PLATFORM)..."

# Detect OS and use appropriate hash commands
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    shasum -a 256 "$TARBALL" > "${TARBALL}.sha256"
    shasum -a 512 "$TARBALL" > "${TARBALL}.sha512"
    md5 "$TARBALL" | sed 's/MD5 (//' | sed 's/) = / /' > "${TARBALL}.md5"
else
    # Linux
    sha256sum "$TARBALL" > "${TARBALL}.sha256"
    sha512sum "$TARBALL" > "${TARBALL}.sha512"
    md5sum "$TARBALL" > "${TARBALL}.md5"
fi

# Create combined checksum file
cat > "checksums-${PLATFORM}.txt" <<EOF
# ascii-chat ${VERSION} - ${PLATFORM} build
# Checksums generated on $(date -u)

## SHA-256
$(cat "${TARBALL}.sha256")

## SHA-512
$(cat "${TARBALL}.sha512")

## MD5
$(cat "${TARBALL}.md5")
EOF

echo "Checksums generated:"
cat "checksums-${PLATFORM}.txt"
