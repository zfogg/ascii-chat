#!/usr/bin/env bash
# version.sh - Print and calculate version numbers from git tags
#
# Usage:
#   ./scripts/version.sh              # Print current version (e.g., 0.3.57)
#   ./scripts/version.sh --next       # Print next patch version (e.g., 0.3.58)
#   ./scripts/version.sh --next --major  # Increment major (e.g., 1.0.0)
#   ./scripts/version.sh --next --minor  # Increment minor (e.g., 0.4.0)
#   ./scripts/version.sh --next --patch  # Increment patch (e.g., 0.3.58)

set -euo pipefail

NEXT=false
INCREMENT_MAJOR=false
INCREMENT_MINOR=false
INCREMENT_PATCH=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --next)
            NEXT=true
            shift
            ;;
        --major)
            INCREMENT_MAJOR=true
            shift
            ;;
        --minor)
            INCREMENT_MINOR=true
            shift
            ;;
        --patch)
            INCREMENT_PATCH=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--next] [--major] [--minor] [--patch]"
            echo ""
            echo "Options:"
            echo "  --next     Print the next version instead of current"
            echo "  --major    Increment major version (resets minor and patch to 0)"
            echo "  --minor    Increment minor version (resets patch to 0)"
            echo "  --patch    Increment patch version (default if --next without specifier)"
            echo ""
            echo "Examples:"
            echo "  $0                    # Current version: 0.3.57"
            echo "  $0 --next             # Next patch: 0.3.58"
            echo "  $0 --next --major     # Next major: 1.0.0"
            echo "  $0 --next --minor     # Next minor: 0.4.0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Get current version from git describe
# Strips the 'v' prefix if present
get_current_version() {
    local version
    version=$(git describe --tags --abbrev=0 2>/dev/null || echo "v0.0.0")
    echo "${version#v}"
}

# Parse version into components
parse_version() {
    local version="$1"
    IFS='.' read -r MAJOR MINOR PATCH <<< "$version"
    # Handle versions with extra components (e.g., 0.3.57-rc1)
    PATCH="${PATCH%%-*}"
}

# Main logic
CURRENT_VERSION=$(get_current_version)
parse_version "$CURRENT_VERSION"

if [[ "$NEXT" == "false" ]]; then
    # Just print current version
    echo "$CURRENT_VERSION"
    exit 0
fi

# --next was specified, calculate next version
# Default to patch increment if no specifier given
if [[ "$INCREMENT_MAJOR" == "false" && "$INCREMENT_MINOR" == "false" && "$INCREMENT_PATCH" == "false" ]]; then
    INCREMENT_PATCH=true
fi

# Apply increments (in order of precedence: major > minor > patch)
if [[ "$INCREMENT_MAJOR" == "true" ]]; then
    MAJOR=$((MAJOR + 1))
    MINOR=0
    PATCH=0
fi

if [[ "$INCREMENT_MINOR" == "true" ]]; then
    MINOR=$((MINOR + 1))
    PATCH=0
fi

if [[ "$INCREMENT_PATCH" == "true" ]]; then
    PATCH=$((PATCH + 1))
fi

echo "${MAJOR}.${MINOR}.${PATCH}"
