#!/usr/bin/env bash
# Create a release tarball with all dependencies bundled
#
# Usage:
#   ./scripts/create-release-tarball.sh [version]
#
# If version is not specified, uses the latest git tag.
# The output tarball includes all submodule sources, making it
# suitable for distribution without requiring git clone --recursive.
#
# Output: ascii-chat-{version}-full.tar.gz

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Get version from argument or latest tag
if [[ -n "$1" ]]; then
  VERSION="$1"
else
  VERSION="$(git describe --tags --abbrev=0 2>/dev/null || echo "dev")"
fi

# Remove leading 'v' if present
VERSION="${VERSION#v}"

echo "Creating release tarball for version $VERSION..."

# Ensure submodules are initialized
echo "Initializing submodules..."
git submodule update --init --recursive

# Create a temporary directory for the release
TMPDIR="$(mktemp -d)"
RELEASE_DIR="$TMPDIR/ascii-chat-$VERSION"
trap "rm -rf '$TMPDIR'" EXIT

echo "Copying source files..."
mkdir -p "$RELEASE_DIR"

# Export main repo (excludes submodules)
git archive HEAD | tar -x -C "$RELEASE_DIR"

# Copy submodule contents (not as git repos, just the files)
echo "Bundling submodules..."
for submodule in deps/bearssl deps/uthash deps/tomlc17 deps/libsodium-bcrypt-pbkdf deps/sokol; do
  if [[ -d "$submodule" ]]; then
    echo "  - $submodule"
    mkdir -p "$RELEASE_DIR/$submodule"
    # Copy all files from submodule, excluding .git
    (cd "$submodule" && find . -type f ! -path './.git/*' -exec cp --parents {} "$RELEASE_DIR/$submodule/" \;)
  fi
done

# Optional: include doxygen-awesome-css for docs
if [[ -d "deps/doxygen-awesome-css" ]]; then
  echo "  - deps/doxygen-awesome-css"
  mkdir -p "$RELEASE_DIR/deps/doxygen-awesome-css"
  (cd "deps/doxygen-awesome-css" && find . -type f ! -path './.git/*' -exec cp --parents {} "$RELEASE_DIR/deps/doxygen-awesome-css/" \;)
fi

# Create the tarball
OUTPUT="$REPO_ROOT/ascii-chat-$VERSION-full.tar.gz"
echo "Creating tarball: $OUTPUT"
tar -czf "$OUTPUT" -C "$TMPDIR" "ascii-chat-$VERSION"

echo ""
echo "Release tarball created: $OUTPUT"
echo "Size: $(du -h "$OUTPUT" | cut -f1)"
echo "SHA256: $(sha256sum "$OUTPUT" | cut -d' ' -f1)"
echo ""
echo "To verify:"
echo "  tar -tzf $OUTPUT | head -20"
echo ""
echo "To upload as GitHub release asset:"
echo "  gh release upload v$VERSION $OUTPUT"
