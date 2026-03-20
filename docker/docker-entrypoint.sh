#!/bin/bash
set -e

# Generate SSH key if not provided via build secret
if [ ! -f /acds/key ]; then
    echo "Generating SSH private key..."
    ssh-keygen -t ed25519 -f /acds/key -N "" -C "ascii-chat-discovery-service"
    chmod 600 /acds/key
fi

# Generate GPG key if not provided via build secret
if [ ! -f /acds/key.gpg ]; then
    echo "Generating GPG key..."
    gpg --batch --generate-key <<EOF
%no-protection
Key-Type: eddsa
Key-Curve: ed25519
Name-Real: ascii-chat-discovery-service
Name-Email: discovery@ascii-chat.local
Expire-Date: 0
EOF
    # Export the private key to a file
    gpg --batch --export-secret-keys > /acds/key.gpg
    chmod 600 /acds/key.gpg
fi

# Run the ascii-chat binary with provided arguments
exec /usr/local/bin/ascii-chat "$@"
