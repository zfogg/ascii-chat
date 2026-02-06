#!/bin/bash
# Export ACDS identity key to SSH and GPG formats for website distribution

set -e

if [ ! -f "acds_identity" ]; then
    echo "Error: acds_identity file not found"
    exit 1
fi

# Extract the last 32 bytes (public key) from the 64-byte ACDS identity
# Ed25519 keys in libsodium format: [32-byte seed][32-byte public key]
dd if=acds_identity bs=1 skip=32 count=32 of=acds_pubkey.raw 2>/dev/null

echo "Extracted public key (32 bytes):"
hexdump -C acds_pubkey.raw

# Convert raw Ed25519 public key to SSH format
# SSH Ed25519 format: "ssh-ed25519 " + base64(0x0000000b + "ssh-ed25519" + 0x00000020 + 32_byte_pubkey)
python3 << 'PYTHON'
import base64
import struct

# Read raw public key
with open('acds_pubkey.raw', 'rb') as f:
    pubkey = f.read()

if len(pubkey) != 32:
    print(f"Error: Expected 32 bytes, got {len(pubkey)}")
    exit(1)

# Build SSH wire format
# [uint32 len]["ssh-ed25519"][uint32 len][32-byte key]
key_type = b'ssh-ed25519'
ssh_blob = struct.pack('>I', len(key_type)) + key_type + struct.pack('>I', 32) + pubkey

# Encode as base64
ssh_b64 = base64.b64encode(ssh_blob).decode('ascii')

# Write SSH public key with correct comment
ssh_key = f"ssh-ed25519 {ssh_b64} ascii-chat Discovery Service\n"
with open('public/key.pub', 'w') as f:
    f.write(ssh_key)

print("SSH public key:")
print(ssh_key)
PYTHON

# For GPG, we'll use the GPG key that was already generated
if [ -f "acds_identity.pub.gpg" ]; then
    echo "GPG public key already exists at acds_identity.pub.gpg"
    echo "Copying to public/key.gpg..."
    cp acds_identity.pub.gpg public/key.gpg
fi

echo ""
echo "✓ Keys exported successfully!"
echo "  - SSH: public/key.pub"
echo "  - GPG: public/key.gpg"
echo ""
echo "SHA256 fingerprint (SSH):"
ssh-keygen -lf public/key.pub
