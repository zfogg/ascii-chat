#!/bin/bash
# Create release binaries and package them
# Usage: ./release.sh [version]
#
# Official releases use musl + static linking for maximum portability.
# PGO is available via 'make pgo' but uses glibc (not suitable for releases).

set -e

VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo 'dev')}"

# Detect OS and architecture
OS="linux"
ARCH="$(uname -m)"

RELEASE_DIR="release-$VERSION"
RELEASE_NAME="ascii-chat-${OS}-${ARCH}"

echo "========================================="
echo "Creating ASCII-Chat Release: $VERSION"
echo "Platform: ${OS}-${ARCH}"
echo "Build Type: Production (musl + static)"
echo "========================================="

# Build production binaries (musl + static)
echo ""
echo "Building production release (musl + static)..."
make production
BUILD_DIR="build-production"

# Create release directory
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"

# Copy binaries
cp "$BUILD_DIR/bin/ascii-chat" "$RELEASE_DIR/"

# Strip binaries (remove debug symbols for smaller size)
strip "$RELEASE_DIR/ascii-chat"

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
- Compiler: musl-gcc $(musl-gcc --version | head -1 2>/dev/null || echo '(version unknown)')
- C Library: musl (static)
- Optimization: Release (-O3 + LTO + aggressive opts)
- Memory Allocator: mimalloc
- Linking: Static (no dependencies)

## Files
- \`ascii-chat\` - Server binary
- \`ascii-chat\` - Client binary

## Verification
\`\`\`bash
# Verify static linking:
ldd ascii-chat  # Should say "not a dynamic executable"

# Check binary info:
file ascii-chat

# Run server:
./ascii-chat --help

# Run client:
./ascii-chat --help
\`\`\`

## System Requirements
- Linux kernel 2.6.32 or later
- No library dependencies (statically linked)
- Portable across different Linux distributions
EOF

# Create tarball with consistent naming
tar czf "${RELEASE_NAME}.tar.gz" "$RELEASE_DIR"

echo ""
echo "========================================="
echo "Release created successfully!"
echo "========================================="
echo "Directory: $RELEASE_DIR"
echo "Archive:   ${RELEASE_NAME}.tar.gz"
echo ""
ls -lh "$RELEASE_DIR"/*
echo ""
echo "Archive size:"
ls -lh "${RELEASE_NAME}.tar.gz"
echo ""
echo "To test:"
echo "  cd $RELEASE_DIR"
echo "  ./ascii-chat --help"
