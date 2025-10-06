# Crypto Implementation Plan

**Date**: October 6, 2025
**Status**: Ready to implement
**Estimated Time**: 8-12 hours total

## Philosophy: Privacy by Default, Security When Needed

> "Encrypted by default. You can't be sure who you're talking to without a shared secret, but at least your ISP can't see your video chat."

## Overview

Implement end-to-end encryption for ASCII-Chat using the existing lib/crypto.c code (533 lines already written). **Encryption is enabled by default** using X25519 Diffie-Hellman key exchange, with optional password authentication and public key pinning for MITM protection.

## Architecture: Progressive Security Ladder

Users can choose their security level based on threat model:

### Level 1: Default Encrypted (Privacy)

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
- Keys wiped on exit
- Server displays its public key (user can ignore or share)
- Each connection gets fresh keypairs (forward secrecy)

**Security properties**:
- ✅ Protects against passive eavesdropping (ISP, WiFi snooping)
- ✅ Forward secrecy (compromising one session doesn't affect others)
- ✅ Full packet encryption (headers + payloads)
- ⚠️ Vulnerable to active MITM attacks (attacker can intercept DH exchange)

**Use case**: "I don't want my coffee shop WiFi admin watching my video chat"

### Level 2: Password Authentication (Security)

**Recommended for most users**:

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

### Level 3: Public Key Pinning (Strong Security)

**SSH-like security model**:

```bash
# Server displays on startup:
# ╔════════════════════════════════════════════════════════════════╗
# ║  SERVER PUBLIC KEY                                             ║
# ║  3a7f2c1b9e4d8a6f5c3b7e9d2f8a4c6e1b5d9a3f7c2e8b4d6a1f5c3b7  ║
# ║                                                                ║
# ║  Share with clients for MITM protection:                       ║
# ║    ascii-chat-client --server-key 3a7f2c1b...                  ║
# ╚════════════════════════════════════════════════════════════════╝

# Client verifies server key:
./ascii-chat-client --server-key 3a7f2c1b9e4d8a6f5c3b7e9d2f8a4c6e1b5d9a3f7c2e8b4d6a1f5c3b7
```

**What happens**:
- Client receives server's public key during handshake
- Compares against expected key provided via `--server-key`
- If mismatch: Displays scary warning and ABORTS connection
- If match: Continues with encrypted session

**Security properties**:
- ✅ Protects against passive eavesdropping
- ✅ Protects against active MITM attacks (cryptographically verified)
- ✅ Forward secrecy
- ✅ No shared password needed
- ✅ Public keys can be shared openly (not secret like passwords)

**Use case**: "Want SSH-like security, can share public keys via email/Slack"

**How to share the key**: Copy server's public key from startup banner, paste into email/Slack/etc. Public keys are not secret!

### Level 4: Server Whitelist (Restricted Access)

**Server only accepts specific clients**:

```bash
# Option 1: Comma-separated list on command line
./ascii-chat-server --client-keys "3a7f2c...,f9e2d4...,b8c1e9..."

# Option 2: File with one key per line
./ascii-chat-server --client-keys /path/to/allowed_keys.txt

# allowed_keys.txt format:
# Lines starting with # are comments
# 3a7f2c1b9e4d8a6f5c3b7e9d2f8a4c6e1b5d9a3f7c2e8b4d6a1f5c3b7  # Alice
# f9e2d4c6b8a175392e4d8f1c6a3b7e5d9c2f8a4e6b1d5c9a3f7e2b4d  # Bob
# b8c1e9d2a6f4c7e3b5d9a1f8c4e6b2d7a9f3c5e1b8d4a6f2c9e5b3  # Carol
```

**What happens**:
- Server loads whitelist of allowed client public keys
- During handshake, server verifies client's public key is in whitelist
- If not in whitelist: Reject connection
- Server logs all connection attempts with client keys (for auditing)

**Security properties**:
- ✅ Protects against passive eavesdropping
- ✅ Protects server from unauthorized clients
- ✅ Audit trail of who connected
- ✅ Can revoke access (remove key from whitelist)
- ⚠️ Client still vulnerable to MITM (unless they use `--server-key`)

**Use case**: "Private server, only my friends can connect"

**Client workflow**:
1. Client runs: `./ascii-chat-client` (displays their public key)
2. Client sends key to server operator (via email/Signal/etc.)
3. Server operator adds key to whitelist
4. Client can now connect

### Level 5: Defense in Depth (Maximum Security)

**Combine all security features**:

```bash
# Server: Password + client whitelist
./ascii-chat-server --key mypassword --client-keys allowed_keys.txt

# Client: Password + server key verification
./ascii-chat-client --key mypassword --server-key 3a7f2c1b9e4d8a6f...
```

**Security properties**:
- ✅ Password authentication (both sides verify password)
- ✅ Public key pinning (client verifies server identity)
- ✅ Client whitelist (server only accepts known clients)
- ✅ Forward secrecy
- ✅ Defense in depth (multiple layers of security)

**Use case**: "Paranoid security for sensitive communications"

### Level 6: Opt-Out (Debugging Only)

**Disable encryption for debugging**:

```bash
# Server
./ascii-chat-server --no-encrypt

# Client
./ascii-chat-client --no-encrypt
```

**Security properties**:
- ❌ No protection
- ⚠️ Anyone on the network can see everything

**Use case**: "I'm debugging and need to see raw packets with tcpdump"

## CLI Flag Behavior

```bash
# Default: Encrypted (DH only)
./ascii-chat-server              # Encrypted, displays public key
./ascii-chat-client              # Encrypted, displays public key

# Password authentication
./ascii-chat-server --key pass   # Encrypted + password auth
./ascii-chat-client --key pass   # Must match server password

# Keyfile: Password from file
./ascii-chat-server --keyfile /path/to/keyfile
./ascii-chat-client --keyfile /path/to/keyfile

# Public key pinning (client verifies server)
./ascii-chat-client --server-key 3a7f2c1b9e4d8a6f...

# Client whitelist (server restricts clients)
./ascii-chat-server --client-keys /path/to/keys.txt
./ascii-chat-server --client-keys "key1,key2,key3"

# Combinations
./ascii-chat-server --key pass --client-keys keys.txt
./ascii-chat-client --key pass --server-key 3a7f...

# Opt-out: Unencrypted
./ascii-chat-server --no-encrypt  # Plain TCP
./ascii-chat-client --no-encrypt  # Plain TCP
```

**Flag implications**:
- `--key PASSWORD` → Encryption enabled with password auth
- `--keyfile FILE` → Encryption enabled with password auth (password from file)
- `--server-key HEXSTRING` → Client verifies server public key (MITM protection)
- `--client-keys LIST_OR_FILE` → Server restricts to whitelisted clients
- `--no-encrypt` → Encryption disabled
- No flags → Encryption enabled (DH only)

**Validation**:
- `--key` and `--keyfile` are mutually exclusive
- If server uses `--key`/`--keyfile`, client MUST provide matching password (or connection rejected)
- If client provides password but server doesn't → connection rejected
- If client provides `--server-key` but keys don't match → connection aborted with MITM warning
- If server has `--client-keys` and client key not in list → connection rejected
- `--no-encrypt` on one side and encryption on other → connection rejected

## The MITM Problem (And Why We Have Multiple Solutions)

**The fundamental security tradeoff**:

There is **no way** to prevent MITM attacks without sharing a secret through a separate channel. This isn't a bug, it's mathematics:

1. **Server and client connect over the internet** (untrusted channel)
2. **They've never met before** (no prior shared secret)
3. **Attacker controls the network** (can intercept/modify packets)
4. **Question**: How do they establish a secure channel?

**Answer**: They can't! Not without one of:
- Pre-shared secret (password) → **`--key`**
- Pre-shared public keys (SSH model) → **`--server-key` / `--client-keys`**
- Trusted third party (certificate authority like HTTPS)
- Out-of-band verification (QR codes, safety numbers)

**What we provide**:
- **Default mode**: Accepts this tradeoff (privacy, not security)
- **Password mode**: Use `--key` (like WiFi passwords)
- **Key pinning mode**: Use `--server-key` (like SSH)
- **Whitelist mode**: Use `--client-keys` (like SSH authorized_keys)
- **Combined mode**: Use all of the above (defense in depth)

**The vibe**: "Encryption by default protects against passive attacks. Add passwords or key pinning if you need protection against active attacks too."

## Security Properties Summary

| Mode | Passive Eavesdrop | MITM Protection | Server Whitelist | Forward Secrecy |
|------|-------------------|-----------------|------------------|-----------------|
| Default | ✅ | ❌ | ❌ | ✅ |
| `--key` | ✅ | ✅ | ❌ | ✅ |
| `--server-key` (client) | ✅ | ✅ (client protected) | ❌ | ✅ |
| `--client-keys` (server) | ✅ | ✅ (server protected) | ✅ | ✅ |
| `--key` + `--server-key` | ✅ | ✅✅ | ❌ | ✅ |
| `--key` + `--client-keys` | ✅ | ✅✅ | ✅ | ✅ |
| All combined | ✅ | ✅✅✅ | ✅ | ✅ |
| `--no-encrypt` | ❌ | ❌ | ❌ | N/A |

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

### Phase 1: CLI Flags & In-Memory State (2.5 hours)

**Goal**: Parse flags, store crypto config in memory, initialize libsodium, display keys

#### 1.1 Update options.c/h

```c
typedef struct {
    // Existing fields...

    // Crypto options
    bool no_encrypt;        // Disable encryption (opt-out)
    char* key;              // Password for authentication
    char* keyfile;          // Path to file containing password
    char* server_key;       // Expected server public key (hex string) - CLIENT ONLY
    char* client_keys;      // Allowed client keys (comma-separated or filepath) - SERVER ONLY
} options_t;
```

**Parsing logic**:
- Default: encryption enabled (if `!no_encrypt && !key && !keyfile`)
- If `--no-encrypt`: encryption disabled
- If `--key` or `--keyfile`: encryption enabled with auth
- Validate: `--key` and `--keyfile` are mutually exclusive
- Validate: `--no-encrypt` cannot be used with `--key`/`--keyfile`/`--server-key`/`--client-keys`

**Helper functions**:

```c
// Read password from file
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

// Convert binary to hex string
static void hex_encode(const uint8_t* data, size_t len, char* out) {
    const char* hex_chars = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i*2] = hex_chars[data[i] >> 4];
        out[i*2 + 1] = hex_chars[data[i] & 0x0F];
    }
    out[len*2] = '\0';
}

// Convert hex string to binary
static int hex_decode(const char* hex, uint8_t* out, size_t out_len) {
    size_t hex_len = strlen(hex);
    if (hex_len != out_len * 2) {
        log_error("Invalid hex string length: expected %zu chars, got %zu", out_len * 2, hex_len);
        return -1;
    }

    for (size_t i = 0; i < out_len; i++) {
        char high = hex[i*2];
        char low = hex[i*2 + 1];

        uint8_t h = (high >= '0' && high <= '9') ? (high - '0') :
                    (high >= 'a' && high <= 'f') ? (high - 'a' + 10) :
                    (high >= 'A' && high <= 'F') ? (high - 'A' + 10) : 0;
        uint8_t l = (low >= '0' && low <= '9') ? (low - '0') :
                    (low >= 'a' && low <= 'f') ? (low - 'a' + 10) :
                    (low >= 'A' && low <= 'F') ? (low - 'A' + 10) : 0;

        out[i] = (h << 4) | l;
    }

    return 0;
}

// Parse --client-keys (filepath or comma-separated list)
typedef struct {
    uint8_t keys[MAX_CLIENTS][32];
    size_t num_keys;
} client_whitelist_t;

static void parse_client_keys(const char* input, client_whitelist_t* whitelist) {
    whitelist->num_keys = 0;

    // Check if input is a filepath (file exists)
    FILE* f = fopen(input, "r");
    if (f != NULL) {
        // Read keys from file (one per line)
        char line[128];
        while (fgets(line, sizeof(line), f) && whitelist->num_keys < MAX_CLIENTS) {
            // Skip comments and empty lines
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

            // Trim whitespace
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
                line[--len] = '\0';
            }

            // Skip if line is now empty
            if (len == 0) continue;

            // Decode hex key
            if (hex_decode(line, whitelist->keys[whitelist->num_keys], 32) == 0) {
                whitelist->num_keys++;
            } else {
                log_warn("Invalid hex key in file (skipping): %s", line);
            }
        }
        fclose(f);
        log_info("Loaded %zu client keys from file: %s", whitelist->num_keys, input);
    } else {
        // Treat as comma-separated list
        char* input_copy = strdup(input);
        char* token = strtok(input_copy, ",");

        while (token != NULL && whitelist->num_keys < MAX_CLIENTS) {
            // Trim whitespace from token
            while (*token == ' ') token++;
            char* end = token + strlen(token) - 1;
            while (end > token && *end == ' ') *end-- = '\0';

            // Decode hex key
            if (hex_decode(token, whitelist->keys[whitelist->num_keys], 32) == 0) {
                whitelist->num_keys++;
            } else {
                log_warn("Invalid hex key in list (skipping): %s", token);
            }

            token = strtok(NULL, ",");
        }
        free(input_copy);
        log_info("Loaded %zu client keys from command line", whitelist->num_keys);
    }

    if (whitelist->num_keys == 0) {
        log_error("No valid client keys found in: %s", input);
        exit(1);
    }
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

// Client whitelist (global)
client_whitelist_t g_client_whitelist = {0};

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

    // Display server public key prominently
    char pubkey_hex[65];
    hex_encode(g_crypto_state.server_public_key, 32, pubkey_hex);

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  SERVER PUBLIC KEY                                             ║\n");
    printf("║  %-62s║\n", pubkey_hex);
    printf("║                                                                ║\n");
    printf("║  Share with clients for MITM protection:                       ║\n");
    printf("║    ascii-chat-client --server-key %-28s║\n", pubkey_hex);
    printf("╔════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Load client whitelist if provided
    if (options.client_keys) {
        parse_client_keys(options.client_keys, &g_client_whitelist);
        log_info("Server will only accept %zu whitelisted clients", g_client_whitelist.num_keys);
    }

    // Password authentication
    if (options.key || options.keyfile) {
        const char* password = options.key ? options.key : read_keyfile(options.keyfile);
        crypto_derive_key_from_password(password, g_crypto_state.password_key);
        g_crypto_state.require_auth = true;

        log_info("Encryption: ENABLED (password authenticated)");
        log_info("Clients must connect with matching password");

        // Security: Zero password after hashing
        if (options.keyfile) {
            memset((void*)password, 0, strlen(password));
            free((void*)password);
        }
    } else {
        // Default encrypted mode (DH only)
        log_info("Encryption: ENABLED (unauthenticated)");
        log_warn("No password - vulnerable to MITM attacks");
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
    uint8_t expected_server_key[32]; // Expected server public key (if --server-key provided)
    bool verify_server_key;          // True if --server-key provided
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

    // Generate ephemeral client keypair
    crypto_generate_keypair(g_crypto_state.client_public_key,
                           g_crypto_state.client_private_key);

    // Display client public key
    char pubkey_hex[65];
    hex_encode(g_crypto_state.client_public_key, 32, pubkey_hex);
    log_info("Client public key: %s", pubkey_hex);
    log_info("(share with server operator to be added to whitelist)");

    // Load expected server key if provided
    if (options.server_key) {
        if (hex_decode(options.server_key, g_crypto_state.expected_server_key, 32) != 0) {
            log_error("Invalid --server-key format (expected 64 hex chars)");
            exit(1);
        }
        g_crypto_state.verify_server_key = true;
        log_info("Will verify server key: %s", options.server_key);
    }

    // Password authentication
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
        log_info("Encryption: ENABLED (no password authentication)");
    }

    // Note: Keypair already generated above (ephemeral)
} else {
    log_info("Encryption: DISABLED");
}
```

### Phase 2: Key Exchange Protocol (3.5 hours)

**Goal**: Establish shared secret using Diffie-Hellman key exchange, verify public keys

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
    PACKET_TYPE_AUTH_FAILED = 19,            // Server -> Client: "authentication failed"
} packet_type_t;
```

#### 2.2 Handshake Flow

**Case 1: Both sides have encryption enabled (DH only, no password, no key pinning)**

```
Client connects -> TCP established

