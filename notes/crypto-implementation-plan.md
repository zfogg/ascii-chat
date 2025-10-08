# ASCII-Chat Cryptography Implementation Plan

**Status**: ✅ **PRODUCTION READY** (October 2025)
**Version**: 2.0
**Security Review**: Completed with all critical issues resolved

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Implementation Status](#implementation-status)
3. [Architecture Overview](#architecture-overview)
4. [Security Model](#security-model)
5. [Implemented Features](#implemented-features)
6. [Future Enhancements](#future-enhancements)
7. [References](#references)

---

## Executive Summary

ASCII-Chat implements **end-to-end encryption by default** using modern cryptographic primitives from libsodium. The implementation follows a progressive security model with 6 authentication levels, from default ephemeral encryption to paranoid multi-factor authentication.

### Key Achievements

- ✅ **Encrypted by default** - No configuration required
- ✅ **SSH key integration** - Use existing Ed25519 keys with SSH agent support
- ✅ **GitHub/GitLab integration** - Fetch public keys automatically via HTTPS (BearSSL)
- ✅ **Password authentication** - Argon2id-based with MITM protection
- ✅ **Known hosts** - SSH-style server verification with IP-based TOFU
- ✅ **Client whitelisting** - Server-side access control
- ✅ **Forward secrecy** - Ephemeral key exchange per connection
- ✅ **All security vulnerabilities resolved** - Including timing attacks, MITM, replay attacks

### Cryptographic Primitives

- **Key Exchange**: X25519 (Curve25519 ECDH)
- **Encryption**: XSalsa20-Poly1305 (AEAD)
- **Password Hashing**: Argon2id (memory-hard KDF)
- **Identity Authentication**: Ed25519 (EdDSA signatures)
- **TLS**: BearSSL (for GitHub/GitLab key fetching)
- **Randomness**: libsodium's `randombytes_buf()` (platform CSPRNG)

---

## Implementation Status

### ✅ Completed (Production Ready)

#### Core Cryptography (`lib/crypto/crypto.c/h`)
- [x] libsodium initialization and teardown
- [x] X25519 key pair generation (ephemeral and from Ed25519)
- [x] DH shared secret computation
- [x] Argon2id password derivation
- [x] XSalsa20-Poly1305 packet encryption/decryption
- [x] Session ID-based nonce generation (replay protection)
- [x] Constant-time HMAC computation and verification
- [x] Memory wiping on cleanup

#### Handshake Protocol (`lib/crypto/handshake.c/h`)
- [x] Client-server key exchange with DH
- [x] Ed25519 signature-based identity authentication
- [x] Bidirectional challenge-response (mutual authentication)
- [x] Password HMAC bound to shared secret (MITM protection)
- [x] Encrypted packet wrapping with AEAD
- [x] Support for all 6 authentication modes

#### Key Management (`lib/crypto/keys.c/h`)
- [x] Ed25519 SSH key parsing (OpenSSH format)
- [x] Encrypted private key support with passphrase prompts
- [x] SSH agent integration (Unix sockets, automatic fallback)
- [x] Ed25519 to X25519 conversion (identity vs encryption separation)
- [x] Public key formatting and display
- [x] GitHub/GitLab username parsing (`github:username`, `gitlab:username`)
- [x] Authorized_keys file parsing (whitelist support)
- [x] Constant-time key comparisons (sodium_memcmp)

#### HTTPS Client (`lib/crypto/http_client.c/h`)
- [x] BearSSL integration for TLS 1.2/1.3
- [x] System CA certificate loading (macOS/Linux/Windows)
- [x] PEM decoding and X.509 trust anchor extraction
- [x] HTTPS GET requests with SNI support
- [x] GitHub SSH key fetching (`github.com/username.keys`)
- [x] GitLab SSH key fetching (`gitlab.com/username.keys`)
- [x] GitHub GPG key fetching (`github.com/username.gpg`)
- [x] GitLab GPG key fetching (`gitlab.com/username.gpg`)
- [x] Ed25519 key filtering (RSA keys rejected for SSH)
- [x] PGP public key block parsing (armored ASCII format)

#### Known Hosts (`lib/crypto/known_hosts.c/h`)
- [x] SSH-style known_hosts file format
- [x] IP-based host verification (DNS hijacking protection)
- [x] IPv4 and IPv6 support with bracket notation
- [x] First-use prompts with fingerprint display
- [x] Key change detection with scary warning
- [x] Trust-on-first-use (TOFU) model

#### Security Hardening
- [x] Timing attack mitigation (constant-time comparisons)
- [x] Replay attack prevention (session IDs in nonces)
- [x] MITM attack protection (password bound to shared secret)
- [x] Mutual authentication (bidirectional challenge-response)
- [x] Code duplication elimination (shared auth functions)

### 🚧 Planned (Future Enhancement)

#### Session Discovery Service (Issue #82)
- [ ] Discovery server implementation (HTTP/WebSocket API)
- [ ] Session string generation (memorable identifiers)
- [ ] Visual fingerprint generation (SSH randomart-style)
- [ ] Unified binary mode detection (session start/join vs legacy)
- [ ] Password-protected ephemeral sessions
- [ ] WebRTC signaling integration (Issue #75)
- [ ] ACIP RFC Section 7: Session Discovery Protocol

#### Advanced Features
- [ ] Post-quantum cryptography (Kyber/Dilithium when libsodium adds support)
- [ ] Certificate transparency log (public key auditing)
- [ ] Perfect forward secrecy enhancement (Double Ratchet protocol)
- [ ] Out-of-band fingerprint verification helpers (QR codes)

---

## Architecture Overview

### File Structure

```
lib/crypto/
├── crypto.c/h              # Core cryptographic operations (533 lines)
│   ├── crypto_init()                   # Initialize libsodium
│   ├── crypto_generate_keypair()       # X25519 ephemeral keys
│   ├── crypto_compute_shared_secret()  # DH key exchange
│   ├── crypto_derive_key_from_password() # Argon2id KDF
│   ├── crypto_encrypt_packet()         # XSalsa20-Poly1305 encryption
│   ├── crypto_decrypt_packet()         # XSalsa20-Poly1305 decryption
│   ├── crypto_compute_auth_response()  # HMAC with shared secret binding
│   └── crypto_verify_auth_response()   # Constant-time HMAC verification
│
├── handshake.c/h           # Cryptographic handshake protocol
│   ├── crypto_handshake_client()       # Client-side handshake
│   ├── crypto_handshake_server()       # Server-side handshake
│   └── (Internal state machines for 6-packet bidirectional protocol)
│
├── keys.c/h                # SSH key management (1,580 lines)
│   ├── parse_private_key()             # OpenSSH Ed25519 key parsing
│   ├── parse_public_key()              # Multi-format key parsing
│   ├── ssh_agent_has_specific_key()    # SSH agent detection
│   ├── ed25519_sign_message()          # Identity signatures (agent or in-memory)
│   ├── ed25519_verify_signature()      # Signature verification
│   ├── fetch_github_keys()             # GitHub username → SSH keys
│   ├── fetch_gitlab_keys()             # GitLab username → SSH keys
│   └── parse_authorized_keys()         # Whitelist file parsing
│
├── http_client.c/h         # BearSSL HTTPS client (531 lines)
│   ├── https_get()                     # Generic HTTPS GET
│   ├── fetch_github_ssh_keys()         # GitHub SSH keys
│   ├── fetch_gitlab_ssh_keys()         # GitLab SSH keys
│   ├── fetch_github_gpg_keys()         # GitHub GPG keys
│   ├── fetch_gitlab_gpg_keys()         # GitLab GPG keys
│   └── (Internal BearSSL TLS setup and CA certificate handling)
│
└── known_hosts.c/h         # SSH-style known hosts
    ├── check_known_host()              # Verify server key
    ├── add_known_host()                # Save new server
    └── remove_known_host()             # Revoke server key
```

### Dependency Graph

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                     │
│             (src/server.c, src/client.c)                │
└─────────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│                  Handshake Protocol                      │
│                (lib/crypto/handshake.c)                 │
└─────────────────────────────────────────────────────────┘
           │                    │                    │
           ▼                    ▼                    ▼
┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│ Core Crypto      │  │ Key Management   │  │ Known Hosts      │
│ (crypto.c)       │  │ (keys.c)         │  │ (known_hosts.c)  │
│                  │  │                  │  │                  │
│ • X25519 DH      │  │ • SSH keys       │  │ • IP-based TOFU  │
│ • XSalsa20       │  │ • SSH agent      │  │ • Fingerprints   │
│ • Argon2id       │  │ • Ed25519        │  │                  │
└──────────────────┘  └──────────────────┘  └──────────────────┘
                         │
                         ▼
                  ┌──────────────────┐
                  │ HTTPS Client     │
                  │ (http_client.c)  │
                  │                  │
                  │ • BearSSL        │
                  │ • GitHub API     │
                  │ • GitLab API     │
                  └──────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│                  External Dependencies                   │
│                                                          │
│  • libsodium (crypto primitives)                        │
│  • BearSSL (TLS for HTTPS)                             │
│  • System CA certificates (/etc/ssl/certs/*)           │
│  • SSH agent (optional, $SSH_AUTH_SOCK)                │
└─────────────────────────────────────────────────────────┘
```

---

## Security Model

### The MITM Problem

ASCII-Chat faces a **fundamental cryptographic challenge**: there is no pre-existing trust infrastructure like the Certificate Authority (CA) system used by HTTPS.

**Without verification:**
- ✅ Privacy: Encrypted against passive eavesdropping (ISP, WiFi admin)
- ❌ Security: Vulnerable to active Man-in-the-Middle (MITM) attacks

**With verification:**
- ✅ Privacy: Encrypted against passive eavesdropping
- ✅ Security: Protected against MITM attacks via key pinning

### Progressive Security Ladder (6 Levels)

#### Level 1: Default Encrypted (Privacy)

**No configuration required** - encryption just works:

```bash
# Server
./ascii-chat-server

# Client
./ascii-chat-client
```

**What happens:**
- Both sides generate ephemeral X25519 keypairs (in-memory only)
- DH key exchange establishes shared secret
- Server displays public key fingerprint (optional verification)
- All packets encrypted with XSalsa20-Poly1305

**Security:**
- ✅ Protects against passive eavesdropping
- ✅ Forward secrecy (new keys per connection)
- ⚠️ Vulnerable to active MITM attacks

**Use case:** "I don't want my coffee shop WiFi admin watching my video chat"

#### Level 2: Password Authentication (Security)

**Recommended for most users:**

```bash
# Server with password
./ascii-chat-server --password mySecretPass123

# Client with password
./ascii-chat-client --password mySecretPass123
```

**What happens:**
- Ephemeral DH key exchange (same as Level 1)
- Server sends random challenge nonce
- Client computes `HMAC(password_key, nonce || shared_secret)`
- Server verifies `HMAC(password_key, nonce || shared_secret)`
- **Mutual authentication**: Server also proves password knowledge

**Security:**
- ✅ Protects against passive eavesdropping
- ✅ Protects against MITM attacks (attacker can't prove password)
- ✅ Forward secrecy
- ✅ Mutual authentication (both sides verify password)

**Use case:** "I need actual security, not just privacy"

**How to share password:** Text it to your friend, Signal message, phone call, in person. Same model as WiFi passwords.

#### Level 3: SSH Key Pinning (Strong Security)

**Leverage existing SSH keys:**

```bash
# Server uses existing SSH key
./ascii-chat-server --key ~/.ssh/id_ed25519

# Client verifies using GitHub
./ascii-chat-client --server-key github:zfogg

# Or paste SSH public key directly
./ascii-chat-client --server-key "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5..."

# Or use known_hosts (automatic after first connection)
./ascii-chat-client  # Checks ~/.ascii-chat/known_hosts
```

**What happens:**
- Server signs ephemeral DH key with long-term Ed25519 identity key
- Server sends: `[ephemeral_pk][identity_pk][signature]`
- Client verifies signature cryptographically binds identity to ephemeral key
- Client checks identity key against `--server-key` or known_hosts
- If mismatch: **ABORT** with scary MITM warning

**Security:**
- ✅ Protects against passive eavesdropping
- ✅ Protects against MITM attacks (cryptographically verified)
- ✅ Forward secrecy (ephemeral keys per connection)
- ✅ No shared secret (public keys can be shared openly)
- ✅ SSH keys are authentication-only, never used for encryption

**Use case:** "Want SSH-like security, already have SSH keys"

**SSH Agent Integration:**
```bash
# Add encrypted key to agent (prompts for password ONCE)
ssh-add ~/.ssh/id_ed25519

# Start server - uses agent, NO password prompt!
./ascii-chat-server --key ~/.ssh/id_ed25519
# INFO: Using SSH agent for this key (agent signing + ephemeral encryption)
```

**Key Architecture:**
- ✅ **Identity signing**: SSH key proves identity via Ed25519 signature
- ✅ **Encryption**: Ephemeral X25519 keys generated fresh per connection
- ✅ **Cryptographic binding**: Signature covers ephemeral key
- ✅ **Forward secrecy**: If SSH key compromised later, past sessions remain secure

#### Level 4: Client Whitelisting (Restricted Access)

**Server only accepts specific clients:**

```bash
# Server whitelist using GitHub usernames (fetches keys automatically!)
./ascii-chat-server --client-keys github:alice,github:bob,github:carol

# Or use SSH authorized_keys format (everyone knows this!)
./ascii-chat-server --client-keys ~/.ssh/authorized_keys

# Or paste SSH public keys directly
./ascii-chat-server --client-keys "ssh-ed25519 AAAAC3...,ssh-ed25519 AAAAB3..."
```

**What happens:**
- Server loads whitelist of allowed client public keys
- During handshake, server verifies client's identity key is in whitelist
- If not in whitelist: Reject connection
- Server logs all connection attempts (audit trail)

**Security:**
- ✅ Protects against passive eavesdropping
- ✅ Server protected from unauthorized clients
- ✅ Audit trail of who connected
- ✅ Revocable access (remove key from whitelist, restart server)
- ⚠️ Client still vulnerable to MITM (unless they use `--server-key`)

**Use case:** "Private server, only my friends can connect"

**Client workflow:**
1. Client displays their SSH public key on startup
2. Client sends key to server operator (via email/Signal/etc.)
3. Server operator adds to authorized_keys or uses GitHub username
4. Client can now connect

#### Level 5: Defense in Depth (Maximum Security)

**Combine all security features:**

```bash
# Server: Password + SSH key + client whitelist
./ascii-chat-server \
  --key ~/.ssh/id_ed25519 \
  --password mySecretPass123 \
  --client-keys ~/.ssh/authorized_keys

# Client: Password + server key verification
./ascii-chat-client \
  --password mySecretPass123 \
  --server-key github:zfogg
```

**Security:**
- ✅ Password authentication (both sides verify password)
- ✅ SSH key pinning (client verifies server identity)
- ✅ Client whitelist (server only accepts known clients)
- ✅ Forward secrecy
- ✅ Defense in depth (multiple layers of security)

**Use case:** "Paranoid security for sensitive communications"

#### Level 6: Opt-Out (Debugging Only)

**Disable encryption for debugging:**

```bash
# Server
./ascii-chat-server --no-encrypt

# Client
./ascii-chat-client --no-encrypt
```

**Security:**
- ❌ No protection
- ⚠️ Anyone on the network can see everything

**Use case:** "I'm debugging and need to see raw packets with tcpdump"

---

## Implemented Features

### 1. Core Cryptographic Operations

**File:** `lib/crypto/crypto.c/h`

#### Initialization
```c
crypto_result_t crypto_init(void);
void crypto_cleanup(void);
```

- Calls `sodium_init()` to initialize libsodium
- Thread-safe (idempotent)
- Platform CSPRNG setup (automatic)

#### Key Generation
```c
crypto_result_t crypto_generate_keypair(uint8_t public_key[32], uint8_t private_key[32]);
```

- Generates ephemeral X25519 keypair
- Uses libsodium's `crypto_box_keypair()`
- Keys wiped on context cleanup

#### DH Key Exchange
```c
crypto_result_t crypto_compute_shared_secret(const uint8_t my_private[32],
                                              const uint8_t their_public[32],
                                              uint8_t shared_secret[32]);
```

- Computes X25519 Diffie-Hellman shared secret
- Uses libsodium's `crypto_scalarmult()`
- Constant-time implementation

#### Password Derivation
```c
crypto_result_t crypto_derive_key_from_password(const char *password,
                                                 uint8_t key[32]);
```

- Uses Argon2id (memory-hard KDF)
- Parameters: 64 MB memory, 2 iterations (interactive)
- Salt generated with `randombytes_buf()`

#### Packet Encryption/Decryption
```c
crypto_result_t crypto_encrypt_packet(crypto_context_t *ctx,
                                      const uint8_t *plaintext, size_t plaintext_len,
                                      uint8_t *ciphertext_out, size_t *ciphertext_len);

crypto_result_t crypto_decrypt_packet(crypto_context_t *ctx,
                                      const uint8_t *ciphertext, size_t ciphertext_len,
                                      uint8_t *plaintext_out, size_t *plaintext_len);
```

- XSalsa20-Poly1305 authenticated encryption (AEAD)
- Nonce format: `[session_id:16][counter:8]`
- Session IDs prevent replay attacks across connections
- Poly1305 MAC prevents tampering

#### Authentication Helpers
```c
crypto_result_t crypto_compute_auth_response(const crypto_context_t *ctx,
                                             const uint8_t nonce[32],
                                             uint8_t hmac_out[32]);

bool crypto_verify_auth_response(const crypto_context_t *ctx,
                                  const uint8_t nonce[32],
                                  const uint8_t expected_hmac[32]);
```

- Computes `HMAC(key, nonce || shared_secret)` - **MITM protection**
- Uses password key if available, otherwise shared secret
- Constant-time verification with `sodium_memcmp()`

### 2. Handshake Protocol

**File:** `lib/crypto/handshake.c/h`

#### Bidirectional Handshake Flow

```
Client                                    Server
  |                                         |
  |------ TCP Connect -------------------->|
  |                                         |
  |                                         | Generate ephemeral X25519
  |                                         | (or use --key SSH key)
  |                                         |
  |<----- KEY_EXCHANGE_INIT ---------------| (Packet Type 14)
  |       [32-byte X25519 ephemeral key]   |
  |       [32-byte Ed25519 identity key]   | (if --key)
  |       [64-byte signature]              | (signature of ephemeral key)
  |                                         |
  | Verify signature (if present)          |
  | Check against --server-key             |
  | Check known_hosts                      |
  | Compute DH shared secret               |
  |                                         |
  |------ KEY_EXCHANGE_RESPONSE ---------->| (Packet Type 15)
  |       [32-byte X25519 ephemeral key]   |
  |       [32-byte Ed25519 identity key]   | (if --key)
  |       [64-byte signature]              | (signature of ephemeral key)
  |                                         |
  |                                         | Verify signature (if present)
  |                                         | Check whitelist (if --client-keys)
  |                                         | Compute DH shared secret
  |                                         |
  |<----- AUTH_CHALLENGE ------------------| (Packet Type 16)
  |       [32-byte server_nonce]           |
  |       [1-byte flags]                   | (password? key?)
  |                                         |
  | Compute HMAC(password_key,             |
  |              server_nonce || shared)   |
  |                                         |
  |------ AUTH_RESPONSE ------------------>| (Packet Type 17)
  |       [32-byte HMAC]                   |
  |       [32-byte client_nonce]           | (for mutual auth)
  |                                         |
  |                                         | Verify HMAC
  |                                         | Compute HMAC(password_key,
  |                                         |              client_nonce || shared)
  |                                         |
  |<----- SERVER_AUTH_RESPONSE ------------| (Packet Type 22)
  |       [32-byte server_HMAC]            | (mutual authentication)
  |                                         |
  | Verify server_HMAC                     |
  |                                         |
  |<----- HANDSHAKE_COMPLETE --------------| (Packet Type 18)
  |                                         |
  | ✅ Encryption active                    | ✅ Encryption active
  |                                         |
  |<===== ENCRYPTED PACKETS ===============>|
```

#### Key Features

- **6-packet protocol**: KEY_EXCHANGE_INIT → KEY_EXCHANGE_RESPONSE → AUTH_CHALLENGE → AUTH_RESPONSE → SERVER_AUTH_RESPONSE → HANDSHAKE_COMPLETE
- **Mutual authentication**: Both client and server prove knowledge of password/shared secret
- **MITM protection**: Password HMAC bound to DH shared secret
- **Signature binding**: Ed25519 signature cryptographically binds identity to ephemeral key
- **Replay protection**: Session IDs in nonces prevent cross-session replay

### 3. Key Management

**File:** `lib/crypto/keys.c/h`

#### SSH Key Formats Supported

1. **Ed25519 Keys** (⭐ RECOMMENDED)
   - `ssh-ed25519 AAAAC3NzaC1lZDI1NTE5... comment`
   - OpenSSH format (both encrypted and unencrypted)
   - Direct conversion to X25519 for DH (for display only, not encryption!)
   - Used for identity authentication via signatures

2. **X25519 Keys** (Raw)
   - 64-character hex string
   - Native DH key format
   - Rarely used (Ed25519 preferred)

3. **GitHub/GitLab SSH Keys**
   - `github:username` → Fetches from `https://github.com/username.keys`
   - `gitlab:username` → Fetches from `https://gitlab.com/username.keys`
   - Only Ed25519 keys accepted (RSA filtered out)

4. **GitHub/GitLab GPG Keys**
   - `github:username.gpg` → Fetches from `https://github.com/username.gpg`
   - `gitlab:username.gpg` → Fetches from `https://gitlab.com/username.gpg`
   - Returns PGP public key blocks in armored ASCII format
   - Supports multiple keys per user

#### SSH Agent Integration

**Automatic detection:**
```c
// When parsing encrypted SSH key, check if it's in SSH agent
bool ssh_agent_has_specific_key(const uint8_t ed25519_public_key[32]);
```

**Signature delegation:**
```c
// If use_ssh_agent=true, signs via SSH agent protocol
int ed25519_sign_message(const private_key_t *key,
                         const uint8_t *message, size_t message_len,
                         uint8_t signature[64]);
```

**Security architecture:**
- ✅ **Identity signing**: SSH agent (Ed25519) or in-memory Ed25519
- ✅ **Encryption keys**: Ephemeral X25519 (always!)
- ✅ **Forward secrecy**: Both modes use ephemeral keys for encryption

**Fallback behavior:**
- If key encrypted but NOT in agent: Prompt for passphrase
- If `SSH_AUTH_SOCK` not set: Fallback to passphrase prompt
- Password required only once per session if using agent

#### Key Parsing Functions

```c
// Parse SSH private key (supports encrypted keys)
int parse_private_key(const char *path, private_key_t *key_out);

// Parse SSH public key (multi-format support)
int parse_public_key(const char *input, public_key_t *key_out);

// Parse authorized_keys file for whitelist
int parse_authorized_keys(const char *path, public_key_t *keys,
                          size_t *num_keys, size_t max_keys);

// Convert Ed25519 to X25519 (for display only, not encryption)
int ed25519_to_x25519_public(const uint8_t ed25519_pk[32], uint8_t x25519_pk[32]);
```

#### Constant-Time Comparisons

**All cryptographic key comparisons use `sodium_memcmp()`:**

```c
// ❌ WRONG - timing attack vulnerability
if (memcmp(server_key, expected_key, 32) == 0) { ... }

// ✅ CORRECT - constant-time comparison
if (sodium_memcmp(server_key, expected_key, 32) == 0) { ... }
```

**Locations:**
- `lib/crypto/handshake.c:194` - Server identity verification
- `lib/crypto/handshake.c:363` - Client whitelist verification
- `lib/crypto/known_hosts.c:69` - Known hosts verification

### 4. HTTPS Client (BearSSL)

**File:** `lib/crypto/http_client.c/h`

#### Core HTTPS Functionality

```c
// Generic HTTPS GET request
char *https_get(const char *hostname, const char *path);

// GitHub/GitLab SSH key convenience wrappers
int fetch_github_ssh_keys(const char *username, char ***keys_out, size_t *num_keys_out);
int fetch_gitlab_ssh_keys(const char *username, char ***keys_out, size_t *num_keys_out);

// GitHub/GitLab GPG key convenience wrappers
int fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys_out);
int fetch_gitlab_gpg_keys(const char *username, char ***keys_out, size_t *num_keys_out);
```

#### BearSSL Integration

**Features:**
- TLS 1.2/1.3 support via BearSSL
- System CA certificate loading (macOS: `/etc/ssl/cert.pem`, Linux: `/etc/ssl/certs/ca-certificates.crt`)
- PEM decoding and X.509 trust anchor extraction
- SNI (Server Name Indication) support
- Proper certificate chain validation

**Why BearSSL?**
- **Tiny**: ~200KB static binary (vs OpenSSL's 3MB)
- **No malloc**: Uses caller-provided buffers (stack-based)
- **Paranoid**: Written by crypto expert Thomas Pornin
- **Constant-time**: Resistant to timing attacks by design

**Security:**
- 20-year longevity via system CA certificates (no manual cert updates)
- TLS protects GitHub/GitLab API requests
- Ed25519 key filtering prevents RSA key acceptance

#### Implementation Details

```c
// BearSSL socket callbacks
static int sock_read(void *ctx, unsigned char *buf, size_t len);
static int sock_write(void *ctx, const unsigned char *buf, size_t len);

// PEM decoding and trust anchor management
static int decode_pem_to_trust_anchors(const char *pem_data, size_t pem_size,
                                       br_x509_trust_anchor **anchors_out,
                                       size_t *num_anchors_out);
static void free_trust_anchors(br_x509_trust_anchor *anchors, size_t num_anchors);

// HTTP response parsing
static char *extract_http_body(const unsigned char *response, size_t response_len);

// SSH key filtering
static char **filter_ed25519_keys(const char *keys_text, size_t *num_keys_out);

// GPG key parsing
static char **parse_pgp_key_blocks(const char *gpg_text, size_t *num_keys_out);
```

### 5. Known Hosts (SSH-Style)

**File:** `lib/crypto/known_hosts.c/h`

#### File Format

**Location:** `~/.ascii-chat/known_hosts`

**Format:**
```
# ASCII-Chat Known Hosts
# Format: IP:port key-type public-key [comment]

# IPv4 example:
192.168.1.100:27224 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo... homeserver
10.0.0.50:8080 ssh-ed25519 AAAAB3NzaC1yc2EAAAADAQABAAABAQC... office-server

# IPv6 example (bracket notation):
[2001:db8::1]:27224 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBar... ipv6-server
[::1]:27224 ssh-ed25519 AAAAB3NzaC1yc2EAAAADAQABAAABAQC... localhost-ipv6
```

#### Security Design: IP Binding

**Why IP addresses instead of hostnames?**

1. **DNS Hijacking Prevention**: DNS can be spoofed → attacker points `example.com` to malicious IP
2. **Cryptographic Binding**: TCP connects to IP, not hostname
3. **IPv6 Support**: Each IP:port combination gets separate key binding

**Attack scenario with hostname binding (avoided):**
```
1. User connects to example.com:27224
2. DNS resolves to attacker IP 203.0.113.50
3. Attacker's server presents key_A
4. Client saves: "example.com:27224 → key_A"
5. Later, DNS changes to legitimate server 198.51.100.25
6. Legitimate server presents key_B
7. Client sees hostname match, ACCEPTS key_B  ❌ No MITM detection!
```

**With IP binding (current implementation):**
```
1. User connects to example.com:27224
2. DNS resolves to attacker IP 203.0.113.50
3. Attacker's server presents key_A
4. Client saves: "203.0.113.50:27224 → key_A"
5. Later, example.com resolves to legitimate IP 198.51.100.25
6. Different IP! No key stored, prompts user
7. User realizes IP changed, investigates  ✅ MITM detected!
```

#### Trust-On-First-Use (TOFU) Model

**First connection:**
```
The authenticity of host '192.168.1.100:27224' can't be established.
Ed25519 key fingerprint is: SHA256:abc123...
Are you sure you want to continue connecting (yes/no)? yes
```

**Subsequent connections:** Silently verify against known_hosts

**Key change detected:**
```
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!
Someone could be eavesdropping on you right now (man-in-the-middle attack)!

The server key for 192.168.1.100:27224 has changed.
Expected: ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo...
Received: ssh-ed25519 AAAAB3NzaC1yc2EAAAADAQABAAABAQC...

If this is expected (server reinstalled), remove old key:
  sed -i '/192.168.1.100:27224/d' ~/.ascii-chat/known_hosts

Connection ABORTED for security.
```

#### Functions

```c
// Check if server key matches known_hosts entry
// Returns: 1 = match, 0 = first connection, -1 = mismatch (MITM!)
int check_known_host(const char *hostname, uint16_t port,
                     const uint8_t server_key[32]);

// Add server to known_hosts after user approval
int add_known_host(const char *hostname, uint16_t port,
                   const uint8_t server_key[32]);

// Remove server from known_hosts (for key rotation)
int remove_known_host(const char *hostname, uint16_t port);
```

---

## Future Enhancements

### 1. Session Discovery Service (Issue #82)

**Goal:** Transform ASCII-Chat from requiring manual IP/port coordination into a phone-number-like system with memorable session strings.

#### Architecture

**Session Discovery Protocol (SDP)** - ACIP RFC Section 7

```
┌─────────────────────────────────────────┐
│           ACIP Protocol Stack           │
├─────────────────────────────────────────┤
│ 7. Session Discovery Protocol (SDP)     │ ← NEW
│    - Session string management          │
│    - Peer introduction/signaling        │
│    - Visual fingerprint verification    │
├─────────────────────────────────────────┤
│ 6. Transport Abstraction Layer          │
│    - TCP, WebSocket, WebRTC DataChannel │
├─────────────────────────────────────────┤
│ 1-5. Core ACIP (Existing)               │
│    - Packet format, media, control      │
└─────────────────────────────────────────┘
```

#### User Experience

**Starting a session:**
```bash
$ ascii-chat
🎥 Starting ASCII-Chat session...
📡 Registering with discovery service...

┌─────────────────────────────────────┐
│  Share this code with others:       │
│                                     │
│       purple-mountain-7843          │
│                                     │
│  Session expires in 24 hours        │
└─────────────────────────────────────┘

🔑 Your identity fingerprint:

┌─────────────────┐
│ o.          .o  │
│  +o.        o+  │   Ed25519: SHA256:abc123...
│   =+o      o+=  │   (Alice's MacBook Pro)
│    .=o    o=.   │
│     .o.  .o.    │
│      .o..o.     │
│       .==.      │
│        ++       │
└─────────────────┘

🔐 Set session password: ████████
✅ Waiting for participants...
```

**Joining a session:**
```bash
$ ascii-chat purple-mountain-7843
🔍 Looking up session...
✅ Found session (2/4 participants)

🔑 Host identity fingerprint:

┌─────────────────┐
│ o.          .o  │
│  +o.        o+  │   Ed25519: SHA256:abc123...
│   =+o      o+=  │   (Alice's MacBook Pro)
└─────────────────┘

🔐 Verify this matches what Alice sees [Y/n]: Y
🔐 Enter session password: ████████
📡 Connecting...
✅ Connected!
```

#### Visual Fingerprint Verification

**SSH Randomart-style identity verification:**

- Deterministic ASCII art pattern from Ed25519 public key hash
- Easy to compare over phone: "Do you see a plus sign in the middle?"
- Same algorithm as `ssh-keygen -lv` (proven UX)
- Prevents MITM attacks via out-of-band verification

#### Session String Format

**Requirements:**
- Memorable and easy to share (voice/text/email)
- Millions of unique combinations
- 24-hour expiration (ephemeral sessions)
- **No emoji** (cross-platform compatibility issues)
- **Example format**: `adjective-noun-number` (e.g., `purple-mountain-7843`)

#### Discovery API Contract

**POST /acip/v1/sessions**
```json
{
  "capabilities": {
    "video": true,
    "audio": true,
    "max_participants": 4
  },
  "security": {
    "requires_password": true,
    "ephemeral": true
  }
}
```

**Response:**
```json
{
  "session_string": "purple-mountain-7843",
  "session_id": "uuid",
  "signaling_endpoint": "wss://...",
  "expires_at": "ISO8601"
}
```

**GET /acip/v1/sessions/{session_string}**
```json
{
  "session_id": "uuid",
  "signaling_endpoint": "wss://...",
  "requires_password": true,
  "current_participants": 2,
  "max_participants": 4
}
```

#### Security Model

- Session strings expire (24h maximum recommended)
- Password authentication uses Argon2id (same as existing)
- Discovery server MUST NOT store plaintext passwords
- Visual fingerprints provide out-of-band verification
- Signaling traffic encrypted (TLS/WSS)

#### Unified Binary Design

**Mode detection:**
```c
int main(int argc, char *argv[]) {
  if (argc == 1) {
    // No arguments: Start new session
    return start_session_mode();
  } else if (argc == 2 && is_session_string(argv[1])) {
    // One argument matching session string pattern: Join mode
    return join_session_mode(argv[1]);
  } else {
    // Multiple arguments: Traditional client/server mode
    return legacy_mode(argc, argv);
  }
}
```

**Backward compatibility:**
```bash
# Legacy modes continue to work unchanged
ascii-chat --server --port 8080
ascii-chat --client --address 192.168.1.100 --port 8080
```

#### Implementation Plan

**Phase 1: Core Discovery Service**
- [ ] Session string generation (memorable algorithm)
- [ ] Discovery API server (HTTP/WebSocket)
- [ ] Database layer (SQLite reference implementation)

**Phase 2: Unified Binary**
- [ ] Main entry point refactor (mode detection)
- [ ] Discovery client (API integration)
- [ ] Terminal UI (password input, status)

**Phase 3: Visual Fingerprints**
- [ ] SSH randomart implementation
- [ ] Ed25519 → ASCII art conversion
- [ ] Terminal display formatting

**Phase 4: Protocol Integration**
- [ ] WebRTC signaling coordination (Issue #75)
- [ ] Transport abstraction layer
- [ ] ACIP RFC Section 7 specification

### 2. Post-Quantum Cryptography

**Timeline:** Wait for libsodium to add post-quantum support (in development)

**Candidates:**
- **Kyber**: NIST-selected post-quantum KEM
- **Dilithium**: NIST-selected post-quantum signatures
- **X25519-Kyber**: Hybrid combining classical + post-quantum

**Approach:**
- Hybrid mode for backward compatibility
- Gradual migration path
- Wait for libsodium integration (don't DIY crypto!)

### 3. Perfect Forward Secrecy Enhancement

**Current:** Forward secrecy per connection (new DH keys each time)

**Future:** Ratcheting (Signal/Double Ratchet protocol)

**Benefit:** Forward secrecy per message (compromise of current key doesn't affect previous messages)

**Cost:** More complex key management, state synchronization issues

**Decision:** Defer until proven need (current forward secrecy is sufficient for most use cases)

### 4. Certificate Transparency Log

**Inspired by:** Let's Encrypt Certificate Transparency

**Idea:** Public append-only log of all server public keys

**Benefits:**
- Detect rogue keys (someone impersonating your server)
- Audit trail of key changes
- Social accountability

**Implementation:**
- Merkle tree of Ed25519 public keys
- Signed by verification server
- Clients can request inclusion proofs

**Status:** Research phase

---

## References

### Documentation

- [ASCII-Chat Crypto Documentation](../docs/crypto.md) - Comprehensive security documentation
- [Issue #82: Session Discovery Service](https://github.com/zfogg/ascii-chat/issues/82) - RFC for discovery protocol
- [Issue #75: WebRTC P2P Architecture](https://github.com/zfogg/ascii-chat/issues/75) - Decentralized sessions
- [Issue #55: ACIP RFC](https://github.com/zfogg/ascii-chat/issues/55) - Standardize ASCII-Chat Internet Protocol

### Cryptography

- [libsodium Documentation](https://doc.libsodium.org/)
- [RFC 7748: Elliptic Curves for Security (X25519)](https://tools.ietf.org/html/rfc7748)
- [RFC 8439: ChaCha20 and Poly1305](https://tools.ietf.org/html/rfc8439)
- [Argon2: Password Hashing Competition Winner](https://github.com/P-H-C/phc-winner-argon2)
- [NaCl: Networking and Cryptography library](https://nacl.cr.yp.to/)

### SSL/TLS

- [BearSSL Official Site](https://bearssl.org/)
- [BearSSL API Documentation](https://bearssl.org/api1.html)
- [System CA Certificates Best Practices](https://www.happyassassin.net/posts/2015/01/12/a-note-about-ssltls-trusted-certificate-stores-and-platforms/)

### Security Analysis

- [Signal Protocol](https://signal.org/docs/)
- [SSH Protocol Architecture](https://tools.ietf.org/html/rfc4251)
- [Trust On First Use (TOFU) Model](https://en.wikipedia.org/wiki/Trust_on_first_use)

---

## Appendix: Code Locations

### Core Files

| Component | File | Lines | Description |
|-----------|------|-------|-------------|
| Core Crypto | `lib/crypto/crypto.c/h` | 689 | X25519 DH, XSalsa20-Poly1305, Argon2id |
| Handshake | `lib/crypto/handshake.c/h` | 1,073 | 6-packet bidirectional protocol |
| Key Management | `lib/crypto/keys.c/h` | 1,580 | SSH keys, agent, GitHub/GitLab |
| HTTPS Client | `lib/crypto/http_client.c/h` | 531 | BearSSL TLS, SSH/GPG key fetching |
| Known Hosts | `lib/crypto/known_hosts.c/h` | 156 | IP-based TOFU, fingerprints |

### Security Fixes

| Issue | Severity | File:Line | Description |
|-------|----------|-----------|-------------|
| Password MITM | Critical | `crypto.c:655-689` | HMAC bound to shared secret |
| Mutual Auth | High | `handshake.c:946` | Bidirectional challenge-response |
| Replay Attacks | Medium | `crypto.c:298-310` | Session IDs in nonces |
| Timing Attacks | Critical | `handshake.c:194,363` <br/> `known_hosts.c:69` | Constant-time `sodium_memcmp()` |
| Code Duplication | Low | `crypto.c:655-689` | Shared auth functions |

### Key Functions

**Encryption/Decryption:**
- `lib/crypto/crypto.c:330` - `crypto_encrypt_packet()`
- `lib/crypto/crypto.c:422` - `crypto_decrypt_packet()`

**Authentication:**
- `lib/crypto/crypto.c:655` - `crypto_compute_auth_response()`
- `lib/crypto/crypto.c:672` - `crypto_verify_auth_response()`

**SSH Agent:**
- `lib/crypto/keys.c:438` - `ssh_agent_has_specific_key()`
- `lib/crypto/keys.c:502` - `ed25519_sign_message()` (agent delegation)
- `lib/crypto/keys.c:823` - Encrypted key detection with agent check

**GitHub/GitLab:**
- `lib/crypto/http_client.c:104` - `https_get()` (BearSSL HTTPS)
- `lib/crypto/http_client.c:348` - `fetch_github_ssh_keys()`
- `lib/crypto/http_client.c:376` - `fetch_gitlab_ssh_keys()`
- `lib/crypto/http_client.c:477` - `fetch_github_gpg_keys()`
- `lib/crypto/http_client.c:505` - `fetch_gitlab_gpg_keys()`

**Known Hosts:**
- `lib/crypto/known_hosts.c:30` - `check_known_host()` (TOFU verification)
- `lib/crypto/known_hosts.c:83` - `add_known_host()` (save trusted server)

---

**Document Version:** 2.0
**Last Updated:** October 2025
**Status:** Production Ready
**Maintainer:** ASCII-Chat Development Team

🤖 *Generated with assistance from [Claude Code](https://claude.ai/code)*

Co-Authored-By: Claude <noreply@anthropic.com>
