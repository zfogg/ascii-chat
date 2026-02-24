# Crypto Module - lib/crypto/

End-to-end encrypted communication for ascii-chat using modern cryptography (X25519, XSalsa20-Poly1305, Ed25519).

## Quick Overview

```
Core Crypto (libsodium)
  ├── X25519 key exchange (Diffie-Hellman)
  ├── XSalsa20-Poly1305 encryption/decryption
  ├── HMAC-SHA256 authentication
  └── Argon2id password-based KDF

Key Management
  ├── SSH keys + SSH agent support
  ├── GPG keys + GPG agent support
  ├── GitHub/GitLab key fetching
  └── Known hosts verification (TOFU)

Handshake Protocol
  ├── Client-side state machine (handshake/client.c)
  ├── Server-side state machine (handshake/server.c)
  └── Shared utilities (handshake/common.c)
```

## File Organization

### Core Cryptography
- **crypto.c/h** - Main interface (encryption, key exchange, HMAC, rekeying)
- **pem_utils.c/h** - PEM/OpenSSH format parsing
- **sha1.c/h** - SHA-1 for fingerprinting
- **regex.c/h** - Format detection

### SSH Integration
- **ssh/ssh_agent.c/h** - SSH agent communication (signing, key listing)
- **ssh/ssh_keys.c/h** - OpenSSH format parsing
- **known_hosts.c/h** - SSH-style known_hosts file
- **keys_validation.c/h** - SSH key validation

### GPG Integration
- **gpg/agent.c/h** - GPG agent communication
- **gpg/export.c/h** - Export keys from GPG keyring
- **gpg/signing.c/h** - GPG signing operations
- **gpg/verification.c/h** - Signature verification
- **gpg/gpg_keys.c/h** - GPG key format parsing
- **gpg/openpgp.c/h** - OpenPGP format handling

### HTTPS & Discovery
- **https_keys.c/h** - Fetch keys from HTTPS URLs (BearSSL)
- **discovery_keys.c/h** - ACDS discovery server key management

### Handshake Protocol
- **handshake/client.c/h** - Client-side handshake
- **handshake/server.c/h** - Server-side handshake
- **handshake/common.c/h** - Shared utilities

### Key Management
- **keys.c/h** - Master key loading interface
- **key_types.h** - Key structure definitions

## Usage Examples

### Basic Key Exchange
```c
#include "ascii-chat/crypto/crypto.h"

crypto_context_t ctx;
crypto_init(&ctx);

// Step 1: Send our ephemeral public key
uint8_t our_pubkey[32];
crypto_get_public_key(&ctx, our_pubkey);

// Step 2: Receive peer's ephemeral public key and compute shared secret
uint8_t peer_pubkey[32];  // Received from peer
crypto_set_peer_public_key(&ctx, peer_pubkey);

// Step 3: Check if ready for encryption
if (crypto_is_ready(&ctx)) {
    printf("Key exchange complete, ready for encryption\n");
}

// Encrypt data
uint8_t plaintext[] = "Hello, World!";
uint8_t ciphertext[1024];
size_t ciphertext_len;
crypto_encrypt(&ctx, plaintext, strlen((char*)plaintext),
               ciphertext, sizeof(ciphertext), &ciphertext_len);

// Decrypt data
uint8_t decrypted[1024];
size_t decrypted_len;
crypto_decrypt(&ctx, ciphertext, ciphertext_len,
               decrypted, sizeof(decrypted), &decrypted_len);

crypto_destroy(&ctx);
```

### SSH Key Authentication
```c
#include "ascii-chat/crypto/ssh/ssh_agent.h"
#include "ascii-chat/crypto/ssh/ssh_keys.h"

// Load SSH key
uint8_t key_data[4096];
size_t key_len = read_file("~/.ssh/id_ed25519", key_data, sizeof(key_data));

ssh_keys_t key;
ssh_keys_parse(key_data, key_len, &key);

// Check if SSH agent is available
if (ssh_agent_is_available()) {
    // Use agent for signing (no passphrase prompt)
    uint8_t signature[64];
    ssh_agent_sign(&key, data_to_sign, sizeof(data_to_sign), signature);
}
```

### GPG Key Authentication
```c
#include "ascii-chat/crypto/gpg/gpg.h"

// Export public key from GPG keyring
uint8_t pubkey[32];
gpg_export_public_key("zfogg", pubkey);

// Sign with GPG agent (no passphrase prompt if agent is running)
uint8_t signature[64];
gpg_agent_sign("zfogg", data_to_sign, sizeof(data_to_sign), signature);
```

### Known Hosts Verification
```c
#include "ascii-chat/crypto/known_hosts.h"

// Verify server identity key
uint8_t server_key[32];  // Received from server during handshake
if (!known_hosts_verify("localhost:27224", server_key)) {
    if (known_hosts_key_changed("localhost:27224", server_key)) {
        printf("ERROR: Server key has changed! Possible MITM attack!\n");
        return -1;
    }
    // New server: Ask user to trust
    if (user_confirms_server_key("localhost:27224", server_key)) {
        known_hosts_add("localhost:27224", server_key);
    }
}
```

