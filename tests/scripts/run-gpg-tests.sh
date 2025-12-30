#!/bin/bash
# Run GPG authentication tests with isolated test keyring
# This script sets up a temporary GPG keyring, runs the tests, and cleans up

set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=== Setting up test GPG keyring ===" >&2
eval "$("$SCRIPT_DIR/setup-test-gpg-keyring.sh")"

# Verify environment variables are set
if [ -z "$TEST_GPG_KEY_ID" ]; then
    echo "ERROR: Failed to set up test GPG keyring" >&2
    exit 1
fi

echo "Test GPG key ID: $TEST_GPG_KEY_ID" >&2
echo "Test GNUPGHOME: $GNUPGHOME" >&2

# Trap to ensure cleanup happens even if tests fail
cleanup() {
    echo "" >&2
    echo "=== Cleaning up test GPG keyring ===" >&2
    "$SCRIPT_DIR/cleanup-test-gpg-keyring.sh"
}
trap cleanup EXIT

# Run the tests
echo "" >&2
echo "=== Running GPG authentication tests ===" >&2
cd "$PROJECT_ROOT"

# Run GPG-related tests directly (not via ctest) to pass environment variables
# CTest doesn't pass shell environment to child processes by default
echo "Running test_integration_test_gpg_authentication..." >&2
GNUPGHOME="$GNUPGHOME" TEST_GPG_KEY_ID="$TEST_GPG_KEY_ID" TEST_GPG_KEY_FPR="$TEST_GPG_KEY_FPR" \
    build/bin/test_integration_test_gpg_authentication --color=always

echo "" >&2
echo "Running test_integration_gpg_handshake..." >&2
GNUPGHOME="$GNUPGHOME" TEST_GPG_KEY_ID="$TEST_GPG_KEY_ID" TEST_GPG_KEY_FPR="$TEST_GPG_KEY_FPR" \
    build/bin/test_integration_gpg_handshake --color=always
