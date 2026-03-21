#!/bin/bash
set -e

# Generate SSH key if not provided via build secret
if [ ! -f /acds/key ]; then
    echo "Generating SSH private key..."
    ssh-keygen -t ed25519 -f /acds/key -N "" -C "ascii-chat-discovery-service"
    chmod 600 /acds/key
fi

# Generate GPG key if not provided via build secret (optional - gpg-agent may not be available)
if [ ! -f /acds/key.gpg ]; then
    if command -v gpg &>/dev/null; then
        echo "Generating GPG key..."
        gpg --batch --generate-key <<EOF 2>/dev/null || true
%no-protection
Key-Type: eddsa
Key-Curve: ed25519
Name-Real: ascii-chat-discovery-service
Name-Email: discovery@ascii-chat.local
Expire-Date: 0
EOF
        # Export the private key to a file (may fail if gpg-agent unavailable)
        gpg --batch --export-secret-keys > /acds/key.gpg 2>/dev/null || true
        chmod 600 /acds/key.gpg 2>/dev/null || true
    fi
fi

# Run the ascii-chat binary with provided arguments
exec /usr/local/bin/ascii-chat "$@"