1. Server -> Client: KEY_EXCHANGE_INIT
   Payload: {server_public_key[32]}

2. Client receives, verifies server key (if --server-key provided):
   if (options.server_key && memcmp(received_key, expected_key, 32) != 0):
       Display MITM warning and EXIT

3. Client computes shared secret:
   crypto_compute_shared_secret(client_private_key, server_public_key, &shared_secret)

4. Client -> Server: KEY_EXCHANGE_RESPONSE
   Payload: {client_public_key[32]}

5. Server receives, verifies client key (if --client-keys provided):
   if (client_whitelist enabled && key not in whitelist):
       Log unauthorized attempt and DISCONNECT

6. Server computes shared secret:
   crypto_compute_shared_secret(server_private_key, client_public_key, &shared_secret)
   Store in client_t->shared_secret[32]

7. Server -> Client: HANDSHAKE_COMPLETE

All future packets are now encrypted with shared_secret
```

**Case 2: Server has password, client has matching password**

```
(Steps 1-6 same as above)

7. Server -> Client: AUTH_CHALLENGE
   Payload: {random_nonce[32]}

8. Client computes proof:
   proof = HMAC-SHA256(password_key, nonce)

9. Client -> Server: AUTH_RESPONSE
   Payload: {proof[32]}

10. Server verifies:
   expected_proof = HMAC-SHA256(password_key, nonce)
   if (memcmp(proof, expected_proof, 32) == 0):
       Send HANDSHAKE_COMPLETE
   else:
       log_error("Password authentication failed")
       Send AUTH_FAILED
       Disconnect client

