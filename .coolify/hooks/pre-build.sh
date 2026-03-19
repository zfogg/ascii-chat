#!/bin/bash
set -euo pipefail

# Initialize git submodules before Docker build
# Coolify's --recurse-submodules flag only initializes metadata,
# this actually checks out the submodule files into deps/
git submodule update --init --recursive

echo "✓ Git submodules initialized successfully"
