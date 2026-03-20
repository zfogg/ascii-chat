#!/bin/bash
# Build and push docker images for ascii-chat
# Usage: ./docker/build-and-push.sh [--main-only] [--deps-only]
#
# By default, builds and pushes deps image, then builds main image.
# Use --main-only to skip deps image (assumes it already exists)
# Use --deps-only to only build deps image (don't build main)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DEPS_IMAGE="zfogg/ascii-chat-deps:latest"
MAIN_IMAGE="zfogg/ascii-chat:latest"
DEPS_ONLY=false
MAIN_ONLY=false

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --deps-only) DEPS_ONLY=true ;;
        --main-only) MAIN_ONLY=true ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

cd "$REPO_ROOT"

# Build and push deps image (unless --main-only)
if [ "$MAIN_ONLY" = false ]; then
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "📦 Building deps image: $DEPS_IMAGE"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    DOCKER_BUILDKIT=1 docker build \
        -f docker/deps.dockerfile \
        -t "$DEPS_IMAGE" \
        . || {
        echo "❌ Failed to build deps image"
        exit 1
    }

    echo ""
    echo "🚀 Pushing deps image to Docker Hub..."
    docker push "$DEPS_IMAGE" || {
        echo "❌ Failed to push deps image"
        exit 1
    }

    echo "✅ Deps image built and pushed successfully"
fi

# Build main image (unless --deps-only)
if [ "$DEPS_ONLY" = false ]; then
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "🏗️  Building main image: $MAIN_IMAGE"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    DOCKER_BUILDKIT=1 docker build \
        -f docker/Dockerfile \
        -t "$MAIN_IMAGE" \
        . || {
        echo "❌ Failed to build main image"
        exit 1
    }

    echo "✅ Main image built successfully"

    # Optionally push main image
    read -p "Push main image to Docker Hub? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "🚀 Pushing main image to Docker Hub..."
        docker push "$MAIN_IMAGE" || {
            echo "❌ Failed to push main image"
            exit 1
        }
        echo "✅ Main image pushed successfully"
    fi
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🎉 Build complete!"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