11. Server -> Client: HANDSHAKE_COMPLETE

All future packets are now encrypted with shared_secret
```

**Case 3: Client verifies server key (MITM protection)**

```
(During step 2)

Client receives KEY_EXCHANGE_INIT with server_public_key[32]

if (g_crypto_state.verify_server_key):
    if (memcmp(server_public_key, g_crypto_state.expected_server_key, 32) != 0):
        // MITM ATTACK DETECTED!

        char expected_hex[65], received_hex[65];
        hex_encode(g_crypto_state.expected_server_key, 32, expected_hex);
        hex_encode(server_public_key, 32, received_hex);

        fprintf(stderr, "\n");
        fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
        fprintf(stderr, "@    WARNING: SERVER KEY MISMATCH - POSSIBLE MITM ATTACK! @\n");
        fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Expected server key: %s\n", expected_hex);
        fprintf(stderr, "Received server key: %s\n", received_hex);
        fprintf(stderr, "\n");
        fprintf(stderr, "Someone may be intercepting your connection!\n");
        fprintf(stderr, "Connection ABORTED for security.\n");
        fprintf(stderr, "\n");

        exit(1);  // ABORT - do not continue

    log_info("✓ Server key verified (MITM protection active)");
```

**Case 4: Server checks client whitelist**

```
(During step 5)

