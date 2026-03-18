#!/bin/bash
# Build docker images with SSH and GPG keys for discovery service

set -e

# Load environment variables from .env
if [ ! -f .env ]; then
  echo "Error: .env file not found in web/"
  exit 1
fi

source .env

if [ -z "$SSH_PUBLIC_KEY" ]; then
  echo "Error: SSH_PUBLIC_KEY not set in .env"
  exit 1
fi

if [ -z "$GPG_PUBLIC_KEY" ]; then
  echo "Error: GPG_PUBLIC_KEY not set in .env"
  exit 1
fi

echo "Building Docker images with keys from .env..."
docker compose build \
  --build-arg SSH_PUBLIC_KEY="$SSH_PUBLIC_KEY" \
  --build-arg GPG_PUBLIC_KEY="$GPG_PUBLIC_KEY" \
  "$@"

echo "✓ Docker images built successfully"
