#!/bin/bash
# Create release binaries and package them
# Usage: ./release.sh [version]

set -e

VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo 'dev')}"
RELEASE_DIR="release-$VERSION"

echo "========================================="
echo "Creating ASCII-Chat Release: $VERSION"
echo "========================================="

# Clean and build production binaries
./build-production.sh

# Create release directory
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"

# Copy binaries
cp build-production/bin/ascii-chat-server "$RELEASE_DIR/"
cp build-production/bin/ascii-chat-client "$RELEASE_DIR/"

# Strip binaries (remove debug symbols for smaller size)
strip "$RELEASE_DIR/ascii-chat-server"
strip "$RELEASE_DIR/ascii-chat-client"

# Copy documentation
cp README.md "$RELEASE_DIR/" 2>/dev/null || true
cp LICENSE "$RELEASE_DIR/" 2>/dev/null || true

# Create release notes
cat > "$RELEASE_DIR/RELEASE_NOTES.md" <<EOF
# ASCII-Chat $VERSION

## Build Information
- Built: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
- Architecture: $(uname -m)
- OS: $(uname -s)
- Compiler: musl-gcc $(musl-gcc --version | head -1)
- Memory Allocator: mimalloc
- Linking: Static (no dependencies)

## Files
- \`ascii-chat-server\` - Server binary
- \`ascii-chat-client\` - Client binary

## Verification
\`\`\`bash
# Verify static linking:
ldd ascii-chat-server  # Should say "not a dynamic executable"

# Check binary info:
file ascii-chat-server

# Run server:
./ascii-chat-server --help

# Run client:
./ascii-chat-client --help
\`\`\`

## System Requirements
- Linux kernel 2.6.32 or later
- No library dependencies (statically linked)
- Portable across different Linux distributions
EOF

# Create tarball
tar czf "${RELEASE_DIR}.tar.gz" "$RELEASE_DIR"

echo ""
echo "========================================="
echo "Release created successfully!"
echo "========================================="
echo "Directory: $RELEASE_DIR"
echo "Archive:   ${RELEASE_DIR}.tar.gz"
echo ""
ls -lh "$RELEASE_DIR"/*
echo ""
echo "Archive size:"
ls -lh "${RELEASE_DIR}.tar.gz"
echo ""
echo "To test:"
echo "  cd $RELEASE_DIR"
echo "  ./ascii-chat-server --help"