Server receives KEY_EXCHANGE_RESPONSE with client_public_key[32]

if (g_client_whitelist.num_keys > 0):
    bool authorized = false;

    for (size_t i = 0; i < g_client_whitelist.num_keys; i++):
        if (memcmp(client_public_key, g_client_whitelist.keys[i], 32) == 0):
            authorized = true;
            break;

    if (!authorized):
        char client_key_hex[65];
        hex_encode(client_public_key, 32, client_key_hex);
        log_warn("Unauthorized client key: %s", client_key_hex);
        log_warn("Client not in whitelist - disconnecting");

        // Send AUTH_FAILED and disconnect
        send_packet(sockfd, PACKET_TYPE_AUTH_FAILED, 0, NULL, 0);
        return -1;  // Disconnect

    log_info("✓ Client key authorized (whitelist verified)");
```

**Case 5: Encryption mismatch**

```
Server encrypted, client --no-encrypt:
- Client sends unencrypted packet
- Server expects KEY_EXCHANGE_RESPONSE after sending KEY_EXCHANGE_INIT
- Server disconnects after timeout (10 seconds)

Server --no-encrypt, client encrypted:
- Server sends unencrypted application packet (e.g., CLIENT_JOIN)
- Client expects KEY_EXCHANGE_INIT
- Client disconnects with error: "Server does not support encryption"
```

**Case 6: Password mismatch**

```
Server --key foo, client --key bar:
- Steps 1-9 complete
- Step 10: Server verifies HMAC, fails
- Server sends AUTH_FAILED and disconnects
- Client logs: "Password authentication rejected by server"

