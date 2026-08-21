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
  VERSION="$("${REPO_ROOT}/scripts/version.sh")"
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

# Preserve version information that is normally discovered from Git metadata.
SOURCE_COMMIT="$(git rev-parse HEAD)"
SOURCE_DATE="$(git log -1 --format=%cs HEAD)"
LIB_VERSION="$(git tag -l 'lib/v[0-9]*.[0-9]*.[0-9]*' --sort=-v:refname | head -1)"
LIB_VERSION="${LIB_VERSION#lib/v}"
if [[ ! "$LIB_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Unable to determine the library version from lib/v* tags" >&2
  exit 1
fi
cat >"$RELEASE_DIR/cmake/install/SourceArchiveVersion.cmake" <<EOF
set(PROJECT_VERSION_FROM_GIT "$VERSION")
set(PROJECT_VERSION_DATE "$SOURCE_DATE")
set(ASCIICHAT_LIB_VERSION "$LIB_VERSION")
set(ASCIICHAT_SOURCE_COMMIT "$SOURCE_COMMIT")
EOF

# Copy every submodule as tracked source files, without Git metadata.
echo "Bundling submodules..."
while read -r _ submodule; do
  if [[ -d "$submodule" ]]; then
    echo "  - $submodule"
    mkdir -p "$RELEASE_DIR/$submodule"
    git -C "$submodule" archive HEAD | tar -x -C "$RELEASE_DIR/$submodule"
  else
    echo "Missing initialized submodule: $submodule" >&2
    exit 1
  fi
done < <(git config --file .gitmodules --get-regexp '^submodule\..*\.path$')

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
