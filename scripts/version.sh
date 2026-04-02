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
#   ./scripts/version.sh --next-minor --tag   # Create git tag v0.4.0
#   ./scripts/version.sh --next-minor --push  # Create and push git tag v0.4.0

set -euo pipefail

SHOW_MAJOR=false
SHOW_MINOR=false
SHOW_PATCH=false
NEXT_MAJOR=false
NEXT_MINOR=false
NEXT_PATCH=false
DO_TAG=false
DO_PUSH=false

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
        --tag)
            DO_TAG=true
            shift
            ;;
        --push)
            DO_PUSH=true
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
            echo "Actions:"
            echo "  --tag         Create a git tag for the computed version"
            echo "  --push        Push the tag to origin (implies --tag)"
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
            echo "  $0 --next-minor --tag       # 0.4.0 (creates v0.4.0 tag)"
            echo "  $0 --next-minor --push      # 0.4.0 (creates and pushes v0.4.0 tag)"
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
    version=$(git describe --tags --abbrev=0 --match 'v[0-9]*.[0-9]*.[0-9]*' 2>/dev/null || echo "v0.0.0")
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

# Check if any --next* flags are set
IS_NEXT=$([[ "$NEXT_MAJOR" == "true" ]] || [[ "$NEXT_MINOR" == "true" ]] || [[ "$NEXT_PATCH" == "true" ]] && echo "true" || echo "false")

# Determine which component to bump
BUMP_MAJOR=false
BUMP_MINOR=false
BUMP_PATCH=false

if [[ "$NEXT_MAJOR" == "true" ]]; then
    BUMP_MAJOR=true
fi

if [[ "$NEXT_MINOR" == "true" ]]; then
    BUMP_MINOR=true
fi

if [[ "$NEXT_PATCH" == "true" ]]; then
    BUMP_PATCH=true
fi

# Component flags can override bump target when combined with --next
if [[ "$SHOW_MAJOR" == "true" ]] && [[ "$IS_NEXT" == "true" ]]; then
    BUMP_MAJOR=true
    BUMP_MINOR=false
    BUMP_PATCH=false
fi

if [[ "$SHOW_MINOR" == "true" ]] && [[ "$IS_NEXT" == "true" ]]; then
    BUMP_MAJOR=false
    BUMP_MINOR=true
    BUMP_PATCH=false
fi

if [[ "$SHOW_PATCH" == "true" ]] && [[ "$IS_NEXT" == "true" ]]; then
    BUMP_MAJOR=false
    BUMP_MINOR=false
    BUMP_PATCH=true
fi

# --push implies --tag
if [[ "$DO_PUSH" == "true" ]]; then
    DO_TAG=true
fi

# Tag/push a version and print it
output_version() {
    local version="$1"
    echo "$version"
    if [[ "$DO_TAG" == "true" ]]; then
        git tag "v${version}"
        echo "Tagged v${version}" >&2
        if [[ "$DO_PUSH" == "true" ]]; then
            git push origin "v${version}"
            echo "Pushed v${version}" >&2
        fi
    fi
}

# Handle next version calculations (always show full version)
if [[ "$BUMP_MAJOR" == "true" ]]; then
    MAJOR=$((MAJOR + 1))
    MINOR=0
    PATCH=0
    output_version "${MAJOR}.${MINOR}.${PATCH}"
    exit 0
fi

if [[ "$BUMP_MINOR" == "true" ]]; then
    MINOR=$((MINOR + 1))
    PATCH=0
    output_version "${MAJOR}.${MINOR}.${PATCH}"
    exit 0
fi

if [[ "$BUMP_PATCH" == "true" ]]; then
    PATCH=$((PATCH + 1))
    output_version "${MAJOR}.${MINOR}.${PATCH}"
    exit 0
fi

# Handle component queries for current version (show only that component)
if [[ "$SHOW_MAJOR" == "true" ]]; then
    echo "${MAJOR}"
    exit 0
fi

if [[ "$SHOW_MINOR" == "true" ]]; then
    echo "${MINOR}"
    exit 0
fi

if [[ "$SHOW_PATCH" == "true" ]]; then
    echo "${PATCH}"
    exit 0
fi

# Default: print current version with all three parts
echo "${MAJOR}.${MINOR}.${PATCH}"
