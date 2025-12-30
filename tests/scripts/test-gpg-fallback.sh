#!/bin/bash
# Test script to verify GPG fallback mechanism works without agent

set -e

echo "=== Testing GPG Fallback Mechanism ==="

# Setup isolated test keyring
eval "$(./tests/scripts/setup-test-gpg-keyring.sh)"

if [ -z "$TEST_GPG_KEY_ID" ]; then
    echo "ERROR: Failed to setup test GPG keyring"
    exit 1
fi

echo "Test key ID: $TEST_GPG_KEY_ID"
echo "GNUPGHOME: $GNUPGHOME"

# Kill any running GPG agent to force fallback
echo "Stopping GPG agent to force fallback..."
gpgconf --kill gpg-agent 2>/dev/null || true
sleep 1

# Run a simple test that exercises the fallback
echo "Running GPG authentication tests..."
LOG_LEVEL=0 build/bin/test_integration_test_gpg_authentication 2>&1 | tee /tmp/gpg_fallback_test.log

# Check if fallback was used
if grep -q "fallback" /tmp/gpg_fallback_test.log; then
    echo "✓ Fallback mechanism was triggered"
else
    echo "⚠ Fallback mechanism may not have been used (check logs)"
fi

# Check if test passed
if grep -q "Passing: 1" /tmp/gpg_fallback_test.log; then
    echo "✓ Test PASSED"
    EXIT_CODE=0
else
    echo "✗ Test FAILED"
    EXIT_CODE=1
fi

# Cleanup
./tests/scripts/cleanup-test-gpg-keyring.sh

exit $EXIT_CODE
