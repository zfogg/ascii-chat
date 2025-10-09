# ASCII-Chat Cryptography Documentation

**Version:** 2.0
**Last Updated:** October 2025
**Status:** Production

## Table of Contents

1. [Overview](#overview)
2. [Philosophy & Threat Model](#philosophy--threat-model)
3. [Cryptographic Primitives](#cryptographic-primitives)
4. [Protocol Architecture](#protocol-architecture)
5. [Handshake Protocol](#handshake-protocol)
6. [Key Management](#key-management)
7. [Authentication Modes](#authentication-modes)
8. [Packet Encryption](#packet-encryption)
9. [Security Considerations](#security-considerations)
10. [Known Vulnerabilities](#known-vulnerabilities)
11. [Future Enhancements](#future-enhancements)

---

## Overview

ASCII-Chat implements **end-to-end encryption by default** using modern cryptographic primitives from [libsodium](https://doc.libsodium.org/). All data packets (headers and payloads) are encrypted after the initial handshake, protecting against eavesdropping and tampering.

### Key Features

- **Encrypted by default** - No configuration required
- **Modern crypto** - X25519, XSalsa20-Poly1305, Argon2id
- **SSH key integration** - Use existing Ed25519 keys
- **SSH agent support** - Auto-adds keys, eliminates password prompts
- **GitHub/GitLab integration** - Fetch public keys automatically
- **Password protection** - Optional shared password authentication
- **Client whitelisting** - Server-side access control
- **Known hosts** - SSH-style server verification
- **Forward secrecy** - Ephemeral key exchange per connection

### Non-Goals

❌ **Not a replacement for TLS/HTTPS** - Different trust model
❌ **Not anonymous** - Focus is encryption, not anonymity
❌ **Not quantum-resistant** - Uses elliptic curve cryptography (X25519)

---

## Philosophy & Threat Model

### The MITM Problem

ASCII-Chat faces a **fundamental cryptographic challenge**: there is no pre-existing trust infrastructure like the Certificate Authority (CA) system used by HTTPS. This creates a security tradeoff:

**Without verification:**
- ✅ Privacy: Encrypted against passive eavesdropping (ISP, WiFi admin, etc.)
- ❌ Security: Vulnerable to active Man-in-the-Middle (MITM) attacks

**With verification:**
- ✅ Privacy: Encrypted against passive eavesdropping
- ✅ Security: Protected against MITM attacks via key pinning

### Default Behavior: Privacy Without Trust

**By default**, ASCII-Chat provides **privacy but not authentication**:

```bash
# Server (ephemeral key generated)
./ascii-chat-server

# Client (ephemeral key generated, no verification)
./ascii-chat-client
```

**Why this default?**
1. **No certificate infrastructure** - Unlike HTTPS, there's no global CA system
2. **Ease of use** - Works immediately without configuration
3. **Better than nothing** - Protects against passive attacks (most common threat)
4. **User choice** - Advanced users can add verification with `--server-key`

This is similar to **Bluetooth pairing** or **Signal safety numbers** - the first connection is vulnerable, but subsequent connections can be verified.

### Trust Models Supported

| Mode | Trust Mechanism | MITM Protection | Use Case |
|------|----------------|-----------------|----------|
| **Default** | None (ephemeral DH) | ❌ | Quick sessions, low-threat environments |
| **Password** | Shared secret | ✅ | Friends exchanging password out-of-band |
| **SSH Keys** | Key pinning | ✅ | Tech users with existing SSH keys |
| **GitHub/GitLab** | Social proof + keys | ✅ | Verify identity via public profiles |
| **Known Hosts** | First-use trust | ⚠️ | Like SSH - detect key changes |
| **Whitelist** | Pre-approved keys | ✅ | Private servers, access control |

### Comparison to Other Protocols

| Protocol | Default Encryption | Trust Model | Verification Difficulty |
|----------|-------------------|-------------|------------------------|
| **HTTPS** | ✅ Always | CA system | Automatic (OS trust store) |
| **SSH** | ✅ Always | Known hosts | Manual (first connection prompts) |
| **Signal** | ✅ Always | Safety numbers | Manual (QR code scanning) |
| **ASCII-Chat** | ✅ Always | Ephemeral DH | Optional (--server-key flag) |
| **Zoom** | ✅ Sometimes | Central server | None (trust Zoom) |

---

## Cryptographic Primitives

ASCII-Chat uses [libsodium](https://doc.libsodium.org/), a modern, portable, easy-to-use crypto library based on NaCl.

### Core Algorithms

#### X25519 (Key Exchange)
- **Algorithm:** Elliptic Curve Diffie-Hellman (ECDH) on Curve25519
- **Key Size:** 32 bytes (256 bits)
- **Purpose:** Establish shared secret between client and server
- **Properties:** Forward secrecy, constant-time operations
- **Function:** `crypto_box_beforenm()`

**Why X25519?**
- Fast: ~40,000 operations/second on modern CPUs
- Secure: No known practical attacks, constant-time implementation
- Small: 32-byte keys, 32-byte shared secrets
- Standard: RFC 7748, widely used (TLS 1.3, SSH, Signal)

#### XSalsa20-Poly1305 (Encryption)
- **Algorithm:** Stream cipher (XSalsa20) + MAC (Poly1305)
- **Key Size:** 32 bytes (256 bits)
- **Nonce Size:** 24 bytes (192 bits)
- **MAC Size:** 16 bytes (128 bits)
- **Purpose:** Encrypt packet data with authenticated encryption
- **Function:** `crypto_secretbox_easy()`

**Why XSalsa20-Poly1305?**
- AEAD: Authenticated Encryption with Associated Data (prevents tampering)
- Fast: ~700 MB/s encryption on modern CPUs
- Nonce-misuse resistant: Large 192-bit nonce makes collisions astronomically unlikely
- Proven: Used in libsodium, NaCl, and many production systems

**Encryption formula:**
```
ciphertext = XSalsa20(key, nonce, plaintext) + Poly1305(key, ciphertext)
```

#### Argon2id (Password Hashing)
- **Algorithm:** Argon2id (hybrid Argon2i + Argon2d)
- **Purpose:** Derive encryption keys from passwords
- **Function:** `crypto_pwhash()`
- **Parameters:**
  - Memory: 64 MB (interactive limit)
  - Operations: 2 (OPSLIMIT_INTERACTIVE)
  - Parallelism: 1

**Why Argon2id?**
- Memory-hard: Resistant to GPU/ASIC brute-force attacks
- Modern: Winner of Password Hashing Competition (2015)
- Hybrid: Combines data-dependent (Argon2d) and data-independent (Argon2i) modes
- Tunable: Can increase difficulty as hardware improves

#### Ed25519 (Signatures - SSH Keys)
- **Algorithm:** EdDSA signatures on Edwards curve (Curve25519)
- **Key Size:** 32 bytes public key, 64 bytes private key (seed + public)
- **Signature Size:** 64 bytes
- **Purpose:** Authenticate clients with SSH keys
- **Functions:** `crypto_sign_detached()`, `crypto_sign_verify_detached()`

**Ed25519 to X25519 Conversion:**
```c
// Convert Ed25519 public key to X25519 for DH
crypto_sign_ed25519_pk_to_curve25519(x25519_pk, ed25519_pk);

// Convert Ed25519 private key to X25519 for DH
crypto_sign_ed25519_sk_to_curve25519(x25519_sk, ed25519_sk);
```

This allows using existing SSH Ed25519 keys for both **signing** (authentication) and **key exchange** (encryption).

### Randomness Source

**CSPRNG:** `randombytes_buf()` (libsodium)
- **Implementation:** Platform-specific secure random
  - Linux/BSD: `/dev/urandom`
  - Windows: `CryptGenRandom()` / `BCryptGenRandom()`
  - macOS: `arc4random_buf()`
- **Properties:** Cryptographically secure, non-blocking

---

## Protocol Architecture

### Packet Structure

All packets (encrypted and unencrypted) share a common header:

```c
typedef struct {
  uint32_t magic;     // 0xDEADBEEF (packet validation)
  uint16_t type;      // packet_type_t (see below)
  uint32_t length;    // payload length in bytes
  uint32_t crc32;     // CRC32 checksum of payload
  uint32_t client_id; // source client ID (0 = server)
} __attribute__((packed)) packet_header_t;  // 18 bytes
```

**Note:** All multi-byte fields are in **network byte order** (big-endian).

### Packet Types

#### Protocol Negotiation Packets (Always Unencrypted)
```c
PACKET_TYPE_PROTOCOL_VERSION       = 1   // Client → Server: Protocol version, compression support, encryption enabled
```

**Purpose:** Establishes basic protocol capabilities before any crypto handshake. Informs server whether client supports encryption and compression.

#### Crypto Handshake Packets (Always Unencrypted)
```c
PACKET_TYPE_CRYPTO_CAPABILITIES       = 14  // Client → Server: Supported crypto algorithms
PACKET_TYPE_CRYPTO_PARAMETERS         = 15  // Server → Client: Chosen algorithms + data sizes
PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT  = 16  // Server → Client: DH public key
PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP  = 17  // Client → Server: DH public key
PACKET_TYPE_CRYPTO_AUTH_CHALLENGE     = 18  // Server → Client: Challenge nonce
PACKET_TYPE_CRYPTO_AUTH_RESPONSE      = 19  // Client → Server: HMAC response
PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE = 20  // Server → Client: Success
PACKET_TYPE_CRYPTO_AUTH_FAILED        = 21  // Server → Client: Failure
PACKET_TYPE_CRYPTO_NO_ENCRYPTION      = 23  // Client → Server: Opt-out
PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP   = 24  // Server → Client: HMAC proof
```

**Why unencrypted?**
These packets establish the encryption keys - they cannot be encrypted with keys that don't exist yet. This is standard for all key exchange protocols (TLS, SSH, etc.).

#### Encrypted Packets (After Handshake)
```c
PACKET_TYPE_ENCRYPTED = 22  // Wrapper for all post-handshake packets
```

All application packets (video, audio, control) are wrapped in `PACKET_TYPE_ENCRYPTED` after successful handshake.

### Wire Format: Encrypted vs Unencrypted

#### Unencrypted Packet (Handshake)
```
[Header: 18 bytes][Payload: N bytes]
|                  |
magic=0xDEADBEEF   type-specific data
type=14-21
length=N
crc32=checksum
client_id=0
```

#### Encrypted Packet (Post-Handshake)
```
[Outer Header: 18 bytes][Encrypted Blob]
|                        |
magic=0xDEADBEEF         [Nonce: 24 bytes][Inner Header + Payload: N bytes][MAC: 16 bytes]
type=20                  |                 |                                  |
length=24+N+16          Random nonce       Real packet data                 Poly1305 tag
crc32=0
client_id=0
```

**Decryption process:**
1. Verify outer header (magic, type)
2. Extract nonce (first 24 bytes of encrypted blob)
3. Decrypt remaining blob with XSalsa20-Poly1305
4. Parse inner header from plaintext
5. Extract real payload

**Result:** An attacker sees:
- Outer packet magic (needed for framing)
- Fact that packet is encrypted
- Total encrypted size (nonce + ciphertext + MAC)

**Hidden from attacker:**
- Real packet type (video, audio, control?)
- Real payload size
- Client ID
- All payload data

---

## Handshake Protocol

### Sequence Diagram

```
Client                                    Server
  |                                         |
  |------ TCP Connect -------------------->|
  |                                         |
  |                                         | Generate ephemeral keypair
  |                                         | (or use --key loaded SSH key)
  |                                         |
  |<----- CRYPTO_KEY_EXCHANGE_INIT --------|
  |       [32-byte X25519 public key]      |
  |       [32-byte Ed25519 identity key]   | (if using SSH key)
  |       [64-byte Ed25519 signature]      | (signature of X25519 key)
  |                                         |
  | Verify signature (if present)          |
  | Check against --server-key (if set)    |
  | Check known_hosts (if exists)          |
  | Compute DH shared secret               |
  |                                         |
  |------ CRYPTO_KEY_EXCHANGE_RESP ------->|
  |       [32-byte X25519 public key]      |
  |       [32-byte Ed25519 identity key]   | (if using SSH key)
  |       [64-byte Ed25519 signature]      | (signature of X25519 key)
  |                                         |
  |                                         | Verify signature (if present)
  |                                         | Check whitelist (if enabled)
  |                                         | Compute DH shared secret
  |                                         |
  |<----- CRYPTO_AUTH_CHALLENGE -----------|
  |       [32-byte random nonce]           |
  |       [1-byte flags]                   | (password required? key required?)
  |                                         |
  | Compute HMAC(shared_secret, nonce)     |
  | If password: HMAC(password_key, nonce) |
  |                                         |
  |------ CRYPTO_AUTH_RESPONSE ----------->|
  |       [32-byte HMAC]                   |
  |                                         |
  |                                         | Verify HMAC
  |                                         | Check password (if required)
  |                                         | Check client key whitelist (if enabled)
  |                                         |
  |<----- CRYPTO_HANDSHAKE_COMPLETE -------|
  |                                         |
  | ✅ Encryption active                    | ✅ Encryption active
  |                                         |
  |<===== ENCRYPTED PACKETS ===============>|
  |       All future packets encrypted     |
```

### Handshake Phases Explained

#### Phase 1: Key Exchange Init (Server → Client)

**Server sends:**
```c
typedef struct {
  uint8_t ephemeral_public_key[32];  // X25519 DH public key (always)
  uint8_t identity_public_key[32];   // Ed25519 identity (if --key used)
  uint8_t signature[64];              // Ed25519 signature (if --key used)
} key_exchange_init_packet_t;
```

**Packet size:**
- **32 bytes**: Ephemeral mode (default)
- **128 bytes**: Authenticated mode (`--key` SSH key)

**Client verifies:**
1. **If `--server-key` provided:** Verify identity key matches expected key → ABORT if mismatch
2. **If signature present:** Verify `signature` is valid for `ephemeral_public_key` using `identity_public_key`
3. **Check known_hosts:** If server:port in `~/.ascii-chat/known_hosts`, verify identity key matches
4. **First connection:** Prompt user to save to known_hosts

**Security note:** The signature binds the ephemeral key to the long-term identity key, preventing an attacker from replacing the DH key while keeping the identity key.

#### Phase 2: Key Exchange Response (Client → Server)

**Client sends:**
```c
typedef struct {
  uint8_t ephemeral_public_key[32];  // X25519 DH public key (always)
  uint8_t identity_public_key[32];   // Ed25519 identity (if --key used)
  uint8_t signature[64];              // Ed25519 signature (if --key used)
} key_exchange_response_packet_t;
```

**Server verifies:**
1. **If `--client-keys` provided:** Check if `identity_public_key` is in whitelist → REJECT if not found
2. **If signature present:** Verify signature is valid
3. **Compute shared secret:** `shared_secret = X25519(server_private, client_public)`

**At this point both sides have:**
- ✅ Ephemeral DH shared secret (32 bytes)
- ✅ Peer's identity public key (if authenticated mode)

#### Phase 3: Authentication Challenge (Server → Client)

**Server sends:**
```c
typedef struct {
  uint8_t nonce[32];  // Random challenge nonce
  uint8_t flags;      // AUTH_REQUIRE_PASSWORD | AUTH_REQUIRE_CLIENT_KEY
} auth_challenge_packet_t;
```

**Flags:**
- `AUTH_REQUIRE_PASSWORD (0x01)`: Server has `--password`, client must prove knowledge
- `AUTH_REQUIRE_CLIENT_KEY (0x02)`: Server has `--client-keys`, client must be whitelisted

**Client prepares response:**
1. **If password set:** `HMAC(password_key, nonce)` using Argon2-derived key
2. **If no password:** `HMAC(shared_secret, nonce)` using DH shared secret
3. **If client has SSH key:** Also sign nonce with Ed25519 identity key

#### Phase 4: Authentication Response (Client → Server)

**Client sends:**
```c
typedef struct {
  uint8_t hmac[32];  // HMAC of challenge nonce
} auth_response_packet_t;
```

**Server verifies:**
1. **Recompute HMAC** using same key (password_key or shared_secret)
2. **Constant-time compare** with received HMAC → REJECT if mismatch
3. **If whitelist enabled:** Verify client's identity key is in `--client-keys`

#### Phase 5: Handshake Complete or Failed

**Success:**
```c
PACKET_TYPE_HANDSHAKE_COMPLETE  // Empty packet
```

**Failure:**
```c
PACKET_TYPE_AUTH_FAILED  // Empty packet
```

After `HANDSHAKE_COMPLETE`, both sides:
- ✅ Enable packet encryption using XSalsa20-Poly1305
- ✅ Use shared secret as encryption key
- ✅ Increment nonce counter for each packet

---

## Key Management

### Design Principle: Separation of Identity and Encryption

**Core architecture decision:** SSH keys are ONLY used for authentication (identity proof), NEVER for encryption.

**Why this matters:**

1. **Forward Secrecy is Critical**
   - If your SSH key is compromised **today**, an attacker should NOT be able to decrypt conversations from **last week**
   - Ephemeral keys provide this guarantee: each session has unique encryption keys that are destroyed after use
   - Using long-term keys for encryption breaks forward secrecy (recorded traffic can be decrypted later)

2. **Consistency Across Environments**
   - SSH agent availability varies by system (Unix-only, requires configuration)
   - Users should get **identical security** regardless of environment
   - Old design: SSH agent = forward secrecy, in-memory = no forward secrecy ❌
   - New design: Both modes = forward secrecy ✅

3. **Matches SSH Protocol Design**
   - SSH itself uses this exact model: long-term host/user keys for identity, ephemeral DH keys for encryption
   - Proven design with 30+ years of security analysis
   - No need to reinvent the wheel

**How it works:**

```
Handshake Protocol:
  1. Server generates ephemeral X25519 keypair (random, per-connection)
  2. Server signs ephemeral public key with long-term Ed25519 key
  3. Server sends: [ephemeral_pk][identity_pk][signature]
  4. Client verifies signature (proves server has identity key)
  5. Client uses ephemeral_pk for encryption (forward secrecy)
```

The signature **cryptographically binds** the ephemeral encryption key to the long-term identity key. This provides:
- ✅ Strong authentication (server must possess identity private key)
- ✅ Forward secrecy (ephemeral keys are destroyed after session)
- ✅ MITM protection (signature prevents key substitution attacks)

**Alternative considered and rejected:**

Using Ed25519→X25519 conversion for encryption would be simpler code-wise, but:
- ❌ Breaks forward secrecy (recorded traffic can be decrypted if key is compromised later)
- ❌ Inconsistent security (SSH agent mode would still need ephemeral keys)
- ❌ Deviates from SSH protocol best practices

**Result:** All modes use ephemeral encryption keys. SSH keys are authentication-only.

### SSH Ed25519 Keys

ASCII-Chat can use existing SSH Ed25519 keys for **authentication** (identity proof via signatures):

```bash
# Server: Use SSH private key for identity
ascii-chat-server --key ~/.ssh/id_ed25519

# Client: Verify server's SSH public key
ascii-chat-client --server-key ~/.ssh/server_id_ed25519.pub
```

**Key file formats supported:**
- OpenSSH native format (`BEGIN OPENSSH PRIVATE KEY`)
- OpenSSH public key format (`ssh-ed25519 AAAAC3...`)
- Encrypted private keys (prompts for passphrase)

**Ed25519 to X25519 conversion:**

ASCII-Chat can convert Ed25519 keys to X25519 format for compatibility, but **does NOT use the converted key for encryption**:

```c
// Public key: Ed25519 (signing) → X25519 (for compatibility only)
crypto_sign_ed25519_pk_to_curve25519(x25519_pk, ed25519_pk);

// Private key: Ed25519 (signing) → X25519 (NEVER used for encryption)
crypto_sign_ed25519_sk_to_curve25519(x25519_sk, ed25519_sk);
```

**Why this works:**
Both Ed25519 and X25519 use the same underlying curve (Curve25519). Ed25519 uses the Edwards form for signing, X25519 uses the Montgomery form for DH. libsodium provides safe conversion functions.

**Security architecture:**
- ✅ **SSH keys used for:** Identity authentication (Ed25519 signatures only)
- ✅ **Ephemeral keys used for:** Encryption (X25519 Diffie-Hellman)
- ✅ **Result:** Forward secrecy + strong authentication

The SSH key proves identity through signatures. The ephemeral keys provide encryption with forward secrecy. The signature binds them together cryptographically, preventing MITM attacks while maintaining forward secrecy.

### SSH Agent Integration

ASCII-Chat supports **SSH agent** for encrypted private keys, allowing password-free authentication when your SSH key is already loaded in the agent.

**How it works:**

When you provide an encrypted SSH key via `--key`, ASCII-Chat automatically checks if that specific key is available in the SSH agent:

```bash
# 1. Start SSH agent (if not already running)
eval "$(ssh-agent -s)"

# 2. Add your encrypted key to the agent (prompts for password ONCE)
ssh-add ~/.ssh/id_ed25519
Enter passphrase for /Users/you/.ssh/id_ed25519: [password]
Identity added: /Users/you/.ssh/id_ed25519

# 3. Start server - uses agent, NO password prompt!
ascii-chat-server --key ~/.ssh/id_ed25519
INFO: Using SSH agent for this key (agent signing + ephemeral encryption)
INFO: SSH agent mode: Will use agent for identity signing, ephemeral X25519 for encryption
```

**Key detection algorithm:**

```c
// lib/crypto/keys.c:823-844
// When parsing an encrypted private key:
if (is_encrypted) {
  // Extract the public key from the encrypted key file
  uint8_t embedded_public_key[32];

  // Check if THIS SPECIFIC key is in SSH agent (not just any Ed25519 key)
  bool ssh_agent_has_key = ssh_agent_has_specific_key(embedded_public_key);

  if (ssh_agent_has_key) {
    // Mode 1: SSH agent mode
    key_out->type = KEY_TYPE_ED25519;
    key_out->use_ssh_agent = true;
    memcpy(key_out->public_key, embedded_public_key, 32);

    // Use agent for identity signing, ephemeral X25519 for encryption
    return 0;
  } else {
    // Mode 2: Password prompt (agent doesn't have this key)
    // Prompts for passphrase and decrypts key
  }
}
```

**SSH agent signing protocol:**

When `use_ssh_agent = true`, all Ed25519 signatures are delegated to the SSH agent:

```c
// lib/crypto/keys.c:1318-1494
int ed25519_sign_message(const private_key_t *key, const uint8_t *message,
                         size_t message_len, uint8_t signature[64]) {
  if (key->use_ssh_agent) {
    // 1. Connect to SSH agent Unix socket ($SSH_AUTH_SOCK)
    int agent_fd = connect_to_agent();

    // 2. Build SSH_AGENTC_SIGN_REQUEST (type 13)
    //    [pubkey_blob][data_to_sign][flags]
    uint8_t request[...];
    send(agent_fd, request);

    // 3. Receive SSH_AGENT_SIGN_RESPONSE (type 14)
    //    [signature_blob: "ssh-ed25519" + 64-byte signature]
    uint8_t response[...];
    recv(agent_fd, response);

    // 4. Extract 64-byte Ed25519 signature
    memcpy(signature, response + offset, 64);
    return 0;
  } else {
    // Use in-memory Ed25519 key to sign
    crypto_sign_detached(signature, NULL, message, message_len, key->key.ed25519);
    return 0;
  }
}
```

**Security architecture:**

ASCII-Chat uses a **separation of concerns** design where SSH keys are ONLY used for authentication, never encryption:

| Component | SSH Agent Mode | In-Memory Mode |
|-----------|----------------|----------------|
| **Identity signing** | SSH agent (Ed25519) | In-memory Ed25519 |
| **Encryption keys** | Ephemeral X25519 | Ephemeral X25519 |
| **Private key storage** | None (agent-only) | Decrypted in memory |
| **Password required** | No (once in agent) | Yes (every restart) |
| **Forward secrecy** | ✅ YES | ✅ YES |

**Why always ephemeral encryption?**

Both modes use ephemeral X25519 keys for encryption to provide **forward secrecy**:

1. **Identity authentication:** SSH key proves identity via Ed25519 signature
2. **Encryption:** Ephemeral X25519 keys generated fresh per connection
3. **Cryptographic binding:** Signature covers ephemeral key, proving possession of both
4. **Forward secrecy:** If SSH key is compromised later, past sessions remain secure

This matches SSH protocol design: long-term keys for identity, ephemeral keys for encryption.

**Handshake signature binding:**

```c
// Server proves: "I possess identity_Ed25519 AND I'm using ephemeral_X25519"
Server sends: [ephemeral_X25519:32][identity_Ed25519:32][signature:64]
  where: signature = sign(identity_private_key, ephemeral_X25519)
```

The signature **cryptographically binds** the ephemeral encryption key to the long-term identity key, preventing MITM attacks while maintaining forward secrecy.

**Fallback behavior:**

```bash
# If key is encrypted but NOT in SSH agent:
ascii-chat-server --key ~/.ssh/id_ed25519
Encrypted private key detected (cipher: aes256-ctr)
Key not in SSH agent, will prompt for password
Enter passphrase for /Users/you/.ssh/id_ed25519: [password]
Successfully decrypted key, parsing...

# If SSH agent isn't running:
ascii-chat-server --key ~/.ssh/id_ed25519
ssh_agent_has_specific_key: SSH_AUTH_SOCK not set
Key not in SSH agent, will prompt for password
```

**Environment variable:**

SSH agent communication requires the `SSH_AUTH_SOCK` environment variable:

```bash
# Check if SSH agent is running
echo $SSH_AUTH_SOCK
/tmp/ssh-XXXXXX/agent.12345

# If not set, start agent:
eval "$(ssh-agent -s)"
```

**Security benefits:**

- ✅ **Password once per session** - Add key to agent once, use many times
- ✅ **No plaintext passwords** - Agent handles passphrase, applications never see it
- ✅ **Process isolation** - Private key never leaves agent process
- ✅ **Forward secrecy** - Ephemeral X25519 keys per connection
- ✅ **Transparent fallback** - Works with or without agent

**Limitations:**

- ❌ **Unix-only** - SSH agent uses Unix domain sockets (not available on Windows)
- ❌ **Signing only** - Cannot use agent for X25519 DH operations
- ❌ **Session-scoped** - Keys removed from agent on logout/reboot

**Implementation details:**

The specific key detection uses `ssh-add -L` to list all keys in the agent, then compares the embedded public key from the encrypted file against each agent key:

```c
// lib/crypto/keys.c:438-524
static bool ssh_agent_has_specific_key(const uint8_t ed25519_public_key[32]) {
  // 1. List all keys in agent
  FILE *fp = popen("ssh-add -L 2>/dev/null", "r");

  // 2. Parse each "ssh-ed25519 <base64> comment" line
  while (fgets(line, sizeof(line), fp)) {
    if (!strstr(line, "ssh-ed25519")) continue;

    // 3. Decode base64 to get SSH public key blob
    base64_decode_ssh_key(base64_buf, &decoded, &decoded_len);

    // 4. Extract 32-byte Ed25519 key (last 32 bytes of blob)
    uint8_t *agent_key = decoded + decoded_len - 32;

    // 5. Compare with our target key
    if (memcmp(agent_key, ed25519_public_key, 32) == 0) {
      return true;  // MATCH!
    }
  }

  return false;  // Not found in agent
}
```

**Code locations:**
- `lib/crypto/keys.h:37` - `bool use_ssh_agent` flag in `private_key_t`
- `lib/crypto/keys.c:438-524` - `ssh_agent_has_specific_key()` detection
- `lib/crypto/keys.c:820-844` - Encrypted key parsing with agent check
- `lib/crypto/keys.c:502-690` - `ed25519_sign_message()` with agent protocol
- `lib/crypto/keys.c:1546-1580` - `crypto_setup_ssh_key_for_handshake()` architecture

**Security guarantee:**
Both SSH agent mode and in-memory mode provide **identical forward secrecy** - ephemeral X25519 keys are used for encryption in all cases. The only difference is where signatures come from (agent vs in-memory).

### Automatic SSH Agent Key Addition (New in 2025)

**Problem:** Users with encrypted SSH keys had to manually add keys to ssh-agent, or enter their password repeatedly.

**Solution:** ASCII-Chat now **automatically adds decrypted keys to ssh-agent** after successful password entry, eliminating future password prompts.

**How it works:**

```bash
# First run with encrypted key (password required)
ascii-chat-server --key ~/.ssh/id_ed25519
Encrypted private key detected (cipher: aes256-ctr)
Key not in SSH agent, will prompt for password
Enter passphrase for /Users/you/.ssh/id_ed25519: [enter password]
Successfully decrypted key, parsing...
[SSH Agent] Adding key to ssh-agent to avoid future password prompts...
[SSH Agent] ✓ Key successfully added to ssh-agent
INFO: Key added to ssh-agent - password won't be required again this session

# Second run (NO password prompt!)
ascii-chat-server --key ~/.ssh/id_ed25519
INFO: Using SSH agent for this key (agent signing + ephemeral encryption)
# Server starts immediately - no password needed!
```

**What happens automatically:**

1. **User enters password** - Decrypts SSH key successfully
2. **Key parsed** - Validates Ed25519 format
3. **Check ssh-agent** - Verifies `$SSH_AUTH_SOCK` is set
4. **Auto-add to agent** - Shells out to `ssh-add` with password via `SSH_ASKPASS`
5. **Future runs** - No password prompt (uses agent)

**Implementation details:**

The auto-add feature uses a temporary `SSH_ASKPASS` script to provide the password non-interactively:

```c
// lib/crypto/keys.c:1047-1090
if (ssh_agent_is_available()) {
  // 1. Create temporary askpass script with user's password
  char askpass_script[512];
  snprintf(askpass_script, sizeof(askpass_script), "/tmp/ascii-chat-askpass-%d.sh", getpid());

  FILE *askpass_fp = fopen(askpass_script, "w");
  fprintf(askpass_fp, "#!/bin/sh\necho '%s'\n", passphrase);
  fclose(askpass_fp);
  chmod(askpass_script, 0700);

  // 2. Shell out to ssh-add with SSH_ASKPASS
  snprintf(ssh_add_cmd, sizeof(ssh_add_cmd),
           "SSH_ASKPASS='%s' SSH_ASKPASS_REQUIRE=force ssh-add '%s' 2>&1",
           askpass_script, path);

  FILE *ssh_add_fp = popen(ssh_add_cmd, "r");
  // ... check for "Identity added" message ...

  // 3. Securely delete temporary script
  unlink(askpass_script);
}
```

**Security considerations:**

- ✅ **Temporary script deleted immediately** - Passphrase file removed after use
- ✅ **Restrictive permissions** - Script created with `chmod 0700` (owner-only)
- ✅ **Process-scoped** - Uses PID in filename to avoid conflicts
- ✅ **Memory zeroed** - Password cleared with `sodium_memzero()` after use
- ✅ **Agent session-scoped** - Key removed from agent on logout/reboot

**Dependencies:**

This feature requires **external commands** that must be in your `$PATH`:

| Command | Purpose | Usually found in |
|---------|---------|------------------|
| `ssh-add` | Add keys to agent | OpenSSH client package |
| `ssh-agent` | Key management daemon | OpenSSH client package (usually auto-started) |

**Platform support:**

| Platform | Supported | Notes |
|----------|-----------|-------|
| **Linux** | ✅ YES | Requires OpenSSH client (`openssh-client`, `openssh`) |
| **macOS** | ✅ YES | Built-in with macOS (ssh-agent auto-starts) |
| **Windows** | ❌ NO | SSH agent uses Unix domain sockets (incompatible with Windows) |

**Windows users:**

On Windows, you'll need to enter your password each time:

```powershell
# Windows behavior (no ssh-agent support)
./ascii-chat-server.exe --key $env:USERPROFILE\.ssh\id_ed25519
Encrypted private key detected (cipher: aes256-ctr)
Key not in SSH agent, will prompt for password
Enter passphrase for C:\Users\you\.ssh\id_ed25519: [password required every time]
```

**Workaround for Windows users:**

1. **Use unencrypted keys** (not recommended for security):
   ```powershell
   ssh-keygen -p -f $env:USERPROFILE\.ssh\id_ed25519 -N ""
   ```

2. **Use password authentication** instead of SSH keys:
   ```powershell
   ./ascii-chat-server.exe --password "your-shared-password"
   ```

3. **Wait for Windows SSH agent support** (future enhancement - see issue #TBD)

**Troubleshooting:**

**Problem:** Auto-add fails with "ssh-add: command not found"

```bash
# Check if ssh-add is installed
which ssh-add
# If not found:
# Ubuntu/Debian: sudo apt-get install openssh-client
# Fedora/RHEL: sudo dnf install openssh-clients
# Arch: sudo pacman -S openssh
# macOS: Built-in (shouldn't fail)
```

**Problem:** Auto-add fails with "Could not add key to ssh-agent"

```bash
# Check if ssh-agent is running
echo $SSH_AUTH_SOCK
# If empty, start agent:
eval "$(ssh-agent -s)"
```

**Problem:** Password still required after auto-add

```bash
# Check if key was actually added
ssh-add -l
# Should show: "256 SHA256:... /path/to/id_ed25519 (ED25519)"

# If not listed, check permissions
ls -la ~/.ssh/id_ed25519
# Should be: -rw------- (600)
```

**Future enhancements:**

- Windows SSH agent support via Named Pipes (tracked in issue #TBD)
- Integration with Windows OpenSSH service (`ssh-agent.exe`)
- Cross-platform passphrase caching (memory-only, no disk storage)

**Code locations:**

- `lib/crypto/keys.c:1047-1090` - Auto-add implementation after successful decryption
- `lib/crypto/ssh_agent.h` - `ssh_agent_is_available()` declaration
- `lib/crypto/ssh_agent.c` - SSH agent detection and communication
- `CMakeLists.txt:1344` - `ssh_agent.c` added to build

### GitHub/GitLab Key Fetching

**Fetch SSH keys from public profiles:**

```bash
# Server: Use your GitHub SSH key
ascii-chat-server --key github:zfogg

# Client: Verify server using GitHub profile
ascii-chat-client --server-key github:zfogg
```

**How it works:**
1. **HTTPS fetch:** `GET https://github.com/zfogg.keys` (using BearSSL)
2. **Parse response:** Extract `ssh-ed25519 AAAAC3NzaC1lZDI1NTE5...` lines
3. **Filter:** Only accept Ed25519 keys (RSA/ECDSA rejected)
4. **Use first key:** If multiple Ed25519 keys, use the first one

**Security properties:**
- ✅ TLS-protected fetch (BearSSL verifies GitHub's certificate)
- ✅ Public keys are not secret (safe to fetch over network)
- ✅ Social proof: Attacker must compromise GitHub account
- ⚠️ Trust GitHub's key infrastructure

**GitLab support:** Same mechanism, `https://gitlab.com/username.keys`

### GPG Key Support (Optional)

**If GPG is installed**, ASCII-Chat can use GPG keys:

```bash
# Use GPG key by ID
ascii-chat-server --key gpg:0xABCD1234

# Use GPG key by fingerprint
ascii-chat-server --key gpg:1234567890ABCDEF1234567890ABCDEF12345678
```

**How it works:**
1. **Check GPG availability:** `gpg --version` (fail gracefully if not found)
2. **Export key:** `gpg --export KEYID` (prompts for passphrase via pinentry)
3. **Hash to X25519:** `BLAKE2b(gpg_export_output, 32)` → 32-byte X25519 key

**Why hash instead of convert?**
GPG keys can be RSA, DSA, or ECC (not necessarily Ed25519). Hashing provides a deterministic X25519 key from any GPG key material.

**Graceful fallback:**
```
ERROR: GPG key requested but 'gpg' command not found
Install GPG:
  Ubuntu/Debian: apt-get install gnupg
  macOS: brew install gnupg
  Arch: pacman -S gnupg
Or use SSH keys: --key ~/.ssh/id_ed25519
```

### Known Hosts (IP-Based TOFU)

**File location:** `~/.ascii-chat/known_hosts`

**Format:**
```
# ASCII-Chat Known Hosts
# Format: IP:port x25519 <hex-key> [comment]
# IPv4 example:
192.168.1.100:27224 x25519 a1b2c3d4e5f6... homeserver
10.0.0.50:8080 x25519 1234567890ab... office-server

# IPv6 example (bracket notation):
[2001:db8::1]:27224 x25519 fedcba098765... ipv6-server
[::1]:27224 x25519 abcdef123456... localhost-ipv6
[::ffff:192.0.2.1]:8080 x25519 9876543210fe... ipv4-mapped
```

**Security Design: IP Binding (Not Hostnames)**

ASCII-Chat binds server keys to **resolved IP addresses**, not DNS hostnames, for critical security reasons:

**Why IP addresses?**
1. **DNS Hijacking Prevention:**
   - DNS responses can be spoofed or hijacked
   - Attacker points `example.com` → malicious server IP
   - With hostname binding: Attacker's server key accepted as `example.com`'s key ❌
   - With IP binding: Client connects to attacker's IP, which won't match trusted IP ✅

2. **Cryptographic Binding:**
   - TCP connection is to a specific IP address, not a hostname
   - Hostname is resolved once via `getaddrinfo()`, then IP is used
   - Binding key to IP matches actual network connection

3. **IPv6 Support:**
   - Dual-stack servers accept IPv4 (`192.0.2.1`) and IPv6 (`2001:db8::1`)
   - Each IP:port combination gets its own key binding
   - Bracket notation `[::1]:8080` clearly distinguishes IPv6

**Example attack scenario (hostname binding):**
```
1. User connects to example.com:27224
2. DNS resolves to 203.0.113.50 (attacker-controlled)
3. Attacker's server presents key_A
4. Client saves: "example.com:27224 → key_A"
5. Later, DNS changes to 198.51.100.25 (legitimate server)
6. Legitimate server presents key_B
7. Client sees hostname match, ACCEPTS key_B
8. No MITM detection! ❌
```

**With IP binding (current implementation):**
```
1. User connects to example.com:27224
2. DNS resolves to 203.0.113.50
3. Attacker's server presents key_A
4. Client saves: "203.0.113.50:27224 → key_A"
5. Later, example.com resolves to 198.51.100.25
6. Different IP! No key stored, prompts user
7. User realizes IP changed, investigates
8. MITM detected! ✅
```

**Behavior:**
1. **First connection:** If server IP:port not in known_hosts, prompt user:
   ```
   The authenticity of host '192.168.1.100:27224' can't be established.
   Ed25519 key fingerprint is: SHA256:abc123...
   Are you sure you want to continue connecting (yes/no)? yes
   ```

2. **IPv6 first connection:**
   ```
   The authenticity of host '[2001:db8::1]:27224' can't be established.
   Ed25519 key fingerprint is: SHA256:def456...
   Are you sure you want to continue connecting (yes/no)? yes
   ```

3. **Subsequent connections:** Verify server key matches stored key → ABORT if mismatch

4. **Key change detected:**
   ```
   @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
   @    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @
   @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
   IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!

   Connection ABORTED for security.
   To remove old key: sed -i '/192.168.1.100:27224 /d' ~/.ascii-chat/known_hosts
   ```

**Trade-offs:**

| Aspect | IP Binding (Current) | Hostname Binding (SSH-style) |
|--------|---------------------|------------------------------|
| **DNS Hijacking** | ✅ Protected | ❌ Vulnerable |
| **Server IP Change** | ⚠️ Prompts user (manual verification) | ✅ Transparent |
| **Dynamic DNS** | ⚠️ New prompt per IP | ✅ Works seamlessly |
| **Multi-homed Servers** | ⚠️ Separate entry per IP | ✅ Single entry |
| **Security Model** | Paranoid (explicit trust) | Convenience (implicit trust) |

**Recommendation:** The security benefit of IP binding outweighs the inconvenience of re-verification when server IPs change. For production servers, IP addresses should be stable (static IPs or fixed cloud instances).

**Security model:**
"Trust on first use" (TOFU) with IP binding - assumes first connection to specific IP:port is legitimate, detects any changes in either IP or key thereafter.

---

## Authentication Modes

### Mode 1: Default (Ephemeral DH Only)

**Server:**
```bash
ascii-chat-server
```

**Client:**
```bash
ascii-chat-client
```

**Security:**
- ✅ Privacy: All packets encrypted
- ✅ Forward secrecy: New keys per connection
- ❌ MITM vulnerable: No identity verification

**Use case:** Quick sessions, low-threat environments

### Mode 2: Password Authentication

**Server:**
```bash
ascii-chat-server --password mySecretPass123
```

**Client:**
```bash
ascii-chat-client --password mySecretPass123
```

**Security:**
- ✅ Privacy: All packets encrypted
- ✅ MITM protection: Attacker must know password
- ✅ Forward secrecy: DH keys still ephemeral
- ⚠️ Password strength: Security depends on password quality

**Password derivation:**
```c
// Argon2id parameters
uint8_t salt[32];  // Random, generated once
uint8_t key[32];   // Derived key
randombytes_buf(salt, sizeof(salt));

crypto_pwhash(
  key, sizeof(key),
  password, strlen(password),
  salt,
  crypto_pwhash_OPSLIMIT_INTERACTIVE,  // 2 iterations
  crypto_pwhash_MEMLIMIT_INTERACTIVE,  // 64 MB
  crypto_pwhash_ALG_ARGON2ID13
);
```

**HMAC challenge/response:**
```c
// Server generates challenge
uint8_t nonce[32];
randombytes_buf(nonce, sizeof(nonce));

// Client computes response
uint8_t hmac[32];
crypto_auth(hmac, nonce, sizeof(nonce), password_key);

// Server verifies
crypto_auth_verify(hmac, nonce, sizeof(nonce), password_key);
```

### Mode 3: SSH Key Pinning

**Server:**
```bash
ascii-chat-server --key ~/.ssh/id_ed25519
```

**Client (verify using GitHub):**
```bash
ascii-chat-client --server-key github:zfogg
```

**Security:**
- ✅ Privacy: All packets encrypted
- ✅ MITM protection: Cryptographically verified identity
- ✅ Forward secrecy: DH keys still ephemeral
- ✅ No shared secret: Public keys can be shared openly

**Verification flow:**
1. Server sends Ed25519 identity key + signature
2. Client fetches expected key from GitHub
3. Client verifies signature: `ed25519_verify(signature, ephemeral_key, identity_key)`
4. Client proceeds only if signature valid

### Mode 4: Client Whitelisting

**Server:**
```bash
ascii-chat-server --client-keys ~/.ssh/authorized_keys
```

**Client:**
```bash
# Client displays their public key on startup
ascii-chat-client

# Output:
# Client public key: ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo... alice@laptop
# Share this key with the server operator to be whitelisted
```

**Server's authorized_keys format:**
```
# ASCII-Chat Authorized Keys
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo... alice@laptop
ssh-ed25519 AAAAB3NzaC1yc2EAAAADAQABAAABAQC... bob@desktop
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBar... carol@phone
```

**Security:**
- ✅ Access control: Only pre-approved clients connect
- ✅ Audit trail: Server logs all connection attempts with client keys
- ✅ Revocable: Remove key from file to revoke access

### Mode 5: Defense in Depth (All Features)

**Server:**
```bash
ascii-chat-server \
  --key ~/.ssh/id_ed25519 \
  --password myPass123 \
  --client-keys ~/.ssh/authorized_keys
```

**Client:**
```bash
ascii-chat-client \
  --password myPass123 \
  --server-key github:zfogg
```

**Security:**
- ✅ Password authentication (both sides verify password)
- ✅ SSH key pinning (client verifies server identity)
- ✅ Client whitelist (server only accepts known clients)
- ✅ Forward secrecy (ephemeral DH keys)
- ✅ Defense in depth (multiple layers)

### Mode 6: Opt-Out (No Encryption)

**Server:**
```bash
ascii-chat-server --no-encrypt
```

**Client:**
```bash
ascii-chat-client --no-encrypt
```

**Security:**
- ❌ No protection whatsoever
- ⚠️ All packets sent in plaintext

**Use case:** Debugging, packet inspection with tcpdump

**WARNING:** Only use in trusted networks or for development!

---

## Packet Encryption

### Encryption Process

**For each packet after handshake:**

```c
// 1. Construct inner packet (real type, real payload)
packet_header_t inner_header = {
  .magic = htonl(PACKET_MAGIC),
  .type = htons(PACKET_TYPE_ASCII_FRAME),  // Real type
  .length = htonl(payload_len),
  .crc32 = htonl(crc32(payload, payload_len)),
  .client_id = htonl(client_id)
};

// 2. Combine inner header + payload
uint8_t plaintext[sizeof(inner_header) + payload_len];
memcpy(plaintext, &inner_header, sizeof(inner_header));
memcpy(plaintext + sizeof(inner_header), payload, payload_len);

// 3. Generate nonce (counter-based, never reused)
uint8_t nonce[24];
memset(nonce, 0, 24);
*(uint64_t*)nonce = htole64(nonce_counter++);  // Increment counter

// 4. Encrypt with XSalsa20-Poly1305
uint8_t ciphertext[crypto_secretbox_MACBYTES + sizeof(plaintext)];
crypto_secretbox_easy(
  ciphertext,
  plaintext, sizeof(plaintext),
  nonce,
  shared_secret  // 32-byte key from DH
);

// 5. Build encrypted packet: [nonce][ciphertext][MAC]
uint8_t encrypted_blob[24 + sizeof(ciphertext)];
memcpy(encrypted_blob, nonce, 24);
memcpy(encrypted_blob + 24, ciphertext, sizeof(ciphertext));

// 6. Send outer packet (type=ENCRYPTED)
packet_header_t outer_header = {
  .magic = htonl(PACKET_MAGIC),
  .type = htons(PACKET_TYPE_ENCRYPTED),
  .length = htonl(sizeof(encrypted_blob)),
  .crc32 = 0,  // Not used for encrypted packets
  .client_id = 0
};
send_packet(socket, &outer_header, encrypted_blob);
```

**Wire format:**
```
[Outer Header: 18 bytes]
  magic: 0xDEADBEEF
  type: 20 (ENCRYPTED)
  length: 24 + N + 16

[Encrypted Blob: 24 + N + 16 bytes]
  [Nonce: 24 bytes] ← Random nonce for this packet
  [Ciphertext: N bytes] ← XSalsa20(plaintext)
  [MAC: 16 bytes] ← Poly1305(ciphertext)
```

### Decryption Process

```c
// 1. Receive outer packet
packet_header_t outer_header;
recv(socket, &outer_header, sizeof(outer_header));

// Verify outer header
if (ntohl(outer_header.magic) != PACKET_MAGIC) return -1;
if (ntohs(outer_header.type) != PACKET_TYPE_ENCRYPTED) return -1;

// 2. Read encrypted blob
uint32_t blob_len = ntohl(outer_header.length);
uint8_t encrypted_blob[blob_len];
recv(socket, encrypted_blob, blob_len);

// 3. Extract nonce and ciphertext
uint8_t nonce[24];
memcpy(nonce, encrypted_blob, 24);

uint8_t *ciphertext = encrypted_blob + 24;
size_t ciphertext_len = blob_len - 24;

// 4. Decrypt with XSalsa20-Poly1305
uint8_t plaintext[ciphertext_len - crypto_secretbox_MACBYTES];
if (crypto_secretbox_open_easy(
      plaintext,
      ciphertext, ciphertext_len,
      nonce,
      shared_secret) != 0) {
  // MAC verification failed - packet was tampered with!
  return -1;
}

// 5. Parse inner header
packet_header_t *inner_header = (packet_header_t*)plaintext;
uint16_t real_type = ntohs(inner_header->type);
uint32_t payload_len = ntohl(inner_header->length);

// 6. Extract payload
uint8_t *payload = plaintext + sizeof(packet_header_t);

// 7. Process packet based on real type
handle_packet(real_type, payload, payload_len);
```

### Nonce Management

**Critical security requirement:** Never reuse a nonce with the same key!

**Implementation:**
```c
// Per-connection nonce counter (starts at 1)
uint64_t nonce_counter = 1;

// Generate nonce for each packet
uint8_t nonce[24];
memset(nonce, 0, 24);
*(uint64_t*)nonce = htole64(nonce_counter++);
```

**Why this is safe:**
- 24-byte nonce = 192 bits
- Using 64 bits for counter = 2^64 = 18 quintillion packets
- At 60 FPS video, this lasts 9.7 trillion years
- Remaining 128 bits are zero (could be random, but counter alone is sufficient)

**Replay protection:**
- Each side maintains their own send counter
- Received packets are not checked for sequence (UDP-like behavior)
- Poly1305 MAC prevents tampering
- Application-level sequence numbers in packet headers (not crypto-related)

---

## Security Considerations

### Cryptographic Strengths

✅ **Modern primitives:** X25519, XSalsa20-Poly1305, Argon2id (current best practices)
✅ **Forward secrecy:** Ephemeral DH keys per connection (compromising one session doesn't affect others)
✅ **Authenticated encryption:** XSalsa20-Poly1305 AEAD prevents tampering
✅ **Memory-hard passwords:** Argon2id resistant to GPU brute-force
✅ **Constant-time crypto:** libsodium uses constant-time implementations (timing attack resistant)
✅ **Large nonce space:** 192-bit nonces make collision astronomically unlikely

### Potential Weaknesses

⚠️ **Default MITM vulnerability:**
- **Threat:** Attacker intercepts initial handshake, performs two separate DH exchanges
- **Mitigation:** Use `--server-key` or known_hosts verification
- **Acceptable because:** User is warned, optional verification available

⚠️ **Password quality:**
- **Threat:** Weak passwords vulnerable to offline dictionary attacks
- **Mitigation:** Argon2id makes attacks expensive, but cannot prevent weak passwords
- **Best practice:** Use long, random passwords (20+ characters)

⚠️ **SSH key security:**
- **Threat:** Compromised SSH private key = attacker can impersonate
- **Mitigation:** Encrypt SSH keys with strong passphrases, use `ssh-agent`
- **Note:** This is the same trust model as SSH itself

⚠️ **GitHub/GitLab trust:**
- **Threat:** Attacker compromises GitHub account → can serve malicious key
- **Mitigation:** Use 2FA on GitHub/GitLab, verify key fingerprints manually
- **Note:** Same trust model as git operations over SSH

⚠️ **No quantum resistance:**
- **Threat:** Large quantum computers could break X25519 in the future
- **Timeline:** Not a practical threat as of 2025
- **Future:** Post-quantum algorithms (Kyber, Dilithium) may be added later

### Not Vulnerable To

✅ **Passive eavesdropping:** All packets encrypted (attacker sees only outer headers)
✅ **Packet tampering:** Poly1305 MAC detects modifications
✅ **Replay attacks:** Nonce counter prevents old packets from being re-sent
✅ **Timing attacks:** libsodium uses constant-time implementations
✅ **Memory disclosure:** libsodium securely wipes keys on cleanup

---

## Known Vulnerabilities

### CVE-None: Default MITM Vulnerability (By Design)

**Severity:** Medium (mitigated by user choice)

**Description:**
By default, ASCII-Chat does not verify server identity. An attacker who controls the network can intercept the initial handshake and perform a man-in-the-middle attack.

**Attack scenario:**
1. Client attempts to connect to server at 192.168.1.100:27224
2. Attacker intercepts connection, poses as server to client
3. Attacker poses as client to real server
4. Attacker decrypts client packets, re-encrypts with server's key
5. Result: Attacker sees all traffic

**Why this is not a critical bug:**
1. **Informed choice:** This is the default because there's no global CA system
2. **User can verify:** `--server-key`, `--password`, or known_hosts provide protection
3. **Active attack required:** Attacker must control network (harder than passive eavesdropping)
4. **Similar to SSH:** First SSH connection has same vulnerability (known_hosts helps thereafter)

**Mitigation:**
```bash
# Server: Use SSH key for identity
ascii-chat-server --key ~/.ssh/id_ed25519

# Client: Verify server (pick one)
ascii-chat-client --server-key github:zfogg  # Fetch from GitHub
ascii-chat-client --server-key ~/.ssh/server.pub  # Manual verification
ascii-chat-client  # Will prompt to save to known_hosts
```

### Potential Bug: Nonce Counter Overflow

**Severity:** Low (practically impossible)

**Description:**
If a single connection sends more than 2^64 packets, the nonce counter wraps to zero, potentially reusing nonces.

**Attack scenario:**
1. Keep connection alive for years
2. Send packets at maximum rate (10,000/sec) for ~58 million years
3. Nonce counter wraps to zero
4. Nonces start repeating

**Likelihood:** Astronomically low (would require 58 million years of continuous packets)

**Impact:** Nonce reuse could allow an attacker to recover plaintext of two packets with the same nonce

**Mitigation:** Reconnect periodically (every 24 hours is more than sufficient)

**Code fix (if needed):**
```c
if (nonce_counter == UINT64_MAX) {
  // Force reconnection before counter wraps
  log_error("Nonce counter exhausted - disconnecting for safety");
  connection_close();
  return -1;
}
```

### Potential Bug: SSH Key Signature Bypass

**Severity:** High (if implementation bug exists)

**Description:**
If signature verification is skipped when `--server-key` is set, attacker could send any identity key.

**Attack scenario:**
1. Client connects with `--server-key github:zfogg`
2. Attacker sends their own Ed25519 key in handshake
3. **BUG:** Client doesn't verify signature of ephemeral key
4. Client accepts attacker's identity key as valid

**Current status:** ✅ Not vulnerable (signature verification is mandatory)

**Code to check:**
```c
// In crypto_handshake_client_key_exchange():
if (identity_key_present) {
  // MUST verify signature before proceeding
  if (ed25519_verify_signature(identity_key, ephemeral_key, signature) != 0) {
    log_error("Invalid signature - server identity could not be verified");
    return -1;  // ABORT connection
  }
}
```

**Test case:** Attempt connection with invalid signature → should be rejected

---

## Critical Security Review (Third-Party Analysis)

> **Note:** This section documents security issues identified during independent review of the cryptographic implementation. These represent real vulnerabilities that should be addressed before production use.

### Issue 1: Password Mode MITM Vulnerability ⚠️ **CRITICAL**

**Severity:** Critical (actively exploitable)

**Description:**
Password mode was vulnerable to MITM attacks despite using encryption. An attacker could intercept the key exchange and derive the password-based key themselves because the password HMAC wasn't bound to the DH shared_secret.

**Previous attack scenario:**
```
Client                    Attacker                    Server
   |                         |                           |
   |--KEY_EXCHANGE_INIT----->|                           |
   |                         |----KEY_EXCHANGE_INIT----->|
   |                         |<---server_pubkey----------|
   |<---attacker_pubkey------|                           |
   |                         |                           |
   | Computes: HMAC(password_key, nonce)                 |
   |                         | Attacker can compute same HMAC with password
   |                         | Even though DH secrets differ!
```

**Root cause:**
- Password HMAC was computed as: `HMAC(password_key, nonce)`
- This didn't bind the password to the DH exchange
- Attacker who knows the password can MITM the DH exchange and still pass authentication

**Status:** 🟢 **FIXED** - Password HMACs now bound to DH shared_secret

**Implementation:**
All password HMAC computations now bind to the DH shared_secret to prevent MITM:

```c
// Client and server now both compute (for AUTH_RESPONSE):
uint8_t combined_data[64];  // nonce || shared_secret
memcpy(combined_data, nonce, 32);
memcpy(combined_data + 32, shared_secret, 32);
uint8_t hmac[32];
crypto_compute_hmac_ex(password_key, combined_data, 64, hmac);

// Server verifies (for SERVER_AUTH_RESPONSE):
uint8_t combined_data[64];  // client_nonce || shared_secret
memcpy(combined_data, client_challenge_nonce, 32);
memcpy(combined_data + 32, shared_secret, 32);
uint8_t server_hmac[32];
crypto_compute_hmac_ex(password_key, combined_data, 64, server_hmac);
```

**Why this fixes the MITM vulnerability:**
- Client computes: `HMAC(password_key, nonce || DH_secret_A)`
- Attacker with different DH secret computes: `HMAC(password_key, nonce || DH_secret_B)`
- Server expects: `HMAC(password_key, nonce || DH_secret_A)`
- Attacker's HMAC doesn't match → authentication fails

**Code locations:**
- `lib/crypto/crypto.h:168-172` - Added `crypto_compute_hmac_ex()` and `crypto_verify_hmac_ex()` for variable-length HMAC
- `lib/crypto/crypto.c:610-648` - Implemented extended HMAC functions
- `lib/crypto/handshake.c:569-582` - Client AUTH_RESPONSE binds to shared_secret
- `lib/crypto/handshake.c:644-657` - Client optional AUTH_RESPONSE binds to shared_secret
- `lib/crypto/handshake.c:706-719` - Client no-server-requirement AUTH_RESPONSE binds to shared_secret
- `lib/crypto/handshake.c:829-841` - Client SERVER_AUTH_RESPONSE verification binds to shared_secret
- `lib/crypto/handshake.c:890-902` - Server AUTH_RESPONSE verification binds to shared_secret
- `lib/crypto/handshake.c:962-974` - Server SERVER_AUTH_RESPONSE computation binds to shared_secret

**Impact:** Critical vulnerability fixed - Password mode now provides true MITM protection, not just passive eavesdropping protection

---

### Issue 2: No Mutual Authentication in Default Mode ⚠️ **HIGH**

**Severity:** High (design flaw)

**Description:**
The challenge-response protocol only authenticates the client to the server, not the other way around. Server never proves it has the shared secret.

**Previous handshake flow:**
```
Server                          Client
  |----KEY_EXCHANGE_INIT--------->|
  |<---KEY_EXCHANGE_RESPONSE------|
  |                                |
  | Derives: shared_secret         | Derives: shared_secret
  |                                |
  |----AUTH_CHALLENGE: nonce----->|
  |<---AUTH_RESPONSE: HMAC--------|
  |                                |
  | Verifies HMAC ✓                | Hopes server has key (no verification)
  |----HANDSHAKE_COMPLETE-------->|
```

**Problem:** Client never receives proof that server has the correct shared secret. A MITM attacker could:
1. Intercept client's DH pubkey
2. Generate their own server DH pubkey
3. Send AUTH_CHALLENGE to client
4. Client responds with HMAC (client is now authenticated)
5. Attacker sends HANDSHAKE_COMPLETE (without ever proving they have the shared secret)

**Status:** 🟢 **FIXED** - Mutual authentication implemented

**Implementation:**
The protocol now includes bidirectional challenge-response:

```c
// New handshake flow with mutual authentication:
// 1. Server → Client: AUTH_CHALLENGE (server_nonce)
// 2. Client generates client_nonce
// 3. Client → Server: AUTH_RESPONSE (HMAC + client_nonce)
//    - Password mode: HMAC(32) + client_nonce(32) = 64 bytes
//    - Ed25519 mode: signature(64) + client_nonce(32) = 96 bytes
// 4. Server verifies client's HMAC/signature
// 5. Server computes HMAC(shared_secret, client_nonce)
// 6. Server → Client: AUTH_CONFIRM (server's HMAC of client_nonce)
// 7. Client verifies server's HMAC
// Both sides now authenticated
```

**New packet type:**
```c
PACKET_TYPE_SERVER_AUTH_RESPONSE = 22  // Server → Client: HMAC(32 bytes)
```

**Code locations:**
- `lib/network.h:97` - Added PACKET_TYPE_SERVER_AUTH_RESPONSE
- `lib/crypto/handshake.c:593` - Client sends challenge nonce
- `lib/crypto/handshake.c:946` - Server sends AUTH_CONFIRM
- `lib/crypto/handshake.c:834` - Client verifies server's HMAC

**Impact:** High - Previously allowed MITM without server authentication, now both sides prove knowledge of shared secret

---

### Issue 3: Replay Vulnerability Across Sessions ⚠️ **MEDIUM**

**Severity:** Medium (limited exploitation window)

**Description:**
Nonce counter resets to 0 on each connection. An attacker who records packets from Session 1 can replay them into Session 2 if the same shared secret is used.

**Attack scenario:**
```
Session 1:
  Client sends: encrypt(nonce=0, "start stream")
  Client sends: encrypt(nonce=1, "video frame 1")
  Attacker records these packets

Session 2 (client reconnects):
  Nonce counter resets to 0
  Attacker replays: encrypt(nonce=0, "start stream")  ← Accepted!
  Attacker replays: encrypt(nonce=1, "video frame 1") ← Accepted!
```

**Root cause:**
```c
// crypto_context_init() in lib/crypto/crypto.c
ctx->send_nonce_counter = 1;  // Resets to 1 every connection!
```

**Fix required:**
Include connection-specific data in nonce derivation:

```c
// Option A: Random session ID
uint8_t session_id[16];
randombytes_buf(session_id, sizeof(session_id));

// Nonce format: [session_id (16 bytes)][counter (8 bytes)]
uint8_t nonce[24];
memcpy(nonce, session_id, 16);
*(uint64_t*)(nonce + 16) = htole64(counter++);
```

**Or:**
```c
// Option B: Timestamp-based nonce
uint64_t session_timestamp = time(NULL);

// Nonce format: [timestamp (8 bytes)][counter (8 bytes)][zeros (8 bytes)]
uint8_t nonce[24];
*(uint64_t*)nonce = htole64(session_timestamp);
*(uint64_t*)(nonce + 8) = htole64(counter++);
```

**Impact:** Medium - Attacker can replay old packets into new sessions, but needs network position and recorded traffic

**Status:** 🟢 **FIXED** - Session IDs added to nonce generation

**Implementation:**
```c
// Each connection gets a unique random session ID
randombytes_buf(ctx->session_id, 16);

// Nonce format: [session_id (16 bytes)][counter (8 bytes)]
SAFE_MEMCPY(nonce_out, 16, ctx->session_id, 16);
uint64_t counter = ctx->nonce_counter++;
SAFE_MEMCPY(nonce_out + 16, 8, &counter, 8);
```

This ensures nonces are unique across sessions even if counters reset.

---

### Issue 4: Whitelist Has No Revocation Mechanism ⚠️ **LOW**

**Severity:** Informational (not a security issue)

**Description:**
If a client's SSH key is compromised, the server operator must manually edit the whitelist file and restart the server to revoke access.

**Current behavior:**
```c
// Server loads whitelist at startup
public_key_t *whitelist = load_whitelist("authorized_clients.txt");

// Whitelist remains in memory until server restart
```

**Why hot-reload is not needed:**

**ASCII-Chat's security model does not require hot revocation:**
1. **Server makes no assumptions about key compromise** - The server's job is to enforce the whitelist at connection time, not to detect or respond to compromise
2. **Manual restart is acceptable** - If an operator learns a client key is compromised, a simple server restart (2 seconds) is perfectly adequate
3. **Symmetric responsibility** - Just as clients can choose not to connect to servers with compromised keys, operators can restart servers to remove compromised client keys
4. **Operator responsibility** - Key management and compromise response are the operator's concern, not the server's

**Revocation workflow (current and sufficient):**
```bash
# 1. Remove compromised key from whitelist
vim ~/.ascii-chat/authorized_clients.txt

# 2. Restart server (takes ~2 seconds)
killall ascii-chat-server
./ascii-chat-server --client-keys ~/.ascii-chat/authorized_clients.txt

# Result: Compromised key can no longer connect
```

**Why this is acceptable:**
- ✅ Simple and predictable
- ✅ No additional complexity or attack surface
- ✅ Matches SSH's `authorized_keys` model (also requires service restart for revocation)
- ✅ If a key is compromised, a 2-second restart is not a meaningful security delay
- ✅ Zero-downtime reload is overkill for a video chat application

**Alternative considered and rejected:**
Signal-based hot reload (SIGHUP) would add complexity for minimal benefit. ASCII-Chat prioritizes simplicity over operational features that don't improve security.

**Impact:** None - This is not a security issue, just an operational characteristic

**Status:** 🟢 **Won't Fix - Out of Scope** - Manual restart is the intended design

---

### Issue 5: Known Hosts TOFU Weakness ⚠️ **INFORMATIONAL**

**Severity:** Informational (inherent to TOFU model)

**Description:**
Trust On First Use (TOFU) means the first connection is always vulnerable. If an attacker MITM's the first connection, their key is saved as "trusted".

**This is the same vulnerability as:**
- SSH on first connection
- HTTPS certificate pinning on first connection
- Signal safety numbers on first message

**Attack scenario:**
```
User's first connection to server:
  1. Attacker intercepts first connection
  2. Attacker presents their own Ed25519 key
  3. Client saves attacker's key to known_hosts
  4. Future connections verify against attacker's key ✓
  5. Real server's key is never seen
```

**Why this is acceptable:**
1. **Industry standard:** SSH uses the same model
2. **User can verify:** Out-of-band key fingerprint verification (QR code, voice call, etc.)
3. **Detectable:** Key change triggers warning
4. **Better than nothing:** Prevents MITM on all future connections

**Mitigations:**
1. **Manual verification (already supported):**
   ```bash
   # Server shows fingerprint
   ascii-chat-server --key ~/.ssh/id_ed25519
   Server fingerprint: SHA256:abc123...

   # Client verifies before saving
   ascii-chat-client --verify-fingerprint SHA256:abc123...
   ```

2. **Future: Verification server (issue #82)**
   - Pre-register keys with verification server
   - First connection queries server for expected key
   - No TOFU vulnerability

**Impact:** Informational - This is an accepted trade-off in the TOFU model

**Status:** 🟢 Acceptable by design (future verification server will improve this)

---

### Issue 6: Code Duplication in Handshake Implementation ⚠️ **TECHNICAL DEBT**

**Severity:** Low (code quality issue, not a security vulnerability)

**Description:**
The cryptographic protocol is symmetric - both client and server perform identical crypto operations - yet the code duplicates logic between `crypto_client_handshake()` and `crypto_server_handshake()`.

**Problem:**
```c
// lib/crypto/handshake.c has two nearly identical functions:
crypto_client_handshake()  // Lines 416+
crypto_server_handshake()  // Lines 772+

// Both compute identical crypto operations:
// - HMAC(password_key, nonce || shared_secret)
// - crypto_verify_hmac_ex(password_key, combined_data, 64, hmac)
// - Ed25519 signature generation/verification
// - DH key derivation
```

**Why this happened:**
The handshake state machines are procedural - client and server have different:
- State transition order (who initiates first)
- Requirement checking (server checks whitelist, client checks --server-key)
- Error message phrasing

But the underlying crypto operations are identical.

**Current duplication examples:**
1. **Password HMAC computation** (appears 5 times):
   ```c
   // Client: handshake.c:569-582, 644-657, 706-719
   // Server: handshake.c:890-902, 962-974
   uint8_t combined_data[64];
   memcpy(combined_data, nonce, 32);
   memcpy(combined_data + 32, shared_secret, 32);
   crypto_compute_hmac_ex(password_key, combined_data, 64, hmac);
   ```

2. **Ed25519 signature verification** (appears 2 times):
   ```c
   // Identical code in client and server for verifying peer's signature
   ```

**Refactoring proposal:**
Extract symmetric crypto operations into reusable functions:

```c
// crypto_auth.c - New file for shared authentication logic
crypto_result_t crypto_compute_auth_response(
  const crypto_context_t *ctx,
  const uint8_t nonce[32],
  uint8_t hmac_out[32]
) {
  uint8_t combined_data[64];
  memcpy(combined_data, nonce, 32);
  memcpy(combined_data + 32, ctx->shared_key, 32);

  const uint8_t *auth_key = ctx->has_password ? ctx->password_key : ctx->shared_key;
  return crypto_compute_hmac_ex(auth_key, combined_data, 64, hmac_out);
}

bool crypto_verify_auth_response(
  const crypto_context_t *ctx,
  const uint8_t nonce[32],
  const uint8_t expected_hmac[32]
) {
  uint8_t combined_data[64];
  memcpy(combined_data, nonce, 32);
  memcpy(combined_data + 32, ctx->shared_key, 32);

  const uint8_t *auth_key = ctx->has_password ? ctx->password_key : ctx->shared_key;
  return crypto_verify_hmac_ex(auth_key, combined_data, 64, expected_hmac);
}
```

**Benefits of refactoring:**
- ✅ DRY principle - Single source of truth for crypto operations
- ✅ Easier to test - Shared functions can be unit tested independently
- ✅ Less error-prone - Bug fixes only need to be applied once
- ✅ Better maintainability - Protocol changes affect fewer locations

**Why not refactored yet:**
- Security fixes took priority
- Handshake logic works correctly as-is
- Refactoring requires careful testing to avoid breaking changes

**Impact:** Low - This is technical debt, not a security issue

**Status:** 🟢 **FIXED** - Shared authentication functions implemented

**Implementation:**
The password HMAC duplication has been eliminated by extracting shared functions:

```c
// lib/crypto/crypto.c - New shared authentication helpers
crypto_result_t crypto_compute_auth_response(const crypto_context_t *ctx,
                                             const uint8_t nonce[32],
                                             uint8_t hmac_out[32]);

bool crypto_verify_auth_response(const crypto_context_t *ctx,
                                  const uint8_t nonce[32],
                                  const uint8_t expected_hmac[32]);
```

**Code locations:**
- `lib/crypto/crypto.h:182-206` - Function declarations with documentation
- `lib/crypto/crypto.c:655-689` - Implementation of shared functions
- `lib/crypto/handshake.c:572` - Client AUTH_RESPONSE (3 locations) now use shared function
- `lib/crypto/handshake.c:815` - Client SERVER_AUTH_RESPONSE verification uses shared function
- `lib/crypto/handshake.c:862` - Server AUTH_RESPONSE verification uses shared function
- `lib/crypto/handshake.c:923` - Server SERVER_AUTH_RESPONSE computation uses shared function

**Results:**
- ✅ ~35 lines of duplicated code eliminated
- ✅ Single source of truth for HMAC computation and verification
- ✅ Bug fixes now apply to all code paths automatically
- ✅ Shared functions can be unit tested independently

---

### Issue 7: Timing Attack on Public Key Comparisons ⚠️ **CRITICAL**

**Severity:** Critical (side-channel information leakage)

**Description:**
Public key comparisons used variable-time `memcmp()` instead of constant-time comparison, allowing timing attacks that could leak information about cryptographic keys through side-channel analysis.

**Vulnerability:**
The standard C library function `memcmp()` returns early on the first byte difference. This creates timing differences that can be measured by an attacker to learn information about the expected key.

```c
// ❌ WRONG: memcmp() is NOT constant-time
if (memcmp(server_key, expected_key.key, 32) == 0) {
  return 1;  // Match
}
```

**Attack scenario:**
```
Attacker tries different server identity keys:

Attempt 1: Key starts with 0x00... → memcmp() fails on byte 0 → 10ns
Attempt 2: Key starts with 0xAB... → memcmp() fails on byte 0 → 10ns
Attempt 3: Key starts with 0xFF... → memcmp() succeeds on byte 0, fails on byte 1 → 11ns ✓

Attacker learns: First byte of expected key is 0xFF
Repeat for each byte to recover full 32-byte key
```

**Root cause:**
Three locations used `memcmp()` for cryptographic key comparisons:
1. `lib/crypto/handshake.c:194` - Server identity verification during client key exchange
2. `lib/crypto/handshake.c:363` - Client whitelist verification during server auth challenge
3. `lib/crypto/known_hosts.c:69` - Known hosts verification (client-side server verification)

**Why this is critical:**
- Timing attacks are **practical** - Measurable over network with enough samples
- Leaks information about **expected keys** - Helps attacker forge identity
- Affects **authentication bypass** - Compromise of identity verification
- Applicable to **all authentication modes** - SSH keys, whitelists, known_hosts

**Status:** 🟢 **FIXED** - All comparisons now use constant-time `sodium_memcmp()`

**Implementation:**
All cryptographic key comparisons now use libsodium's constant-time comparison:

```c
// ✅ CORRECT: sodium_memcmp() is constant-time
// Compare server's IDENTITY key with expected key (constant-time to prevent timing attacks)
if (sodium_memcmp(server_identity_key, expected_key.key, 32) != 0) {
  log_error("Server identity key mismatch - potential MITM attack!");
  return -1;
}
```

**Code locations:**
- `lib/crypto/handshake.c:194` - ✅ Fixed: Server identity verification
- `lib/crypto/handshake.c:363` - ✅ Fixed: Client whitelist verification
- `lib/crypto/known_hosts.c:69` - ✅ Fixed: Known hosts verification

**How `sodium_memcmp()` prevents timing attacks:**
```c
// From libsodium source (simplified):
int sodium_memcmp(const void *b1, const void *b2, size_t len) {
  const unsigned char *c1 = b1;
  const unsigned char *c2 = b2;
  unsigned char d = 0;

  // Always compares ALL bytes, never returns early
  for (size_t i = 0; i < len; i++) {
    d |= c1[i] ^ c2[i];
  }

  return (1 & ((d - 1) >> 8)) - 1;  // Constant-time final comparison
}
```

**Benefits of constant-time comparison:**
- ✅ **No timing variation** - Takes same time regardless of where keys differ
- ✅ **Cryptographically sound** - Standard practice for key comparison
- ✅ **libsodium guarantee** - Maintained by crypto experts
- ✅ **Zero performance cost** - 32-byte comparison is <100ns either way

**Cleanup performed:**
Removed obsolete duplicate files that contained vulnerable code:
- `lib/known_hosts.c` - Deleted (contained `memcmp()`, not used in build)
- `lib/known_hosts.h` - Deleted (duplicate of `lib/crypto/known_hosts.h`)

The build system only compiles `lib/crypto/known_hosts.c` which uses the secure implementation.

**Impact:** Critical vulnerability fixed - All cryptographic key comparisons now resistant to timing attacks

---

### Summary of Required Fixes

| Issue | Severity | Fix Difficulty | Status |
|-------|----------|----------------|--------|
| Password Mode MITM | 🔴 Critical | High | ✅ **FIXED** - HMACs bound to DH shared_secret |
| No Mutual Auth | 🔴 High | Medium | ✅ **FIXED** - Bidirectional challenge-response |
| Replay Across Sessions | 🟡 Medium | Low | ✅ **FIXED** - Session IDs implemented |
| No Whitelist Revocation | 🟢 Low | N/A | 🟢 **Won't Fix** - Out of scope, manual restart acceptable |
| TOFU Weakness | 🟢 Info | N/A | 🟢 **Acceptable** - Inherent to TOFU model |
| Code Duplication | 🟢 Low | Low | ✅ **FIXED** - Shared authentication functions |
| Timing Attack on Keys | 🔴 Critical | Low | ✅ **FIXED** - Constant-time `sodium_memcmp()` |

### Recommendations

**Security Status:** ✅ **All security vulnerabilities have been addressed**

~~**Before production use:**~~
1. ~~✅ Fix mutual authentication (add server challenge-response)~~ - **DONE**
2. ~~✅ Fix replay vulnerability (add session IDs to nonces)~~ - **DONE**
3. ~~✅ Fix password mode MITM (bind HMACs to DH shared_secret)~~ - **DONE**
4. ~~✅ Refactor crypto handshake to share symmetric operations~~ - **DONE**

**Long-term enhancements (out of current scope):**
- Implement verification server (issue #82) - For distributed trust infrastructure
- Add out-of-band fingerprint verification helpers (QR codes, etc.) - For manual verification UX
- Post-quantum cryptography (Kyber/Dilithium) - When libsodium adds support

---

## Future Enhancements

### 1. Verification Server (Planned - See Issue #82)

**Problem:** No global certificate authority like HTTPS

**Proposed solution:** Optional verification server for key registry

**Architecture:**
```
┌─────────┐
│ Client  │────┐
└─────────┘    │
               ├─→ [Verification Server] ←─── Stores public keys
┌─────────┐    │     - Users register keys
│ Server  │────┘     - Provides key lookup
└─────────┘          - Issues signed certificates
```

**Benefits:**
- ✅ Verified identity without pre-sharing keys
- ✅ Revocation support (server marks keys as invalid)
- ✅ Discovery service integration (session strings + verification)

**Trust model:**
Similar to Signal's key server - optional, transparency log, user can verify out-of-band

**Status:** RFC in progress (see issue #82 for detailed spec)

### 2. Post-Quantum Cryptography

**Current:** X25519 (not quantum-resistant)

**Future:** Hybrid key exchange with post-quantum algorithm

**Candidates:**
- **Kyber:** NIST-selected post-quantum KEM (key encapsulation)
- **X25519-Kyber:** Hybrid combining classical + post-quantum

**Timeline:** Wait for libsodium to add post-quantum support (currently in development)

**Backward compatibility:** Hybrid mode allows gradual migration

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
- Social accountability (anyone can verify log integrity)

**Implementation:**
- Merkle tree of Ed25519 public keys
- Signed by verification server
- Clients can request inclusion proofs

**Status:** Research phase

---

## Appendix: Cryptography Bugs to Watch For

### Bug Class 1: Nonce Reuse

**Danger:** Reusing a nonce with the same key breaks XSalsa20-Poly1305 security

**How it happens:**
```c
// ❌ WRONG: Same nonce for multiple packets
uint8_t nonce[24] = {0};  // Never changes!
for (int i = 0; i < 100; i++) {
  crypto_secretbox_easy(ciphertext, plaintext, nonce, key);  // Same nonce!
}
```

**Fix:**
```c
// ✅ CORRECT: Increment nonce counter
uint64_t counter = 1;
for (int i = 0; i < 100; i++) {
  uint8_t nonce[24] = {0};
  *(uint64_t*)nonce = htole64(counter++);  // Unique nonce per packet
  crypto_secretbox_easy(ciphertext, plaintext, nonce, key);
}
```

**Test:** Verify nonce increments in packet capture

### Bug Class 2: Timing Attacks in Verification

**Danger:** Variable-time comparison leaks information

**How it happens:**
```c
// ❌ WRONG: memcmp() returns early on first mismatch
if (memcmp(expected_hmac, received_hmac, 32) == 0) {
  // Attacker can measure timing differences to guess HMAC
}
```

**Fix:**
```c
// ✅ CORRECT: Constant-time comparison
if (crypto_verify_32(expected_hmac, received_hmac) == 0) {
  // Takes same time regardless of mismatch location
}
```

**Test:** Run verification 10,000 times with random HMACs, verify timing is consistent

### Bug Class 3: Signature Bypass

**Danger:** Skipping signature verification allows impersonation

**How it happens:**
```c
// ❌ WRONG: Assuming presence of signature field means it's valid
if (packet_len == 128) {
  // Has signature field, assume it's correct
  identity_key = packet->identity_key;
}
```

**Fix:**
```c
// ✅ CORRECT: Always verify signature if present
if (packet_len == 128) {
  if (ed25519_verify_signature(identity_key, ephemeral_key, signature) != 0) {
    return -1;  // REJECT invalid signature
  }
}
```

**Test:** Send handshake with random signature bytes → should be rejected

### Bug Class 4: Replay Attacks

**Danger:** Old packets can be re-sent by attacker

**How it happens:**
```c
// ❌ WRONG: No replay protection
decrypt_packet(ciphertext, plaintext);
process_packet(plaintext);  // Attacker can replay old encrypted packets
```

**Fix:**
```c
// ✅ CORRECT: Check sequence numbers or use nonces
if (packet->sequence_number <= last_seen_sequence) {
  return -1;  // REJECT replayed packet
}
```

**Note:** ASCII-Chat uses nonce-based encryption which provides implicit replay resistance (same nonce won't decrypt to same plaintext due to MAC)

### Bug Class 5: Key Confusion

**Danger:** Using wrong key for operation

**How it happens:**
```c
// ❌ WRONG: Using password key when DH key should be used
if (password_set) {
  crypto_secretbox_easy(ciphertext, plaintext, nonce, password_key);
} else {
  crypto_secretbox_easy(ciphertext, plaintext, nonce, shared_secret);
}
// Attacker can cause decrypt with wrong key by manipulating password flag
```

**Fix:**
```c
// ✅ CORRECT: Consistent key selection logic
uint8_t *encryption_key = (key_exchange_complete && shared_secret_valid)
                          ? shared_secret
                          : password_key;
crypto_secretbox_easy(ciphertext, plaintext, nonce, encryption_key);
```

**Test:** Verify encrypted packets with password can't be decrypted with DH key and vice versa

---

## References

- [libsodium Documentation](https://doc.libsodium.org/)
- [RFC 7748: Elliptic Curves for Security (X25519)](https://tools.ietf.org/html/rfc7748)
- [RFC 8439: ChaCha20 and Poly1305](https://tools.ietf.org/html/rfc8439)
- [Argon2: Password Hashing Competition Winner](https://github.com/P-H-C/phc-winner-argon2)
- [NaCl: Networking and Cryptography library](https://nacl.cr.yp.to/)
- [Signal Protocol](https://signal.org/docs/)
- [OpenSSH SSH Agent Protocol](https://tools.ietf.org/id/draft-miller-ssh-agent-04.html)
- [SSH_ASKPASS Environment Variable](https://man.openbsd.org/ssh-add.1#ENVIRONMENT)

---

**Document Version:** 2.1
**Last Updated:** October 2025 (SSH agent auto-add feature)
**Maintainer:** ASCII-Chat Development Team
**License:** Same as ASCII-Chat project (see LICENSE)

🤖 *This document was generated with assistance from [Claude Code](https://claude.ai/code)*
