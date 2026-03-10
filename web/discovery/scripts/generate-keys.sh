#!/bin/bash

set -e

cd "$(dirname "$0")/.."

# Try to load from .env if available (for local development)
if [ -f .env ]; then
  set -a
  source .env
  set +a
fi

# Ensure public directory exists
mkdir -p public

# Generate key.pub from SSH_PUBLIC_KEY env var
if [ -z "$SSH_PUBLIC_KEY" ]; then
  echo "Error: SSH_PUBLIC_KEY env var not set"
  exit 1
fi
echo "$SSH_PUBLIC_KEY" > public/key.pub
echo "Generated public/key.pub from SSH_PUBLIC_KEY"

# Generate key.gpg from GPG_PUBLIC_KEY env var
# Handle escaped newlines: convert \n to actual newlines
if [ -z "$GPG_PUBLIC_KEY" ]; then
  echo "Error: GPG_PUBLIC_KEY env var not set"
  exit 1
fi
# Use printf to convert escaped newlines to actual newlines
printf "%b" "$GPG_PUBLIC_KEY" > public/key.gpg
echo "Generated public/key.gpg from GPG_PUBLIC_KEY"
