#!/usr/bin/env bash
# version.sh - Print and calculate version numbers from git tags
#
# Usage:
#   ./scripts/version.sh              # Print current version (e.g., 0.3.57)
#   ./scripts/version.sh --major      # Print major component (e.g., 0)
#   ./scripts/version.sh --minor      # Print minor component (e.g., 3)
#   ./scripts/version.sh --patch      # Print patch component (e.g., 57)
#   ./scripts/version.sh --next       # Print next patch version (e.g., 0.3.58)
#   ./scripts/version.sh --next-major # Increment major (e.g., 1.0.0)
#   ./scripts/version.sh --next-minor # Increment minor (e.g., 0.4.0)
#   ./scripts/version.sh --next-patch # Increment patch (e.g., 0.3.58)

set -euo pipefail

SHOW_MAJOR=false
SHOW_MINOR=false
SHOW_PATCH=false
NEXT_MAJOR=false
NEXT_MINOR=false
NEXT_PATCH=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --major)
            SHOW_MAJOR=true
            shift
            ;;
        --minor)
            SHOW_MINOR=true
            shift
            ;;
        --patch)
            SHOW_PATCH=true
            shift
            ;;
        --next)
            NEXT_PATCH=true
            shift
            ;;
        --next-major)
            NEXT_MAJOR=true
            shift
            ;;
        --next-minor)
            NEXT_MINOR=true
            shift
            ;;
        --next-patch)
            NEXT_PATCH=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Print version:"
            echo "  (no args)     Print full version (e.g., 0.3.57)"
            echo "  --major       Print major component (e.g., 0)"
            echo "  --minor       Print minor component (e.g., 3)"
            echo "  --patch       Print patch component (e.g., 57)"
            echo ""
            echo "Calculate next version:"
            echo "  --next        Print next patch version (shortcut for --next-patch)"
            echo "  --next-major  Increment major, reset minor and patch to 0"
            echo "  --next-minor  Increment minor, reset patch to 0"
            echo "  --next-patch  Increment patch"
            echo ""
            echo "Examples:"
            echo "  $0                    # 0.3.57"
            echo "  $0 --major            # 0"
            echo "  $0 --minor            # 3"
            echo "  $0 --patch            # 57"
            echo "  $0 --next             # 0.3.58"
            echo "  $0 --next-major       # 1.0.0"
            echo "  $0 --next-minor       # 0.4.0"
            echo "  $0 --next-patch       # 0.3.58"
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
    version=$(git describe --tags --abbrev=0 --match v[0-9]*.[0-9].[0-9]* 2>/dev/null || echo "v0.0.0")
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

# Handle component queries
if [[ "$SHOW_MAJOR" == "true" ]]; then
    echo "$MAJOR"
    exit 0
fi

if [[ "$SHOW_MINOR" == "true" ]]; then
    echo "$MINOR"
    exit 0
fi

if [[ "$SHOW_PATCH" == "true" ]]; then
    echo "$PATCH"
    exit 0
fi

# Handle next version calculations
if [[ "$NEXT_MAJOR" == "true" ]]; then
    MAJOR=$((MAJOR + 1))
    MINOR=0
    PATCH=0
    echo "${MAJOR}.${MINOR}.${PATCH}"
    exit 0
fi

if [[ "$NEXT_MINOR" == "true" ]]; then
    MINOR=$((MINOR + 1))
    PATCH=0
    echo "${MAJOR}.${MINOR}.${PATCH}"
    exit 0
fi

if [[ "$NEXT_PATCH" == "true" ]]; then
    PATCH=$((PATCH + 1))
    echo "${MAJOR}.${MINOR}.${PATCH}"
    exit 0
fi

# Default: print current version
echo "$CURRENT_VERSION"
