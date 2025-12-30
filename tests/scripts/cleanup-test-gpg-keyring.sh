#!/bin/bash
# Clean up test GPG keyring

if [ -n "$GNUPGHOME" ] && [[ "$GNUPGHOME" == /tmp/ascii-chat-test-gpg-* ]]; then
    rm -rf "$GNUPGHOME"
    unset GNUPGHOME
    unset TEST_GPG_KEY_ID
    unset TEST_GPG_KEY_FPR
fi