### GitHub Key Fetching
```c
#include "ascii-chat/crypto/https_keys.h"

// Fetch SSH public keys from GitHub
uint8_t github_keys[8192];
size_t keys_len;
https_keys_fetch_github("zfogg", github_keys, sizeof(github_keys), &keys_len);

// Verify server identity against GitHub key
uint8_t server_identity_key[32];  // From handshake
if (verify_key_in_buffer(server_identity_key, github_keys, keys_len)) {
    printf("Server identity verified via GitHub!\n");
}
```

## Key Concepts

### X25519 Key Exchange
- **Ephemeral:** New key pair for each connection
- **Perfect Forward Secrecy:** Old connections safe even if long-term key compromised
- **32-byte keys:** Compact, secure
- **libsodium:** Hardware-optimized, constant-time

### XSalsa20-Poly1305 Encryption
- **Authenticated encryption:** Detects tampering
- **Nonce-based:** Different nonce per packet (prevents replay)
- **MAC verification:** Automatic, returns error on mismatch
- **Speed:** ~20μs per KB on modern CPUs

### HMAC-SHA256 Authentication
- **Challenge-response:** Server sends random nonce
- **Password binding:** HMAC includes shared secret (MITM protection)
- **Constant-time verification:** No timing attacks

### Argon2id Password KDF
- **Memory-hard:** Resists GPU/ASIC brute-force
- **Interactive limits:** ~0.1s, ~64MB memory
- **Deterministic salt:** Same password produces same key

### Session Rekeying
- **Automatic:** After 1 hour or 1M packets (whichever first)
- **DH re-exchange:** New ephemeral keys generated
- **Seamless:** No interruption to connection
- **Test mode:** 30 seconds / 1000 packets for fast testing

## Configuration

### Environment Variables
- `SSH_AUTH_SOCK` - SSH agent socket (Unix domain socket or named pipe)
- `GNUPGHOME` - GPG home directory (default: ~/.gnupg)
- `CRITERION_TEST` / `TESTING` - Enable test mode (reduced rekeying thresholds)

### Configuration Files
- `~/.ascii-chat/authorized_clients.txt` - Server whitelist (one key per line)
- `~/.ascii-chat/known_hosts` - SSH-style known hosts (hostname:port ssh-ed25519 <key>)
- `~/.ascii-chat/acds_keys/` - Cached ACDS server keys

## Dependencies

- **libsodium** - Modern cryptography (X25519, XSalsa20-Poly1305, Argon2id, Ed25519)
- **BearSSL** - TLS for HTTPS key fetching (OpenSSL compatibility)
- **OpenSSL** (optional) - For gpg-agent communication

## Building

```bash
# Configure and build
cmake --preset default -B build
cmake --build build

# Build just the crypto library
cmake --build build --target ascii-chat-lib-crypto

# Run crypto tests
ctest --test-dir build -R crypto
```

## Performance

| Operation | Time | Notes |
|-----------|------|-------|
| crypto_init() | ~100μs | libsodium init + key gen |
| Key exchange | ~500μs | X25519 computation |
| Encrypt 1KB | ~20μs | XSalsa20 |
| Decrypt 1KB | ~20μs | XSalsa20 + Poly1305 verify |
| SSH agent sign | ~1ms | IPC + SSH protocol |
| GPG agent sign | ~50ms | IPC + GPG protocol |
| HTTPS key fetch | ~200ms | Network + TLS |

## Testing

```bash
# Unit tests
ctest --test-dir build -R crypto

# Handshake tests
ctest --test-dir build -R handshake

# SSH key tests
ctest --test-dir build -R ssh

# All crypto-related tests
ctest --test-dir build -R crypto
```

## Security Properties

✅ **Encryption:**
- XSalsa20-Poly1305 (AEAD)
- Unique nonces per packet (session_id || counter)
- Automatic MAC verification
- Constant-time comparison

✅ **Key Exchange:**
- X25519 ephemeral keys
- Forward secrecy
- Optional authentication via SSH/GPG

✅ **Authentication:**
- HMAC-SHA256 challenge-response
- Ed25519 signatures
- SSH agent integration (keys never in memory)
- GPG agent integration (keys never in memory)

⚠️ **Known Limitations:**
- TOFU (Trust On First Use) for known_hosts - first connection vulnerable to MITM
- No post-quantum cryptography (planned for future)
- No Certificate Authority (planned: verification server)

## Debugging

### Enable Verbose Logging
```bash
./build/bin/ascii-chat --log-level debug server --grep "/crypto|handshake/i"
```

### Inspect Known Hosts
```bash
cat ~/.ascii-chat/known_hosts
```

### Check SSH Agent
```bash
ssh-add -L  # List available keys
```

### Check GPG Agent
```bash
gpg --list-keys
echo "test" | gpg -s  # Test agent availability
```

## Further Reading

- **Complete documentation:** docs/topics/crypto.dox
- **Module organization:** docs/topics/module_crypto_organization.dox
- **Client-specific:** docs/topics/client_crypto.dox
- **Server-specific:** docs/topics/server_crypto.dox
- **Protocol specification:** docs/handshake_protocol.md

## Contributing

When modifying crypto code:

1. **Always use `sodium_memcmp()`** for comparing sensitive data (keys, MACs)
2. **Use `SAFE_MALLOC/FREE`** for all memory allocation
3. **Add comprehensive comments** - cryptography is complex
4. **Test timing** - ensure no timing variations in comparisons
5. **Update Doxygen docs** - keep inline documentation current

## License

Same as ascii-chat project.