Server no password, client --key foo:
- Server completes handshake at step 7 (no AUTH_CHALLENGE)
- Client expects AUTH_CHALLENGE, doesn't get it
- Client receives HANDSHAKE_COMPLETE
- Client disconnects: "Server does not require password (expected authentication)"
```

#### 2.3 Per-Client Encryption State (server)

Update `client_t` struct in server:
```c
typedef struct client_t {
    // Existing fields...

    // Crypto state (one shared_secret per client)
    uint8_t shared_secret[32];
    uint8_t client_public_key[32];  // For logging/auditing
    uint64_t send_nonce;            // Increment per packet sent
    uint64_t recv_nonce;            // Last received nonce (replay protection)
    bool encryption_ready;          // True after handshake complete
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
- Perform handshake with key verification
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

1. **Both default (encrypted, no password, no keys)**
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

6. **Client with --server-key (matching)**
   - ✅ Should connect successfully
   - ✅ Log "Server key verified"

7. **Client with --server-key (mismatched)**
   - ✅ Should abort with MITM warning
   - ✅ Display expected vs received keys

8. **Server with --client-keys (authorized client)**
   - ✅ Should connect successfully
   - ✅ Log "Client key authorized"

9. **Server with --client-keys (unauthorized client)**
   - ✅ Should reject connection
   - ✅ Log unauthorized client key

10. **Both with --no-encrypt**
   - ✅ Should connect unencrypted
   - ⚠️ Log warning about plaintext

11. **Server encrypted, client --no-encrypt**
   - ✅ Should fail to connect (encryption mismatch)

12. **Multi-client with encryption**
   - ✅ Each client has unique shared_secret
   - ✅ Clients cannot decrypt each other's packets

13. **Replay attack test**
   - ✅ Resending old packet should fail

14. **Defense in depth (all features combined)**
   - ✅ Server: --key + --client-keys
   - ✅ Client: --key + --server-key
   - ✅ Should work with all protections active

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
log_crypto("Server key verified: %s", hex);
log_crypto("Client key authorized: %s", hex);
```

#### 4.3 Test Script

```bash
#!/bin/bash
# tests/crypto_test.sh

set -e

echo "=== Test 1: Default encrypted (no password, no keys) ==="
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
echo "=== Test 4: Server key verification (matching) ==="
./build/bin/ascii-chat-server --log-file /tmp/server4.log &
SERVER_PID=$!
sleep 1
# Extract server key from log
SERVER_KEY=$(grep -oP 'SERVER PUBLIC KEY.*\K[0-9a-f]{64}' /tmp/server4.log | head -1)
timeout 5 ./build/bin/ascii-chat-client --server-key "$SERVER_KEY" --snapshot --log-file /tmp/client4.log || true
kill $SERVER_PID 2>/dev/null || true
grep -q "Server key verified" /tmp/client4.log && echo "✅ Test 4 passed"

echo ""
echo "=== Test 5: Server key verification (mismatched) ==="
./build/bin/ascii-chat-server --log-file /tmp/server5.log &
SERVER_PID=$!
sleep 1
FAKE_KEY="0000000000000000000000000000000000000000000000000000000000000000"
timeout 5 ./build/bin/ascii-chat-client --server-key "$FAKE_KEY" --snapshot --log-file /tmp/client5.log 2>&1 | grep -q "MITM" && echo "✅ Test 5 passed" || echo "❌ Test 5 failed"
kill $SERVER_PID 2>/dev/null || true

echo ""
echo "=== Test 6: Client whitelist (authorized) ==="
# Generate client key first
./build/bin/ascii-chat-client --help > /dev/null 2>&1  # Just to generate key
CLIENT_KEY=$(./build/bin/ascii-chat-client 2>&1 | grep -oP 'Client public key: \K[0-9a-f]{64}' | head -1)
./build/bin/ascii-chat-server --client-keys "$CLIENT_KEY" --log-file /tmp/server6.log &
SERVER_PID=$!
sleep 1
timeout 5 ./build/bin/ascii-chat-client --snapshot --log-file /tmp/client6.log || true
kill $SERVER_PID 2>/dev/null || true
grep -q "Client key authorized" /tmp/server6.log && echo "✅ Test 6 passed"

echo ""
echo "=== Test 7: Unencrypted mode ==="
./build/bin/ascii-chat-server --no-encrypt --log-file /tmp/server7.log &
SERVER_PID=$!
sleep 1
timeout 5 ./build/bin/ascii-chat-client --no-encrypt --snapshot --log-file /tmp/client7.log || true
kill $SERVER_PID 2>/dev/null || true
grep -q "DISABLED" /tmp/server7.log && echo "✅ Test 7 passed"

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

### Security Levels (Choose What You Need)

#### Level 1: Default Encrypted (Privacy)

```bash
# Server
./bin/ascii-chat-server

# Client
./bin/ascii-chat-client
```

**Privacy from passive eavesdropping**: Your ISP, coffee shop WiFi, or network admin cannot see your video chat. However, without a password or key pinning, an active attacker with network access could perform a man-in-the-middle attack.

**Server displays public key on startup** - you can share this with clients for MITM protection (see Level 3).

#### Level 2: Password Authentication (Security)

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

#### Level 3: Public Key Pinning (Strong Security)

```bash
# Server displays on startup:
# ╔════════════════════════════════════════════════════════════════╗
# ║  SERVER PUBLIC KEY                                             ║
# ║  3a7f2c1b9e4d8a6f5c3b7e9d2f8a4c6e1b5d9a3f7c2e8b4d6a1f5c3b7  ║
# ╚════════════════════════════════════════════════════════════════╝

# Client verifies server key (SSH-like security):
./bin/ascii-chat-client --server-key 3a7f2c1b9e4d8a6f5c3b7e9d2f8a4c6e1b5d9a3f7c2e8b4d6a1f5c3b7
```

**Cryptographic MITM protection**: Client verifies server's public key. If keys don't match, connection is aborted with a warning. Share the server's public key via email, Slack, or any channel (public keys are not secret!).

#### Level 4: Server Whitelist (Restricted Access)

```bash
# Client displays their public key on startup - send to server operator
# Client public key: f9e2d4c6b8a175392e4d8f1c6a3b7e5d9c2f8a4e6b1d5c9a3f7e2b4d

# Server accepts only whitelisted clients:
./bin/ascii-chat-server --client-keys /path/to/allowed_keys.txt

# allowed_keys.txt format:
# 3a7f2c1b9e4d8a6f5c3b7e9d2f8a4c6e1b5d9a3f7c2e8b4d6a1f5c3b7  # Alice
# f9e2d4c6b8a175392e4d8f1c6a3b7e5d9c2f8a4e6b1d5c9a3f7e2b4d  # Bob

# Or comma-separated list:
./bin/ascii-chat-server --client-keys "3a7f2c...,f9e2d4...,b8c1e9..."
```

**Private server**: Only clients with whitelisted public keys can connect. Server logs all connection attempts for auditing.

#### Level 5: Defense in Depth (Maximum Security)

```bash
# Combine password + key pinning + whitelist:
./bin/ascii-chat-server --key mypass --client-keys allowed_keys.txt
./bin/ascii-chat-client --key mypass --server-key 3a7f2c1b...
```

**Layered security**: Password authentication + public key verification + client whitelist. For paranoid security requirements.

### Opt-Out: Unencrypted (Testing Only)

```bash
./bin/ascii-chat-server --no-encrypt
./bin/ascii-chat-client --no-encrypt
```

**No protection**: All traffic sent in plaintext. Only use for debugging or testing.

### Security Notes

- **Default encryption**: Protects against passive attacks (ISP snooping, WiFi eavesdropping)
- **Password authentication**: Protects against active attacks (MITM)
- **Public key pinning**: Cryptographic MITM protection (like SSH)
- **Client whitelist**: Server access control
- **Forward secrecy**: Each connection uses fresh keys (compromising one session doesn't affect others)
- **Per-client isolation**: Each client has a unique shared secret
- **Full packet encryption**: Headers and payloads encrypted (only 4-byte magic number visible)
- **Replay protection**: Nonce counters prevent attackers from resending old packets

### Why Can't We Prevent MITM Without Shared Secrets?

This isn't a flaw in ASCII-Chat - it's a fundamental limitation of cryptography. If two parties have never met before and communicate over an untrusted channel, there's no way to authenticate the connection without:

1. A pre-shared secret (password) → **`--key`**
2. Pre-shared public keys (SSH model) → **`--server-key` / `--client-keys`**
3. A trusted third party (certificate authority like HTTPS uses)
4. Out-of-band verification (QR codes, safety numbers, etc.)

ASCII-Chat provides options #1 and #2 because they're simple and users already understand the model from WiFi passwords, SSH, Signal safety numbers, and Bluetooth pairing codes.

**The bottom line**: Default encryption protects your privacy. Add passwords or key pinning if you need security.
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
- `--client-keys LIST_OR_FILE`: Whitelist of allowed client public keys (comma-separated or filepath)
- `--no-encrypt`: Disable encryption (plaintext mode)
- `-h --help`: Show help message

### Client Options

- `-a --address ADDRESS`: IPv4 address to connect to (default: 0.0.0.0)
- `-p --port PORT`: TCP port (default: 27224)
- [... other options ...]
- `--key PASSWORD`: Connect with password authentication
- `--keyfile FILE`: Read password from file
- `--server-key HEXSTRING`: Expected server public key (64 hex chars, enables MITM protection)
- `--no-encrypt`: Disable encryption (must match server)
- `-h --help`: Show help message
```

#### 5.3 Update CLAUDE.md

Add to "Recent Updates" section:

```markdown
### Cryptography Implementation (October 2025)
- **Encrypted by default**: All connections use X25519 + XSalsa20-Poly1305
- **Progressive security ladder**: 5 levels from basic privacy to maximum security
- **Password authentication**: `--key` flag prevents MITM attacks
- **Public key pinning**: `--server-key` for SSH-like client verification
- **Server whitelist**: `--client-keys` for access control
- **Full packet encryption**: Headers and payloads encrypted (minimal metadata leakage)
- **Forward secrecy**: Ephemeral keypairs for each connection
- **Per-client isolation**: Each client has unique shared secret
- **Replay protection**: Nonce counters prevent packet replay
```

## Implementation Checklist

### Phase 1: Foundation
- [ ] Add `no_encrypt`, `key`, `keyfile`, `server_key`, `client_keys` fields to options_t
- [ ] Parse `--no-encrypt`, `--key PASSWORD`, `--keyfile FILE` in options.c
- [ ] Parse `--server-key HEXSTRING` (client only) in options.c
- [ ] Parse `--client-keys LIST_OR_FILE` (server only) in options.c
- [ ] Implement `read_keyfile()` helper function
- [ ] Implement `hex_encode()` helper function
- [ ] Implement `hex_decode()` helper function
- [ ] Implement `parse_client_keys()` helper function (file or comma-separated)
- [ ] Validate: `--key` and `--keyfile` mutually exclusive
- [ ] Validate: `--no-encrypt` incompatible with other crypto flags
- [ ] Add `server_crypto_state_t` to server
- [ ] Add `client_whitelist_t` to server (global)
- [ ] Add `client_crypto_state_t` to client
- [ ] Initialize libsodium with `crypto_init()`
- [ ] Generate ephemeral keypairs on startup (if encryption enabled)
- [ ] Display server public key prominently on startup
- [ ] Display client public key on startup
- [ ] Load client whitelist if `--client-keys` provided
- [ ] Load expected server key if `--server-key` provided
- [ ] Derive password key with Argon2id (if password provided)

### Phase 2: Handshake
- [ ] Add 6 new packet types to network.h (KEY_EXCHANGE_*, AUTH_*, HANDSHAKE_COMPLETE, AUTH_FAILED)
- [ ] Implement server: send KEY_EXCHANGE_INIT on client connect
- [ ] Implement client: handle KEY_EXCHANGE_INIT, generate keypair
- [ ] Implement client: verify server key if `--server-key` provided (MITM detection)
- [ ] Implement client: compute shared_secret, send KEY_EXCHANGE_RESPONSE
- [ ] Implement server: handle KEY_EXCHANGE_RESPONSE
- [ ] Implement server: verify client key against whitelist if `--client-keys` provided
- [ ] Implement server: compute shared_secret
- [ ] Implement server: send AUTH_CHALLENGE (if password mode)
- [ ] Implement client: handle AUTH_CHALLENGE, compute HMAC proof
- [ ] Implement client: send AUTH_RESPONSE
- [ ] Implement server: verify AUTH_RESPONSE, disconnect if wrong
- [ ] Implement server: send HANDSHAKE_COMPLETE or AUTH_FAILED
- [ ] Implement client: handle HANDSHAKE_COMPLETE
- [ ] Implement client: handle AUTH_FAILED
- [ ] Add `shared_secret[32]` and `client_public_key[32]` to `client_t` struct
- [ ] Add `encryption_ready` flag to track handshake completion
- [ ] Log all key verifications (success and failure)

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
- [ ] Test: Client --server-key matching (should work, log verified)
- [ ] Test: Client --server-key mismatched (should abort with MITM warning)
- [ ] Test: Server --client-keys authorized (should work, log authorized)
- [ ] Test: Server --client-keys unauthorized (should reject)
- [ ] Test: Both --no-encrypt (should work, log plaintext warning)
- [ ] Test: Server encrypted, client --no-encrypt (should fail)
- [ ] Test: Multi-client encrypted session
- [ ] Test: Replay attack protection
- [ ] Test: Defense in depth (all flags combined)
- [ ] Add DEBUG_CRYPTO logging
- [ ] Create tests/crypto_test.sh script

### Phase 5: Documentation
- [ ] Update README.md Cryptography section (remove "NOT YET IMPLEMENTED")
- [ ] Update README.md command line flags
- [ ] Update CLAUDE.md with crypto implementation notes
- [ ] Remove old "NOT YET IMPLEMENTED" warnings from code
- [ ] Add comments explaining handshake flow
- [ ] Document `--client-keys` parsing (file vs comma-separated)

## Timeline Estimate

| Phase | Time | Cumulative |
|-------|------|------------|
| Phase 1: Foundation + Key Display | 2.5 hours | 2.5 hours |
| Phase 2: Handshake + Key Verification | 3.5 hours | 6 hours |
| Phase 3: Encryption | 3 hours | 9 hours |
| Phase 4: Testing (expanded) | 2 hours | 11 hours |
| Phase 5: Documentation | 1 hour | 12 hours |

**Total**: 12 hours for complete implementation and testing (including public key pinning features)

## Success Criteria

- [ ] Default mode: Server and client connect with DH encryption (no password)
- [ ] Server displays public key prominently on startup
- [ ] Client displays public key on startup
- [ ] Password mode: Server and client can establish encrypted connection with password
- [ ] Authentication: Server rejects clients with wrong password
- [ ] Key pinning: Client verifies server key and aborts on mismatch (MITM protection)
- [ ] Whitelist: Server restricts to whitelisted client keys
- [ ] `--client-keys` parses both filepaths and comma-separated lists
- [ ] Isolation: Multi-client sessions work, each client has unique shared_secret
- [ ] Security: Replay attacks are detected and blocked
- [ ] Defense in depth: All security features work together
- [ ] Opt-out: --no-encrypt mode works for debugging
- [ ] Quality: No memory leaks (AddressSanitizer clean)
- [ ] Docs: README updated to reflect "encrypted by default" with security levels
- [ ] Compatibility: All existing tests still pass

---

**Ready to implement! Start with Phase 1. 🔐**

**The vibe**: "Privacy by default, security when needed, paranoia when required"
