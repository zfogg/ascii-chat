#!/bin/bash
# Setup isolated GPG keyring for tests
# Creates a test key without modifying user's keyring

set -e

# Create temporary GPG home directory
TEST_GNUPGHOME="${TEST_GNUPGHOME:-/tmp/ascii-chat-test-gpg-$$}"
export GNUPGHOME="$TEST_GNUPGHOME"

# Clean up any existing test keyring
rm -rf "$GNUPGHOME"
mkdir -p "$GNUPGHOME"
chmod 700 "$GNUPGHOME"

# Generate test key (unattended)
cat > "$GNUPGHOME/gen-key-script" << 'EOF'
Key-Type: EDDSA
Key-Curve: Ed25519
Key-Usage: sign
Subkey-Type: ECDH
Subkey-Curve: Curve25519
Subkey-Usage: encrypt
Name-Real: ASCII Chat Test
Name-Email: test@ascii-chat.local
Expire-Date: 0
%no-protection
%commit
EOF

# Generate the key
gpg --batch --generate-key "$GNUPGHOME/gen-key-script" >/dev/null 2>&1

# Get the generated key ID (last 16 hex chars of fingerprint)
TEST_KEY_FPR=$(gpg --list-secret-keys --with-colons 2>/dev/null | grep "^fpr" | head -1 | cut -d: -f10)
TEST_KEY_ID="${TEST_KEY_FPR: -16}"

# Verify the key exists
if ! gpg --list-secret-keys "$TEST_KEY_ID" >/dev/null 2>&1; then
    echo "ERROR: Failed to create test GPG key" >&2
    exit 1
fi

# Export environment variables for tests to use
echo "export GNUPGHOME='$GNUPGHOME'"
echo "export TEST_GPG_KEY_ID='$TEST_KEY_ID'"
echo "export TEST_GPG_KEY_FPR='$TEST_KEY_FPR'"
