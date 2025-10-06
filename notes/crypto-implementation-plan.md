# Crypto Implementation Plan

**Date**: October 6, 2025
**Status**: Ready to implement
**Estimated Time**: 6-10 hours total

## Philosophy: Privacy by Default

> "Encrypted by default. You can't be sure who you're talking to without a shared secret, but at least your ISP can't see your video chat."

## Overview

Implement end-to-end encryption for ASCII-Chat using the existing lib/crypto.c code (533 lines already written). **Encryption is enabled by default** using X25519 Diffie-Hellman key exchange, with optional password authentication for MITM protection.

## Architecture: Three Modes

### Mode 1: Default Encrypted (No Flags)

**The default mode** - requires no configuration:

```bash
# Server
./ascii-chat-server

# Client
./ascii-chat-client
```

**What happens**:
- Pure ephemeral DH key exchange (X25519)
- Both sides generate random keypairs **in memory only**
- Keys wiped on exit, never shown to user
- Each connection gets fresh keypairs (forward secrecy)

**Security properties**:
- ✅ Protects against passive eavesdropping (ISP, WiFi snooping)
- ✅ Forward secrecy (compromising one session doesn't affect others)
- ✅ Full packet encryption (headers + payloads)
- ⚠️ Vulnerable to active MITM attacks (attacker can intercept DH exchange)

**Use case**: "I don't want my coffee shop WiFi admin watching my video chat"

### Mode 2: Authenticated Encryption (`--key` or `--keyfile`)

**Recommended for security-sensitive use**:

```bash
# Server
./ascii-chat-server --key mypassword

# Client
./ascii-chat-client --key mypassword

# Or with keyfile
echo "mypassword" > /tmp/keyfile
./ascii-chat-server --keyfile /tmp/keyfile
./ascii-chat-client --keyfile /tmp/keyfile
```

**What happens**:
- DH key exchange + password verification
- Server sends random challenge, client proves knowledge of password
- Connection rejected if passwords don't match

**Security properties**:
- ✅ Protects against passive eavesdropping
- ✅ Protects against active MITM attacks (attacker can't prove password knowledge)
- ✅ Forward secrecy
- ✅ Each client has unique shared secret

**Use case**: "I need actual security, not just privacy"

**How to share the password**: Text it to your friend, Signal message, phone call, in person, etc. This is the same model as WiFi passwords or Signal safety numbers.

### Mode 3: Unencrypted (`--no-encrypt`)

**Opt-out for testing/debugging**:

```bash
# Server
./ascii-chat-server --no-encrypt

# Client
./ascii-chat-client --no-encrypt
```

**What happens**:
- No encryption, plain TCP
- Backward compatible with old clients
- Useful for debugging protocol issues

**Security properties**:
- ❌ No protection
- ⚠️ Anyone on the network can see everything

**Use case**: "I'm debugging and need to see raw packets with tcpdump"

## CLI Flag Behavior

```c
// Default: Encrypted (DH only)
./ascii-chat-server              // Encrypted
./ascii-chat-client              // Encrypted

// Authenticated: Password required
./ascii-chat-server --key pass   // Encrypted + password auth
./ascii-chat-client --key pass   // Must match server password

// Keyfile: Password from file
./ascii-chat-server --keyfile /path/to/key
./ascii-chat-client --keyfile /path/to/key

// Opt-out: Unencrypted
./ascii-chat-server --no-encrypt  // Plain TCP
./ascii-chat-client --no-encrypt  // Plain TCP
```

**Flag implications**:
- `--key PASSWORD` → Encryption enabled with auth
- `--keyfile FILE` → Encryption enabled with auth (password from file)
- `--no-encrypt` → Encryption disabled
- No flags → Encryption enabled (DH only)

**Validation**:
- `--key` and `--keyfile` are mutually exclusive
- If server uses `--key`/`--keyfile`, client MUST provide matching password
- If client provides password but server doesn't → connection rejected
- `--no-encrypt` on one side and encryption on other → connection rejected

## The MITM Problem (And Why It's Okay)

**The fundamental security tradeoff**:

There is **no way** to prevent MITM attacks without sharing a secret through a separate channel. This isn't a bug, it's mathematics:

1. **Server and client connect over the internet** (untrusted channel)
2. **They've never met before** (no prior shared secret)
3. **Attacker controls the network** (can intercept/modify packets)
4. **Question**: How do they establish a secure channel?

**Answer**: They can't! Not without one of:
- Pre-shared secret (password)
- Trusted third party (certificate authority)
- Out-of-band verification (QR code, safety numbers, etc.)

**What we chose**:
- Default mode accepts this tradeoff (privacy, not security)
- Users who need security use `--key` (just like WiFi passwords)
- This is the same model as SSH first connection, Signal safety numbers, Bluetooth pairing

**The vibe**: "Encryption by default protects against passive attacks. Add a password if you need protection against active attacks too."

## Security Properties Summary

| Property | Default | --key | --no-encrypt |
|----------|---------|-------|--------------|
| Passive eavesdrop protection | ✅ | ✅ | ❌ |
| MITM protection | ❌ | ✅ | ❌ |
| Forward secrecy | ✅ | ✅ | N/A |
| Replay protection | ✅ | ✅ | ❌ |
| Per-client isolation | ✅ | ✅ | N/A |
| Full packet encryption | ✅ | ✅ | ❌ |

## Existing Code (lib/crypto.c)

Already implemented and ready to use:

```c
crypto_init()                     // Initialize libsodium
crypto_generate_keypair()         // X25519 keypair generation
crypto_compute_shared_secret()    // DH key exchange
crypto_derive_key_from_password() // Argon2id password hashing
crypto_encrypt_packet()           // XSalsa20-Poly1305 encryption
crypto_decrypt_packet()           // XSalsa20-Poly1305 decryption
```

**Note**: libsodium already has cross-platform CSPRNG (`randombytes_buf()`), so we get secure random numbers on Linux/macOS/Windows for free.

## Implementation Phases

### Phase 1: CLI Flags & In-Memory State (2 hours)

**Goal**: Parse flags, store crypto config in memory, initialize libsodium

#### 1.1 Update options.c/h

```c
typedef struct {
    // Existing fields...

    // Crypto options
    bool no_encrypt;        // Disable encryption (opt-out)
    char* key;              // Password for authentication
    char* keyfile;          // Path to file containing password
} options_t;
```

**Parsing logic**:
- Default: encryption enabled (if `!no_encrypt && !key && !keyfile`)
- If `--no-encrypt`: encryption disabled
- If `--key` or `--keyfile`: encryption enabled with auth
- Validate: `--key` and `--keyfile` are mutually exclusive
- Validate: `--no-encrypt` cannot be used with `--key`/`--keyfile`

**Helper function**:
```c
static char* read_keyfile(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        log_error("Cannot open keyfile: %s", path);
        exit(1);
    }

    char* password = SAFE_MALLOC(256);
    if (fgets(password, 256, f) == NULL) {
        log_error("Cannot read keyfile");
        exit(1);
    }
    fclose(f);

    // Trim whitespace/newlines
    size_t len = strlen(password);
    while (len > 0 && (password[len-1] == '\n' || password[len-1] == '\r' || password[len-1] == ' ')) {
        password[--len] = '\0';
    }

    return password;
}
```

#### 1.2 Add crypto state to server (src/server/main.c)

```c
typedef struct {
    bool encryption_enabled;
    uint8_t server_public_key[32];   // Ephemeral keypair (new per server run)
    uint8_t server_private_key[32];
    uint8_t password_key[32];        // Derived from password (if provided)
    bool require_auth;               // True if --key/--keyfile provided
} server_crypto_state_t;

// Global crypto state
server_crypto_state_t g_crypto_state = {0};
```

**Initialization** (in main()):
```c
// Determine if encryption is enabled
bool encryption_enabled = !options.no_encrypt;

if (encryption_enabled) {
    crypto_init();
    g_crypto_state.encryption_enabled = true;

    // Generate ephemeral server keypair
    crypto_generate_keypair(g_crypto_state.server_public_key,
                           g_crypto_state.server_private_key);

    if (options.key || options.keyfile) {
        // Authenticated mode
        const char* password = options.key ? options.key : read_keyfile(options.keyfile);
        crypto_derive_key_from_password(password, g_crypto_state.password_key);
        g_crypto_state.require_auth = true;

        log_info("Encryption: ENABLED (authenticated)");
        log_info("Clients must connect with matching password");

        // Security: Zero password after hashing
        if (options.keyfile) {
            memset((void*)password, 0, strlen(password));
            free((void*)password);
        }
    } else {
        // Default encrypted mode (DH only)
        log_info("Encryption: ENABLED (unauthenticated)");
        log_warn("No password authentication - vulnerable to MITM attacks");
        log_info("Use --key PASSWORD for MITM protection");
    }
} else {
    log_warn("Encryption: DISABLED");
    log_warn("All traffic sent in PLAINTEXT");
}
```

#### 1.3 Add crypto state to client (src/client/main.c)

```c
typedef struct {
    bool encryption_enabled;
    uint8_t client_public_key[32];   // Ephemeral keypair (new per connection)
    uint8_t client_private_key[32];
    uint8_t shared_secret[32];       // Computed from DH
    uint8_t password_key[32];        // Derived from password (if provided)
    uint64_t send_nonce;             // Nonce counter for sending
    uint64_t recv_nonce;             // Last received nonce (for replay protection)
    bool has_password;               // True if --key/--keyfile provided
} client_crypto_state_t;

client_crypto_state_t g_crypto_state = {0};
```

**Initialization** (in main()):
```c
// Determine if encryption is enabled
bool encryption_enabled = !options.no_encrypt;

if (encryption_enabled) {
    crypto_init();
    g_crypto_state.encryption_enabled = true;

    if (options.key || options.keyfile) {
        const char* password = options.key ? options.key : read_keyfile(options.keyfile);
        crypto_derive_key_from_password(password, g_crypto_state.password_key);
        g_crypto_state.has_password = true;

        log_info("Encryption: ENABLED (will authenticate with password)");

        // Security: Zero password after hashing
        if (options.keyfile) {
            memset((void*)password, 0, strlen(password));
            free((void*)password);
        }
    } else {
        log_info("Encryption: ENABLED (no authentication)");
    }

    // Keypair generated AFTER connection (ephemeral)
} else {
    log_info("Encryption: DISABLED");
}
```

### Phase 2: Key Exchange Protocol (3 hours)

**Goal**: Establish shared secret using Diffie-Hellman key exchange

#### 2.1 Add new packet types (lib/network.h)

```c
typedef enum {
    // Existing types...
    PACKET_TYPE_AUDIO_BATCH = 13,

    // Crypto handshake packets (ALWAYS SENT UNENCRYPTED)
    PACKET_TYPE_KEY_EXCHANGE_INIT = 14,      // Server -> Client: {server_pubkey[32]}
    PACKET_TYPE_KEY_EXCHANGE_RESPONSE = 15,  // Client -> Server: {client_pubkey[32]}
    PACKET_TYPE_AUTH_CHALLENGE = 16,         // Server -> Client: {nonce[32]}
    PACKET_TYPE_AUTH_RESPONSE = 17,          // Client -> Server: {HMAC[32]}
    PACKET_TYPE_HANDSHAKE_COMPLETE = 18,     // Server -> Client: "encryption ready"
} packet_type_t;
```

#### 2.2 Handshake Flow

**Case 1: Both sides have encryption enabled (DH only, no password)**

```
Client connects -> TCP established

1. Server -> Client: KEY_EXCHANGE_INIT
   Payload: {server_public_key[32]}

2. Client receives, generates ephemeral keypair
   crypto_generate_keypair(&client_public_key, &client_private_key)

3. Client computes shared secret
   crypto_compute_shared_secret(client_private_key, server_public_key, &shared_secret)

4. Client -> Server: KEY_EXCHANGE_RESPONSE
   Payload: {client_public_key[32]}

5. Server computes shared secret
   crypto_compute_shared_secret(server_private_key, client_public_key, &shared_secret)
   Store in client_t->shared_secret[32]

6. Server -> Client: HANDSHAKE_COMPLETE

All future packets are now encrypted with shared_secret
```

**Case 2: Server has password, client has matching password**

```
(Steps 1-5 same as above)

6. Server -> Client: AUTH_CHALLENGE
   Payload: {random_nonce[32]}

7. Client computes proof:
   proof = HMAC-SHA256(password_key, nonce)

8. Client -> Server: AUTH_RESPONSE
   Payload: {proof[32]}

9. Server verifies:
   expected_proof = HMAC-SHA256(password_key, nonce)
   if (memcmp(proof, expected_proof, 32) == 0):
       Send HANDSHAKE_COMPLETE
   else:
       log_error("Authentication failed")
       Disconnect client

10. Server -> Client: HANDSHAKE_COMPLETE

All future packets are now encrypted with shared_secret
```

**Case 3: Encryption mismatch**

```
Server encrypted, client --no-encrypt:
- Client sends unencrypted packet
- Server expects KEY_EXCHANGE_INIT response
- Server disconnects after timeout (10 seconds)

Server --no-encrypt, client encrypted:
- Server sends unencrypted PACKET_TYPE_CLIENT_JOIN or similar
- Client expects KEY_EXCHANGE_INIT
- Client disconnects with error: "Server does not support encryption"
```

**Case 4: Password mismatch**

```
Server --key foo, client --key bar:
- Steps 1-8 complete
- Step 9: Server verifies HMAC, fails
- Server sends error (TODO: which packet type?) and disconnects
- Client logs: "Authentication rejected by server"

Server no password, client --key foo:
- Server completes handshake at step 6 (no AUTH_CHALLENGE)
- Client expects AUTH_CHALLENGE, doesn't get it
- Client disconnects: "Server does not require password"
```

#### 2.3 Per-Client Encryption State (server)

Update `client_t` struct in server:
```c
typedef struct client_t {
    // Existing fields...

    // Crypto state (one shared_secret per client)
    uint8_t shared_secret[32];
    uint64_t send_nonce;          // Increment per packet sent
    uint64_t recv_nonce;          // Last received nonce (replay protection)
    bool encryption_ready;        // True after handshake complete
} client_t;
```

### Phase 3: Full Packet Encryption (3 hours)

**Goal**: Encrypt entire packets (headers + payloads) after handshake

#### 3.1 Encrypted Packet Structure

**Handshake packets** (unencrypted):
```c
[uint32_t magic] [uint16_t type] [uint32_t length] [uint32_t crc32] [uint32_t client_id] [payload...]
```

**All packets after handshake** (encrypted):
```c
[uint32_t magic] [encrypted_blob]

Where encrypted_blob contains:
    [uint64_t nonce] [uint16_t type] [uint32_t length] [uint32_t client_id] [payload...]
```

**Why keep magic unencrypted**:
- Protocol identification (detect ascii-chat vs random data)
- Version negotiation in future
- Only 4 bytes exposed (minimal metadata leakage)

**Everything else encrypted**:
- Packet type (prevents traffic analysis)
- Length (prevents size-based fingerprinting)
- Client ID (privacy)
- Payload (confidentiality)
- Nonce (prevents tampering with replay counter)

#### 3.2 Sending Encrypted Packets

```c
// New function in lib/network.c
ssize_t send_encrypted_packet(
    socket_t sockfd,
    packet_type_t type,
    uint32_t client_id,
    const uint8_t* payload,
    uint32_t payload_len,
    const uint8_t shared_secret[32],
    uint64_t* nonce_counter
) {
    // 1. Build plaintext packet data (nonce + header + payload)
    size_t plaintext_size = 8 + 2 + 4 + 4 + payload_len;
    uint8_t* plaintext = SAFE_MALLOC(plaintext_size);
    uint64_t nonce = (*nonce_counter)++;

    size_t offset = 0;
    memcpy(plaintext + offset, &nonce, 8); offset += 8;
    memcpy(plaintext + offset, &type, 2); offset += 2;
    memcpy(plaintext + offset, &payload_len, 4); offset += 4;
    memcpy(plaintext + offset, &client_id, 4); offset += 4;
    if (payload_len > 0) {
        memcpy(plaintext + offset, payload, payload_len);
    }

    // 2. Encrypt entire packet data
    size_t encrypted_size = plaintext_size + crypto_secretbox_MACBYTES;
    uint8_t* encrypted = SAFE_MALLOC(encrypted_size);

    if (crypto_encrypt_packet(plaintext, plaintext_size,
                              shared_secret, nonce,
                              encrypted, &encrypted_size) != 0) {
        log_error("Encryption failed");
        free(plaintext);
        free(encrypted);
        return -1;
    }

    // 3. Send magic + encrypted blob
    uint32_t magic = PACKET_MAGIC;
    if (send_all(sockfd, &magic, 4) != 4) {
        free(plaintext);
        free(encrypted);
        return -1;
    }

    ssize_t sent = send_all(sockfd, encrypted, encrypted_size);

    free(plaintext);
    free(encrypted);

    return sent + 4;
}
```

#### 3.3 Receiving Encrypted Packets

```c
// New function in lib/network.c
int receive_encrypted_packet(
    socket_t sockfd,
    packet_type_t* type,
    uint32_t* client_id,
    uint8_t** payload,
    uint32_t* payload_len,
    const uint8_t shared_secret[32],
    uint64_t* last_recv_nonce
) {
    // 1. Read magic
    uint32_t magic;
    if (recv_all(sockfd, &magic, 4) != 4) return -1;
    if (magic != PACKET_MAGIC) {
        log_error("Invalid magic: 0x%08X", magic);
        return -1;
    }

    // 2. Read encrypted blob (we need to peek at length or use max size)
    uint8_t encrypted[MAX_PACKET_SIZE];
    ssize_t encrypted_len = recv(sockfd, encrypted, sizeof(encrypted), 0);
    if (encrypted_len <= 0) return -1;

    // 3. Decrypt
    size_t plaintext_size = encrypted_len - crypto_secretbox_MACBYTES;
    uint8_t* plaintext = SAFE_MALLOC(plaintext_size);

    if (crypto_decrypt_packet(encrypted, encrypted_len,
                              shared_secret,
                              plaintext, &plaintext_size) != 0) {
        log_error("Decryption failed");
        free(plaintext);
        return -1;
    }

    // 4. Parse plaintext
    uint64_t nonce;
    size_t offset = 0;
    memcpy(&nonce, plaintext + offset, 8); offset += 8;
    memcpy(type, plaintext + offset, 2); offset += 2;
    memcpy(payload_len, plaintext + offset, 4); offset += 4;
    memcpy(client_id, plaintext + offset, 4); offset += 4;

    // 5. Replay protection
    if (nonce <= *last_recv_nonce) {
        log_error("Replay attack detected! nonce=%llu, expected > %llu", nonce, *last_recv_nonce);
        free(plaintext);
        return -1;
    }
    *last_recv_nonce = nonce;

    // 6. Extract payload
    if (*payload_len > 0) {
        *payload = SAFE_MALLOC(*payload_len);
        memcpy(*payload, plaintext + offset, *payload_len);
    } else {
        *payload = NULL;
    }

    free(plaintext);
    return 0;
}
```

#### 3.4 Integration Points

**Server** (src/server/client.c or src/server/main.c):
- Modify `client_thread_func()` to perform handshake FIRST
- After handshake complete, route through encrypted/unencrypted send/receive
- Create wrapper: `send_packet_auto()` that checks `client->encryption_ready`

**Client** (src/client/main.c):
- After TCP connect, wait for KEY_EXCHANGE_INIT
- Perform handshake
- After HANDSHAKE_COMPLETE, use encrypted send/receive
- Create wrapper: `send_packet_auto()` that checks `g_crypto_state.encryption_enabled`

**Example wrapper**:
```c
ssize_t send_packet_auto(socket_t sockfd, packet_type_t type, uint32_t client_id,
                         const uint8_t* payload, uint32_t payload_len,
                         client_t* client /* or global state */) {
    if (client->encryption_ready) {
        return send_encrypted_packet(sockfd, type, client_id, payload, payload_len,
                                    client->shared_secret, &client->send_nonce);
    } else {
        return send_packet(sockfd, type, client_id, payload, payload_len);
    }
}
```

### Phase 4: Testing & Validation (2 hours)

#### 4.1 Test Cases

1. **Both default (encrypted, no password)**
   - ✅ Should connect and communicate encrypted
   - ⚠️ Log warning about MITM vulnerability

2. **Both with matching --key**
   - ✅ Should connect and communicate encrypted
   - ✅ Should authenticate successfully

3. **Both with different --key**
   - ✅ Should reject during AUTH_RESPONSE

4. **Server --key, client no password**
   - ✅ Should reject (server requires auth)

5. **Server no password, client --key**
   - ✅ Should reject (client expects auth, server doesn't provide)

6. **Both with --no-encrypt**
   - ✅ Should connect unencrypted
   - ⚠️ Log warning about plaintext

7. **Server encrypted, client --no-encrypt**
   - ✅ Should fail to connect (encryption mismatch)

8. **Multi-client with encryption**
   - ✅ Each client has unique shared_secret
   - ✅ Clients cannot decrypt each other's packets

9. **Replay attack test**
   - ✅ Resending old packet should fail

#### 4.2 Debug Helpers

```c
// Add to common.h
#ifdef DEBUG_CRYPTO
#define log_crypto(...) log_debug("CRYPTO: " __VA_ARGS__)
#else
#define log_crypto(...) ((void)0)
#endif

// Usage
log_crypto("Handshake complete, shared secret established");
log_crypto("Encrypted packet sent: type=%d, len=%u, nonce=%llu", type, len, nonce);
log_crypto("Decrypted packet: type=%d, len=%u, nonce=%llu", type, len, nonce);
```

#### 4.3 Test Script

```bash
#!/bin/bash
# tests/crypto_test.sh

set -e

echo "=== Test 1: Default encrypted (no password) ==="
./build/bin/ascii-chat-server --log-file /tmp/server1.log &
SERVER_PID=$!
sleep 1
timeout 5 ./build/bin/ascii-chat-client --snapshot --log-file /tmp/client1.log || true
kill $SERVER_PID 2>/dev/null || true
grep -q "Encryption: ENABLED" /tmp/server1.log && echo "✅ Test 1 passed"

echo ""
echo "=== Test 2: Matching passwords ==="
./build/bin/ascii-chat-server --key testpass --log-file /tmp/server2.log &
SERVER_PID=$!
sleep 1
timeout 5 ./build/bin/ascii-chat-client --key testpass --snapshot --log-file /tmp/client2.log || true
kill $SERVER_PID 2>/dev/null || true
grep -q "authenticated" /tmp/server2.log && echo "✅ Test 2 passed"

echo ""
echo "=== Test 3: Wrong password ==="
./build/bin/ascii-chat-server --key rightpass --log-file /tmp/server3.log &
SERVER_PID=$!
sleep 1
timeout 5 ./build/bin/ascii-chat-client --key wrongpass --snapshot --log-file /tmp/client3.log 2>&1 | grep -q "Authentication" && echo "✅ Test 3 passed" || echo "❌ Test 3 failed"
kill $SERVER_PID 2>/dev/null || true

echo ""
echo "=== Test 4: Unencrypted mode ==="
./build/bin/ascii-chat-server --no-encrypt --log-file /tmp/server4.log &
SERVER_PID=$!
sleep 1
timeout 5 ./build/bin/ascii-chat-client --no-encrypt --snapshot --log-file /tmp/client4.log || true
kill $SERVER_PID 2>/dev/null || true
grep -q "DISABLED" /tmp/server4.log && echo "✅ Test 4 passed"

echo ""
echo "All tests complete!"
```

### Phase 5: Documentation & Cleanup (1 hour)

#### 5.1 Update README.md

Replace Cryptography section:

```markdown
## Cryptography

**ASCII-Chat is encrypted by default** using modern cryptographic primitives from libsodium:

- **Key Exchange**: X25519 Diffie-Hellman (forward secrecy)
- **Encryption**: XSalsa20-Poly1305 (authenticated encryption)
- **Password Hashing**: Argon2id (memory-hard KDF)
- **Replay Protection**: Nonce counters prevent packet replay

### Default: Encrypted (No Configuration)

```bash
# Server
./bin/ascii-chat-server

# Client
./bin/ascii-chat-client
```

**Privacy from passive eavesdropping**: Your ISP, coffee shop WiFi, or network admin cannot see your video chat. However, without a password, an active attacker with network access could perform a man-in-the-middle attack.

### Recommended: Password Authentication

```bash
# Server
./bin/ascii-chat-server --key mypassword

# Client
./bin/ascii-chat-client --key mypassword
```

**Full security**: Protects against both passive eavesdropping AND active MITM attacks. Share the password with your friend through a separate channel (text message, Signal, in person, etc.) - just like a WiFi password.

**Alternative**: Read password from file
```bash
echo "mypassword" > /tmp/keyfile
./bin/ascii-chat-server --keyfile /tmp/keyfile
./bin/ascii-chat-client --keyfile /tmp/keyfile
```

### Opt-Out: Unencrypted (Testing Only)

```bash
./bin/ascii-chat-server --no-encrypt
./bin/ascii-chat-client --no-encrypt
```

**No protection**: All traffic sent in plaintext. Only use for debugging or testing.

### Security Notes

- **Default encryption**: Protects against passive attacks (ISP snooping, WiFi eavesdropping)
- **Password authentication**: Protects against active attacks (MITM)
- **Forward secrecy**: Each connection uses fresh keys (compromising one session doesn't affect others)
- **Per-client isolation**: Each client has a unique shared secret
- **Full packet encryption**: Headers and payloads encrypted (only 4-byte magic number visible)
- **Replay protection**: Nonce counters prevent attackers from resending old packets

### Why Can't We Prevent MITM Without a Password?

This isn't a flaw in ASCII-Chat - it's a fundamental limitation of cryptography. If two parties have never met before and communicate over an untrusted channel, there's no way to authenticate the connection without:

1. A pre-shared secret (password)
2. A trusted third party (certificate authority like HTTPS uses)
3. Out-of-band verification (QR codes, safety numbers, etc.)

ASCII-Chat uses approach #1 (passwords) because it's simple and users already understand the model from WiFi passwords, Signal safety numbers, and Bluetooth pairing codes.

**The bottom line**: Default encryption protects your privacy. Add a password if you need security.
```

#### 5.2 Update Command Line Flags Section

Update both server and client options:

```markdown
### Server Options

- `-a --address ADDRESS`: IPv4 address to bind to (default: 0.0.0.0)
- `-p --port PORT`: TCP port to listen on (default: 27224)
- `-A --audio`: Enable audio mixing and streaming
- `-L --log-file FILE`: Redirect logs to file
- `--key PASSWORD`: Enable encryption with password authentication
- `--keyfile FILE`: Read password from file (enables encryption)
- `--no-encrypt`: Disable encryption (plaintext mode)
- `-h --help`: Show help message

### Client Options

- `-a --address ADDRESS`: IPv4 address to connect to (default: 0.0.0.0)
- `-p --port PORT`: TCP port (default: 27224)
- [... other options ...]
- `--key PASSWORD`: Connect with password authentication
- `--keyfile FILE`: Read password from file
- `--no-encrypt`: Disable encryption (must match server)
- `-h --help`: Show help message
```

#### 5.3 Update CLAUDE.md

Add to "Recent Updates" section:

```markdown
### Cryptography Implementation (October 2025)
- **Encrypted by default**: All connections use X25519 + XSalsa20-Poly1305
- **Optional password authentication**: `--key` flag prevents MITM attacks
- **Full packet encryption**: Headers and payloads encrypted (minimal metadata leakage)
- **Forward secrecy**: Ephemeral keypairs for each connection
- **Per-client isolation**: Each client has unique shared secret
- **Replay protection**: Nonce counters prevent packet replay
```

## Implementation Checklist

### Phase 1: Foundation
- [ ] Add `no_encrypt`, `key`, `keyfile` fields to options_t
- [ ] Parse `--no-encrypt`, `--key PASSWORD`, `--keyfile FILE` in options.c
- [ ] Implement `read_keyfile()` helper function
- [ ] Validate: `--key` and `--keyfile` mutually exclusive
- [ ] Validate: `--no-encrypt` incompatible with `--key`/`--keyfile`
- [ ] Add `server_crypto_state_t` to server
- [ ] Add `client_crypto_state_t` to client
- [ ] Initialize libsodium with `crypto_init()`
- [ ] Generate ephemeral keypairs on startup (if encryption enabled)
- [ ] Derive password key with Argon2id (if password provided)

### Phase 2: Handshake
- [ ] Add 5 new packet types to network.h (KEY_EXCHANGE_*, AUTH_*, HANDSHAKE_COMPLETE)
- [ ] Implement server: send KEY_EXCHANGE_INIT on client connect
- [ ] Implement client: handle KEY_EXCHANGE_INIT, generate keypair
- [ ] Implement client: compute shared_secret, send KEY_EXCHANGE_RESPONSE
- [ ] Implement server: handle KEY_EXCHANGE_RESPONSE, compute shared_secret
- [ ] Implement server: send AUTH_CHALLENGE (if password mode)
- [ ] Implement client: handle AUTH_CHALLENGE, compute HMAC proof
- [ ] Implement client: send AUTH_RESPONSE
- [ ] Implement server: verify AUTH_RESPONSE, disconnect if wrong
- [ ] Implement server: send HANDSHAKE_COMPLETE
- [ ] Implement client: handle HANDSHAKE_COMPLETE
- [ ] Add `shared_secret[32]` to `client_t` struct
- [ ] Add `encryption_ready` flag to track handshake completion

### Phase 3: Encryption
- [ ] Implement `send_encrypted_packet()` in network.c
- [ ] Implement `receive_encrypted_packet()` in network.c
- [ ] Add nonce management (send_nonce, recv_nonce) to client state
- [ ] Add replay protection (validate nonce > last_recv_nonce)
- [ ] Implement `send_packet_auto()` wrapper (routes encrypted/unencrypted)
- [ ] Implement `receive_packet_auto()` wrapper
- [ ] Replace server send_packet() calls with send_packet_auto()
- [ ] Replace server receive_packet() calls with receive_packet_auto()
- [ ] Replace client send_packet() calls with send_packet_auto()
- [ ] Replace client receive_packet() calls with receive_packet_auto()
- [ ] Keep handshake packets unencrypted

### Phase 4: Testing
- [ ] Test: Both default (should work, log MITM warning)
- [ ] Test: Both --key matching (should work)
- [ ] Test: Both --key mismatched (should reject)
- [ ] Test: Server --key, client no password (should reject)
- [ ] Test: Server no password, client --key (should reject)
- [ ] Test: Both --no-encrypt (should work, log plaintext warning)
- [ ] Test: Server encrypted, client --no-encrypt (should fail)
- [ ] Test: Multi-client encrypted session
- [ ] Test: Replay attack protection
- [ ] Add DEBUG_CRYPTO logging
- [ ] Create tests/crypto_test.sh script

### Phase 5: Documentation
- [ ] Update README.md Cryptography section (remove "NOT YET IMPLEMENTED")
- [ ] Update README.md command line flags
- [ ] Update CLAUDE.md with crypto implementation notes
- [ ] Remove old "NOT YET IMPLEMENTED" warnings from code
- [ ] Add comments explaining handshake flow

## Timeline Estimate

| Phase | Time | Cumulative |
|-------|------|------------|
| Phase 1: Foundation | 2 hours | 2 hours |
| Phase 2: Handshake | 3 hours | 5 hours |
| Phase 3: Encryption | 3 hours | 8 hours |
| Phase 4: Testing | 1 hour | 9 hours |
| Phase 5: Documentation | 1 hour | 10 hours |

**Total**: 10 hours for complete implementation and testing

## Success Criteria

- [ ] Default mode: Server and client connect with DH encryption (no password)
- [ ] Password mode: Server and client can establish encrypted connection with password
- [ ] Authentication: Server rejects clients with wrong password
- [ ] Isolation: Multi-client sessions work, each client has unique shared_secret
- [ ] Security: Replay attacks are detected and blocked
- [ ] Opt-out: --no-encrypt mode works for debugging
- [ ] Quality: No memory leaks (AddressSanitizer clean)
- [ ] Docs: README updated to reflect "encrypted by default"
- [ ] Compatibility: All existing tests still pass

---

**Ready to implement! Start with Phase 1. 🔐**

**The vibe**: "Privacy by default, security if you need it"
