# Crypto Implementation Plan

**Date**: October 6, 2025
**Status**: Ready to implement
**Estimated Time**: 10-14 hours total

## Philosophy: Privacy by Default, Leverage Existing Infrastructure

> "Encrypted by default. Use your existing SSH keys. Verify servers like you verify GitHub."

## Overview

Implement end-to-end encryption for ASCII-Chat using the existing lib/crypto.c code (533 lines already written). **Encryption is enabled by default** using X25519 Diffie-Hellman key exchange, with optional password authentication and **SSH key pinning** for MITM protection.

**Key Innovation**: Use existing SSH/GPG keys (Ed25519 or direct X25519) and leverage GitHub/GitLab for public key distribution. No custom key formats, no new tools to learn. **Modern crypto only - RSA is considered legacy.**

### Supported Key Types

1. **Ed25519** (ssh-ed25519) ⭐ **RECOMMENDED**
   - Direct conversion to X25519 via libsodium
   - Modern, fast, secure (Curve25519)
   - Default in OpenSSH since 2014
   - 32-byte keys, constant-time operations

2. **X25519** (raw hex or base64)
   - Direct use, no conversion needed
   - For users who generate keys with libsodium directly
   - Same crypto as Ed25519 (Curve25519)

3. **GPG Keys** (Ed25519 variant only) - **OPTIONAL**
   - Fetch from github.com/username.gpg or gitlab.com/username.gpg
   - Shell out to `gpg` command for key export
   - Supports pinentry for passphrase prompts
   - GPG agent caching works
   - **Graceful fallback**: If `gpg` not installed, other auth methods still work

### Deprecated: RSA Keys

**RSA is NOT supported.** Here's why:

- **Cannot convert to X25519** (different mathematical foundations)
- **Would require hybrid crypto** (RSA wraps X25519 session keys)
- **Large dependency** (OpenSSL ~3MB just for RSA operations)
- **Slower** (2048-bit RSA vs 256-bit Ed25519)
- **Legacy crypto** (designed in 1977, Curve25519 is 2005)

**If you have RSA keys:** Regenerate with Ed25519. It's 2025, time to upgrade.

```bash
# Generate new Ed25519 key
ssh-keygen -t ed25519 -C "your_email@example.com"
```

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
- Server displays its public key in SSH format (user can ignore or share)
- Each connection gets fresh keypairs (forward secrecy)

**Server displays on startup:**
```
╔════════════════════════════════════════════════════════════════╗
║  SERVER PUBLIC KEY                                             ║
║  ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo... ascii-chat-server ║
║                                                                ║
║  Verify with:                                                  ║
║    ascii-chat-client --server-key <paste-key-above>            ║
║  Or save to known_hosts:                                       ║
║    echo "hostname ssh-ed25519 AAAA..." >> ~/.ascii-chat/known_hosts║
╚════════════════════════════════════════════════════════════════╝
```

**Security properties**:
- ✅ Protects against passive eavesdropping (ISP, WiFi snooping)
- ✅ Forward secrecy (compromising one session doesn't affect others)
- ✅ Full packet encryption (headers + payloads)
- ⚠️ Vulnerable to active MITM attacks (attacker can intercept DH exchange)

**Use case**: "I don't want my coffee shop WiFi admin watching my video chat"

### Level 2: Password or GPG Key Authentication (Security)

**Recommended for most users**:

```bash
# Server with password
./ascii-chat-server --key mypassword

# Client with password
./ascii-chat-client --key mypassword

# Or with keyfile
echo "mypassword" > /tmp/keyfile
./ascii-chat-server --keyfile /tmp/keyfile
./ascii-chat-client --keyfile /tmp/keyfile

# Or with GPG key (OPTIONAL - requires gpg installed)
./ascii-chat-server --key gpg:0xABCD1234
./ascii-chat-client --key gpg:0xABCD1234

# GPG key by fingerprint
./ascii-chat-server --key gpg:1234567890ABCDEF1234567890ABCDEF12345678
```

**What happens**:

**With password**:
- DH key exchange + password verification
- Server sends random challenge, client proves knowledge of password
- Connection rejected if passwords don't match
- **Works everywhere** - no external dependencies

**With GPG key** (`--key gpg:...`) - **OPTIONAL**:
- Shell out to `gpg --export` to get key material
- GPG prompts for passphrase via pinentry (native GPG UI)
- Derive shared secret from GPG key material
- Use same challenge/response as password mode
- GPG agent can cache passphrase (like SSH agent)
- **Graceful fallback**: If `gpg` not found, error with helpful message:
  ```
  ERROR: GPG key requested but 'gpg' command not found
  Install GPG: apt-get install gnupg
  Or use password auth: --key mypassword
  ```

**Security properties**:
- ✅ Protects against passive eavesdropping
- ✅ Protects against active MITM attacks (attacker can't prove password knowledge)
- ✅ Forward secrecy
- ✅ Each client has unique shared secret

**Use case**: "I need actual security, not just privacy"

**How to share the password**: Text it to your friend, Signal message, phone call, in person, etc. This is the same model as WiFi passwords or Signal safety numbers.

### Level 3: SSH Key Pinning (Strong Security)

**Leverage your existing SSH keys!**

```bash
# Server uses existing SSH key
./ascii-chat-server --ssh-key ~/.ssh/id_ed25519

# Server displays on startup:
# ╔════════════════════════════════════════════════════════════════╗
# ║  SERVER PUBLIC KEY                                             ║
# ║  ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo... zack@ascii-chat  ║
# ║                                                                ║
# ║  Verify with GitHub:                                           ║
# ║    ascii-chat-client --server-key github:zfogg                 ║
# ║  Or paste directly:                                            ║
# ║    ascii-chat-client --server-key "ssh-ed25519 AAAAC3..."      ║
# ╚════════════════════════════════════════════════════════════════╝

# Client verifies using GitHub (automatic fetch!)
./ascii-chat-client --server-key github:zfogg

# Or paste SSH public key directly
./ascii-chat-client --server-key "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5..."

# Or use SSH known_hosts file (just like SSH does!)
echo "192.168.1.100 ssh-ed25519 AAAAC3..." >> ~/.ascii-chat/known_hosts
./ascii-chat-client  # Automatically verifies against known_hosts
```

**What happens**:
- Server uses existing SSH Ed25519 key (converted to X25519 for DH)
- Client receives server's public key during handshake
- Client verifies against:
  1. `--server-key` argument (if provided)
  2. `~/.ascii-chat/known_hosts` file (if exists)
  3. GitHub API if `github:username` format
- If mismatch: Displays scary warning and ABORTS connection
- If match: Continues with encrypted session
- On first connection: Prompts to save to known_hosts

**Security properties**:
- ✅ Protects against passive eavesdropping
- ✅ Protects against active MITM attacks (cryptographically verified)
- ✅ Forward secrecy
- ✅ No shared password needed
- ✅ SSH keys can be shared openly (not secret like passwords)
- ✅ Social proof via GitHub profile

**Use case**: "Want SSH-like security, already have SSH keys"

**How it works**:
- **Ed25519 keys**: Converted to X25519 using libsodium's `crypto_sign_ed25519_pk_to_curve25519()`
- **X25519 keys**: Used directly, no conversion needed
- **GPG keys**: Shell out to `gpg --export`, hash output to derive X25519 key material

### Level 4: Server Whitelist (Restricted Access)

**Server only accepts specific clients**:

```bash
# Client displays their SSH public key on startup
# Client public key: ssh-ed25519 AAAAC3NzaC1lZDI1NTE5... alice@laptop

# Server whitelist using GitHub usernames (fetches keys automatically!)
./ascii-chat-server --client-keys github:alice,github:bob,github:carol

# Or use SSH authorized_keys format (everyone knows this!)
./ascii-chat-server --client-keys ~/.ssh/authorized_keys

# authorized_keys format (standard SSH):
# ssh-ed25519 AAAAC3NzaC1lZDI1NTE5... alice@laptop
# ssh-ed25519 AAAAB3NzaC1yc2EAAAADAQABAAABAQC... bob@desktop
# # Comments start with #

# Or mix formats:
./ascii-chat-server --client-keys "github:alice,ssh-ed25519 AAAAC3...,github:bob"

# Or paste SSH public keys directly
./ascii-chat-server --client-keys "ssh-ed25519 AAAAC3...,ssh-ed25519 AAAAB3..."
```

**What happens**:
- Server loads whitelist of allowed client public keys
- Supports multiple formats:
  - `github:username` - Fetches from `https://github.com/username.keys`
  - `gitlab:username` - Fetches from `https://gitlab.com/username.keys`
  - File path (tries SSH authorized_keys format first, then custom format)
  - Comma-separated SSH public keys
  - Comma-separated GitHub usernames
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
1. Client runs: `./ascii-chat-client` (displays their SSH public key)
2. Client sends key to server operator (via email/Signal/etc.)
3. Server operator adds to authorized_keys or uses GitHub username
4. Client can now connect

### Level 5: Defense in Depth (Maximum Security)

**Combine all security features**:

```bash
# Server: Password + SSH key + client whitelist
./ascii-chat-server --ssh-key ~/.ssh/id_ed25519 --key mypassword --client-keys ~/.ssh/authorized_keys

# Client: Password + server key verification
./ascii-chat-client --key mypassword --server-key github:zfogg
```

**Security properties**:
- ✅ Password authentication (both sides verify password)
- ✅ SSH key pinning (client verifies server identity)
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
# Default: Encrypted (ephemeral DH)
./ascii-chat-server              # Encrypted, displays ephemeral SSH public key
./ascii-chat-client              # Encrypted, displays ephemeral SSH public key

# Use existing SSH key
./ascii-chat-server --ssh-key ~/.ssh/id_ed25519
./ascii-chat-server --ssh-key ~/.ssh/id_ed25519.pub  # Also works (public key only)

# Password authentication
./ascii-chat-server --key pass   # Encrypted + password auth
./ascii-chat-client --key pass   # Must match server password

# Keyfile: Password from file
./ascii-chat-server --keyfile /path/to/keyfile
./ascii-chat-client --keyfile /path/to/keyfile

# SSH key pinning (client verifies server)
./ascii-chat-client --server-key github:zfogg
./ascii-chat-client --server-key "ssh-ed25519 AAAAC3..."
./ascii-chat-client --server-key ~/.ssh/known_hosts  # (if we support this)

# Client whitelist (server restricts clients)
./ascii-chat-server --client-keys github:alice,github:bob
./ascii-chat-server --client-keys ~/.ssh/authorized_keys
./ascii-chat-server --client-keys "ssh-ed25519 AAAA...,github:bob"

# Combinations
./ascii-chat-server --ssh-key ~/.ssh/id_ed25519 --key pass --client-keys github:alice,github:bob
./ascii-chat-client --key pass --server-key github:zfogg

# Opt-out: Unencrypted
./ascii-chat-server --no-encrypt  # Plain TCP
./ascii-chat-client --no-encrypt  # Plain TCP
```

**Flag implications**:
- `--ssh-key FILE` → Server uses existing SSH Ed25519 key (converted to X25519)
- `--key PASSWORD` → Encryption enabled with password auth
- `--keyfile FILE` → Encryption enabled with password auth (password from file)
- `--server-key FORMAT` → Client verifies server public key (MITM protection)
  - Formats: `github:username`, `gitlab:username`, `ssh-ed25519 ...`, file path
- `--client-keys FORMAT` → Server restricts to whitelisted clients
  - Formats: `github:user1,github:user2`, file path, comma-separated SSH keys
- `--no-encrypt` → Encryption disabled
- No flags → Encryption enabled (ephemeral DH only)

**Validation**:
- `--key` and `--keyfile` are mutually exclusive
- If server uses `--key`/`--keyfile`, client MUST provide matching password (or connection rejected)
- If client provides password but server doesn't → connection rejected
- If client provides `--server-key` but keys don't match → connection aborted with MITM warning
- If server has `--client-keys` and client key not in list → connection rejected
- `--no-encrypt` on one side and encryption on other → connection rejected

## SSH Known_Hosts Integration

### Client Side: ~/.ascii-chat/known_hosts

**Format** (same as SSH known_hosts):
```
# ASCII-Chat Known Hosts
# Format: hostname key-type public-key [comment]
192.168.1.100 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo... server1
example.com ssh-ed25519 AAAAB3NzaC1yc2EAAAADAQABAAABAQC... server2
10.0.0.5 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBar... homeserver
```

**Behavior**:
1. On first connection to a server, if no `--server-key` provided:
   - Display server's public key
   - Ask: "Save to known_hosts? [yes/no]"
   - If yes: Append to `~/.ascii-chat/known_hosts`

2. On subsequent connections:
   - Check `~/.ascii-chat/known_hosts` for this hostname
   - If found: Verify server key matches
   - If match: Continue silently
   - If mismatch: **ABORT** with warning (like SSH does)

3. With `--server-key` flag:
   - Skip known_hosts check
   - Use provided key for verification

**Example warning** (if server key changes):
```
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!
Someone could be eavesdropping on you right now (man-in-the-middle attack)!

The server key for 192.168.1.100 has changed.
Expected: ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo...
Received: ssh-ed25519 AAAAB3NzaC1yc2EAAAADAQABAAABAQC...

If this is expected (server reinstalled), remove old key:
  sed -i '/192.168.1.100/d' ~/.ascii-chat/known_hosts

Connection ABORTED for security.
```

### Server Side: SSH authorized_keys Support

**Format** (same as SSH authorized_keys):
```
# ASCII-Chat Authorized Keys
# Format: key-type public-key comment
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo... alice@laptop
ssh-ed25519 AAAAB3NzaC1yc2EAAAADAQABAAABAQC... bob@desktop
# Lines starting with # are comments
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBar... carol@phone
```

**Parsing logic**:
1. Read file line by line
2. Skip lines starting with `#` (comments)
3. Skip empty lines
4. Parse `ssh-ed25519 <base64>` format
5. Convert Ed25519 → X25519
6. Add to whitelist

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
- **SSH key mode**: Use `--server-key` / `--ssh-key` (like SSH)
- **GitHub trust**: Use `github:username` (social proof + public keys)
- **Known hosts**: Track servers like SSH does (detect key changes)
- **Authorized keys**: Whitelist clients like SSH does

**The vibe**: "Encryption by default protects against passive attacks. Use SSH keys or passwords for active attack protection."

## Security Properties Summary

| Mode | Passive Eavesdrop | MITM Protection | Server Whitelist | Forward Secrecy | Uses Existing Keys |
|------|-------------------|-----------------|------------------|-----------------|-------------------|
| Default | ✅ | ❌ | ❌ | ✅ | ❌ |
| `--key` | ✅ | ✅ | ❌ | ✅ | ❌ |
| `--ssh-key` (server) | ✅ | ✅ (if client verifies) | ❌ | ✅ | ✅ |
| `--server-key github:user` | ✅ | ✅ | ❌ | ✅ | ✅ |
| `--client-keys github:users` | ✅ | ✅ (server protected) | ✅ | ✅ | ✅ |
| `--ssh-key` + `--client-keys` | ✅ | ✅✅ | ✅ | ✅ | ✅ |
| All combined | ✅ | ✅✅✅ | ✅ | ✅ | ✅ |
| `--no-encrypt` | ❌ | ❌ | ❌ | N/A | N/A |

## Existing Code (lib/crypto.c)

Already implemented and ready to use:

```c
crypto_init()                     // Initialize libsodium
crypto_generate_keypair()         // X25519 keypair generation
crypto_compute_shared_secret()    // DH key exchange
crypto_derive_key_from_password() // Argon2id password hashing
crypto_encrypt_packet()           // XSalsa20-Poly1305 encryption
crypto_decrypt_packet()           // XSalsa20-Poly1305 decryption

// NEW: libsodium includes Ed25519 → X25519 conversion!
crypto_sign_ed25519_pk_to_curve25519()  // Convert SSH key to DH key
crypto_sign_ed25519_sk_to_curve25519()  // Convert SSH private key to DH private key
```

**Note**: libsodium already has cross-platform CSPRNG (`randombytes_buf()`), so we get secure random numbers on Linux/macOS/Windows for free.

## GPG Optional Dependency Behavior

**GPG is completely optional.** All core functionality works without it.

### What Works WITHOUT GPG

✅ **All of these work perfectly:**
- Default encryption (ephemeral Ed25519 DH)
- Password authentication (`--key mypassword`)
- Keyfile authentication (`--keyfile /path/to/file`)
- SSH Ed25519 keys (`--ssh-key ~/.ssh/id_ed25519`)
- GitHub SSH keys (`--server-key github:username`)
- GitLab SSH keys (`--server-key gitlab:username`)
- Raw X25519 keys (hex format)
- SSH authorized_keys whitelist (`--client-keys ~/.ssh/authorized_keys`)

### What Requires GPG

⭕ **These features need `gpg` installed:**
- GPG key authentication (`--key gpg:0xKEYID`)
- GitHub GPG keys (`--server-key github:username.gpg`)
- GitLab GPG keys (`--server-key gitlab:username.gpg`)

### Graceful Fallback Behavior

If user requests GPG feature but `gpg` not found:
```
ERROR: GPG key requested but 'gpg' command not found
Install GPG:
  Ubuntu/Debian: apt-get install gnupg
  macOS: brew install gnupg
  Arch: pacman -S gnupg
Or use password auth: --key mypassword
Or use SSH keys: --server-key github:username
```

**Detection**: Before any GPG operation, run `is_gpg_available()`:
```c
static bool is_gpg_available(void) {
    FILE* fp = popen("gpg --version 2>/dev/null", "r");
    if (!fp) return false;
    char buf[256];
    bool found = (fgets(buf, sizeof(buf), fp) != NULL);
    pclose(fp);
    return found;
}
```

This way, ascii-chat works on minimal systems without GPG installed.

## New Dependencies

### BearSSL (for HTTPS requests to GitHub/GitLab)

**Why BearSSL?**
- **Tiny**: ~200KB static binary (vs OpenSSL's 3MB)
- **No malloc**: Uses caller-provided buffers (stack-based)
- **Paranoid**: Written by crypto expert Thomas Pornin
- **Clean**: ISC license (BSD-style), modern C
- **Explicit**: API forces you to think about what you're doing
- **Constant-time**: Resistant to timing attacks by design

**What we use it for:**
- HTTPS connections to fetch SSH/GPG keys
- Just `GET https://github.com/username.keys`
- TLS 1.2+ handshake and encrypted transport

**BearSSL Philosophy:**
- No hidden state, no malloc
- Caller provides all buffers
- Explicit context management
- Perfect for embedded systems and paranoid developers

**Basic HTTP/TLS Client Pattern:**
```c
#include <bearssl.h>

// 1. Stack buffers (no heap allocation)
unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
unsigned char response[65536];
br_sslio_context ioc;
br_ssl_client_context sc;

// 2. TCP connect
socket_t sock = connect_tcp("github.com", 443);

// 3. TLS handshake with system trust anchors
br_ssl_client_init_full(&sc, &xc, trust_anchors, num_anchors);
br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof iobuf, 1);
br_ssl_client_reset(&sc, "github.com", 0);
br_sslio_init(&ioc, &sc.eng, low_read, &sock, low_write, &sock);

// 4. Send HTTP GET
const char* request = "GET /zfogg.keys HTTP/1.1\r\nHost: github.com\r\n\r\n";
br_sslio_write_all(&ioc, request, strlen(request));
br_sslio_flush(&ioc);

// 5. Read HTTP response
size_t len = br_sslio_read(&ioc, response, sizeof response);

// 6. Parse HTTP response (skip headers, extract body)
char* body = parse_http_body(response, len);
```

**Install**:
- Linux: `apt-get install libbearssl-dev` or build from source
- macOS: `brew install bearssl` or build from source
- Windows: Build from source (CMake, quick compile)
- Or: **Vendor it** (recommended - single library, ~200KB, 3 files)

**Vendoring** (recommended):
```bash
# Add BearSSL to project
git submodule add https://www.bearssl.org/git/BearSSL deps/bearssl
cd deps/bearssl && make
```

Then in CMakeLists.txt:
```cmake
add_subdirectory(deps/bearssl)
target_link_libraries(asciichat_lib bearssl)
```

### Base64 decoder (already have via libsodium)

**Why**: SSH public keys are base64-encoded

**libsodium provides**:
```c
int sodium_base642bin(
    unsigned char * const bin,
    const size_t bin_maxlen,
    const char * const b64,
    const size_t b64_len,
    const char * const ignore,
    size_t * const bin_len,
    const char ** const b64_end,
    const int variant
);
```

Use variant: `sodium_base64_VARIANT_ORIGINAL`

## Implementation Phases

### Phase 1: SSH/GPG Key Infrastructure (3 hours)

**Goal**: Parse SSH/GPG keys (Ed25519, X25519 only), convert to X25519, support GitHub/GitLab fetching with BearSSL

#### 1.1 Create lib/keys.c/h (NEW FILE)

```c
// lib/keys.h
#ifndef SSH_KEYS_H
#define SSH_KEYS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Key type enumeration (Ed25519 and X25519 only - no RSA!)
typedef enum {
    KEY_TYPE_UNKNOWN = 0,
    KEY_TYPE_ED25519,    // ssh-ed25519 (converts to X25519)
    KEY_TYPE_X25519,     // Native X25519 (raw hex or base64)
    KEY_TYPE_GPG         // GPG key (Ed25519 variant, derived to X25519)
} key_type_t;

// Public key structure (simple - just 32 bytes!)
typedef struct {
    key_type_t type;
    uint8_t key[32];           // Always 32 bytes (Ed25519, X25519, or GPG-derived)
    char comment[256];         // Key comment/label
} public_key_t;

// Private key structure (for server --ssh-key)
typedef struct {
    key_type_t type;
    union {
        uint8_t ed25519[64];   // Ed25519 seed (32) + public key (32) = 64 bytes
        uint8_t x25519[32];    // X25519 private key (32 bytes)
    } key;
} private_key_t;

// Parse SSH/GPG public key from any format
// Formats:
//   - "ssh-ed25519 AAAAC3... comment" (SSH Ed25519)
//   - "github:username" (fetches from GitHub .keys, uses first Ed25519 key)
//   - "gitlab:username" (fetches from GitLab .keys, uses first Ed25519 key)
//   - "github:username.gpg" (fetches GPG key from GitHub)
//   - "gitlab:username.gpg" (fetches GPG key from GitLab)
//   - "gpg:0xKEYID" (shells out to `gpg --export KEYID`)
//   - File path (reads first line and parses)
//   - Raw hex (64 chars for X25519)
// Returns: 0 on success, -1 on failure
int parse_public_key(const char* input, public_key_t* key_out);

// Parse SSH private key from file
// Supports:
//   - ~/.ssh/id_ed25519 (OpenSSH Ed25519 format)
//   - Raw hex file (64 chars for X25519)
// Returns: 0 on success, -1 on failure
int parse_private_key(const char* path, private_key_t* key_out);

// Convert public key to X25519 for DH
// Ed25519 → X25519 conversion, X25519 passthrough, GPG already derived
// Returns: 0 on success, -1 on failure
int public_key_to_x25519(const public_key_t* key, uint8_t x25519_pk[32]);

// Convert private key to X25519 for DH
// Returns: 0 on success, -1 on failure
int private_key_to_x25519(const private_key_t* key, uint8_t x25519_sk[32]);

// Fetch SSH keys from GitHub using BearSSL
// GET https://github.com/username.keys
// Returns array of SSH public key strings (caller must free)
int fetch_github_keys(const char* username, char*** keys_out, size_t* num_keys);

// Fetch SSH keys from GitLab using BearSSL
// GET https://gitlab.com/username.keys
int fetch_gitlab_keys(const char* username, char*** keys_out, size_t* num_keys);

// Fetch GPG keys from GitHub using BearSSL
// GET https://github.com/username.gpg
int fetch_github_gpg_keys(const char* username, char*** keys_out, size_t* num_keys);

// Fetch GPG keys from GitLab using BearSSL
// GET https://gitlab.com/username.gpg
int fetch_gitlab_gpg_keys(const char* username, char*** keys_out, size_t* num_keys);

// Parse SSH authorized_keys file
// Returns array of public keys (Ed25519 or X25519 only)
int parse_authorized_keys(const char* path, public_key_t* keys, size_t* num_keys, size_t max_keys);

// Convert public key to display format (ssh-ed25519 or x25519 hex)
void format_public_key(const public_key_t* key, char* output, size_t output_size);

#endif
```

**Implementation**:

```c
// lib/keys.c
#include "keys.h"
#include "common.h"
#include "logging.h"
#include <sodium.h>
#include <bearssl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Base64 decode SSH key blob
static int base64_decode_ssh_key(const char* base64, size_t base64_len, uint8_t** blob_out, size_t* blob_len) {
    // Allocate max possible size
    *blob_out = SAFE_MALLOC(base64_len);

    const char* end;
    int result = sodium_base642bin(
        *blob_out, base64_len,
        base64, base64_len,
        NULL,  // ignore chars
        blob_len,
        &end,
        sodium_base64_VARIANT_ORIGINAL
    );

    if (result != 0) {
        free(*blob_out);
        return -1;
    }

    return 0;
}

// Parse SSH Ed25519 public key from "ssh-ed25519 AAAAC3..." format
static int parse_ssh_ed25519_line(const char* line, uint8_t ed25519_pk[32]) {
    // Find "ssh-ed25519 "
    const char* type_start = strstr(line, "ssh-ed25519");
    if (!type_start) return -1;

    // Skip to base64 part
    const char* base64_start = type_start + 11;  // strlen("ssh-ed25519")
    while (*base64_start == ' ' || *base64_start == '\t') base64_start++;

    // Find end of base64 (space, newline, or end of string)
    const char* base64_end = base64_start;
    while (*base64_end && *base64_end != ' ' && *base64_end != '\t' &&
           *base64_end != '\n' && *base64_end != '\r') {
        base64_end++;
    }

    size_t base64_len = base64_end - base64_start;

    // Base64 decode
    uint8_t* blob;
    size_t blob_len;
    if (base64_decode_ssh_key(base64_start, base64_len, &blob, &blob_len) != 0) {
        return -1;
    }

    // Parse SSH key blob structure:
    // [4 bytes: length of "ssh-ed25519"]
    // [11 bytes: "ssh-ed25519"]
    // [4 bytes: length of public key (32)]
    // [32 bytes: Ed25519 public key]

    if (blob_len < 4 + 11 + 4 + 32) {
        free(blob);
        return -1;
    }

    // Extract Ed25519 public key (last 32 bytes)
    memcpy(ed25519_pk, blob + blob_len - 32, 32);
    free(blob);

    return 0;
}

// Convert Ed25519 public key to X25519 for DH
static int ed25519_to_x25519_pk(const uint8_t ed25519_pk[32], uint8_t x25519_pk[32]) {
    return crypto_sign_ed25519_pk_to_curve25519(x25519_pk, ed25519_pk);
}

// Convert Ed25519 private key to X25519 for DH
static int ed25519_to_x25519_sk(const uint8_t ed25519_sk[64], uint8_t x25519_sk[32]) {
    return crypto_sign_ed25519_sk_to_curve25519(x25519_sk, ed25519_sk);
}

// Low-level socket read callback for BearSSL
static int sock_read(void* ctx, unsigned char* buf, size_t len) {
    socket_t* sock = (socket_t*)ctx;
    ssize_t n = recv(*sock, buf, len, 0);
    return (n < 0) ? -1 : (int)n;
}

// Low-level socket write callback for BearSSL
static int sock_write(void* ctx, const unsigned char* buf, size_t len) {
    socket_t* sock = (socket_t*)ctx;
    ssize_t n = send(*sock, buf, len, 0);
    return (n < 0) ? -1 : (int)n;
}

// Parse HTTP response to extract body
static char* parse_http_body(const unsigned char* response, size_t len) {
    // Find \r\n\r\n (end of headers)
    const char* body_start = strstr((const char*)response, "\r\n\r\n");
    if (!body_start) {
        // Try \n\n
        body_start = strstr((const char*)response, "\n\n");
        if (!body_start) return NULL;
        body_start += 2;
    } else {
        body_start += 4;
    }

    // Calculate body length
    size_t body_len = len - (body_start - (const char*)response);

    // Copy body to new buffer
    char* body = SAFE_MALLOC(body_len + 1);
    memcpy(body, body_start, body_len);
    body[body_len] = '\0';

    return body;
}

// Fetch URL using BearSSL (HTTPS GET request)
static char* fetch_url(const char* url) {
    // Parse URL: https://hostname/path
    char hostname[256];
    char path[512];

    if (sscanf(url, "https://%255[^/]%511s", hostname, path) != 2) {
        log_error("Invalid URL format: %s", url);
        return NULL;
    }

    // Connect TCP socket
    socket_t sock = socket_connect_tcp(hostname, 443);
    if (sock == INVALID_SOCKET_VALUE) {
        log_error("Failed to connect to %s:443", hostname);
        return NULL;
    }

    // BearSSL context setup (stack-based, no malloc!)
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    unsigned char response[65536];
    br_sslio_context ioc;
    br_ssl_client_context sc;
    br_x509_minimal_context xc;

    // Initialize SSL client with minimal profile (no malloc!)
    br_ssl_client_init_full(&sc, &xc, TAs, TAs_NUM);
    br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof iobuf, 1);
    br_ssl_client_reset(&sc, hostname, 0);

    // Initialize I/O wrapper
    br_sslio_init(&ioc, &sc.eng, sock_read, &sock, sock_write, &sock);

    // Send HTTP GET request
    char request[1024];
    snprintf(request, sizeof request,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: ascii-chat/1.0\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, hostname);

    if (br_sslio_write_all(&ioc, request, strlen(request)) < 0) {
        log_error("Failed to send HTTPS request");
        socket_close(sock);
        return NULL;
    }
    br_sslio_flush(&ioc);

    // Read HTTP response
    size_t total = 0;
    int n;
    while (total < sizeof(response) - 1 &&
           (n = br_sslio_read(&ioc, response + total, sizeof(response) - total - 1)) > 0) {
        total += n;
    }
    response[total] = '\0';

    socket_close(sock);

    if (total == 0) {
        log_error("Empty response from %s", url);
        return NULL;
    }

    // Extract body from HTTP response
    return parse_http_body(response, total);
}

// Fetch SSH keys from GitHub
int fetch_github_keys(const char* username, char*** keys_out, size_t* num_keys) {
    char url[256];
    snprintf(url, sizeof(url), "https://github.com/%s.keys", username);

    char* response = fetch_url(url);
    if (!response) {
        log_error("Failed to fetch GitHub keys for: %s", username);
        return -1;
    }

    // Parse response into lines (only Ed25519 keys!)
    char** keys = SAFE_MALLOC(sizeof(char*) * 16);  // Max 16 keys
    *num_keys = 0;

    char* line = strtok(response, "\n\r");
    while (line && *num_keys < 16) {
        // ONLY accept Ed25519 keys (skip RSA, ECDSA, DSA)
        if (strstr(line, "ssh-ed25519")) {
            keys[*num_keys] = strdup(line);
            (*num_keys)++;
        }
        line = strtok(NULL, "\n\r");
    }

    free(response);

    if (*num_keys == 0) {
        log_warn("No Ed25519 keys found for GitHub user: %s", username);
        log_warn("RSA keys are not supported. Please add an Ed25519 key to your GitHub account.");
        free(keys);
        return -1;
    }

    *keys_out = keys;
    return 0;
}

// Fetch SSH keys from GitLab
int fetch_gitlab_keys(const char* username, char*** keys_out, size_t* num_keys) {
    char url[256];
    snprintf(url, sizeof(url), "https://gitlab.com/%s.keys", username);

    // Same implementation as GitHub
    // (GitLab uses same .keys endpoint)

    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    char* response = NULL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || !response) {
        free(response);
        return -1;
    }

    // Parse response (same as GitHub)
    char** keys = SAFE_MALLOC(sizeof(char*) * 16);
    *num_keys = 0;

    char* line = strtok(response, "\n\r");
    while (line && *num_keys < 16) {
        if (strstr(line, "ssh-ed25519") || strstr(line, "ssh-rsa")) {
            keys[*num_keys] = strdup(line);
            (*num_keys)++;
        }
        line = strtok(NULL, "\n\r");
    }

    free(response);

    if (*num_keys == 0) {
        free(keys);
        return -1;
    }

    *keys_out = keys;
    return 0;
}

// Fetch GPG keys from GitHub
int fetch_github_gpg_keys(const char* username, char*** keys_out, size_t* num_keys) {
    char url[256];
    snprintf(url, sizeof(url), "https://github.com/%s.gpg", username);

    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    char* response = NULL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || !response) {
        free(response);
        return -1;
    }

    // GPG key is in ASCII armor format
    // We'll need to parse this with gpg command or a GPG library
    // For now, store the raw GPG key
    *keys_out = SAFE_MALLOC(sizeof(char*) * 1);
    (*keys_out)[0] = response;  // Don't free, transfer ownership
    *num_keys = 1;

    return 0;
}

// Check if gpg command is available
static bool is_gpg_available(void) {
    // Try to run 'gpg --version'
    FILE* fp = popen("gpg --version 2>/dev/null", "r");
    if (!fp) return false;

    char buf[256];
    bool found = (fgets(buf, sizeof(buf), fp) != NULL);
    pclose(fp);

    return found;
}

// Get GPG key material by shelling out to gpg command
// This allows GPG agent to handle passphrase prompts via pinentry
// Returns: 0 on success, -1 on failure (gpg not found or key export failed)
static int get_gpg_key_material(const char* key_id, uint8_t material[32]) {
    // Check if gpg is installed
    if (!is_gpg_available()) {
        log_error("GPG key requested but 'gpg' command not found");
        log_error("Install GPG:");
        log_error("  Ubuntu/Debian: apt-get install gnupg");
        log_error("  macOS: brew install gnupg");
        log_error("  Arch: pacman -S gnupg");
        log_error("Or use password auth: --key mypassword");
        return -1;
    }

    // Use gpg to export the key and derive material
    char cmd[512];

    // Export public key fingerprint as raw bytes
    snprintf(cmd, sizeof(cmd), "gpg --export %s 2>/dev/null", key_id);

    FILE* fp = popen(cmd, "r");
    if (!fp) {
        log_error("Failed to run gpg command");
        return -1;
    }

    // Read GPG key export
    uint8_t buffer[8192];
    size_t total = 0;
    size_t n;

    while ((n = fread(buffer + total, 1, sizeof(buffer) - total, fp)) > 0) {
        total += n;
        if (total >= sizeof(buffer)) break;
    }

    int status = pclose(fp);
    if (status != 0 || total == 0) {
        log_error("Failed to export GPG key: %s", key_id);
        log_error("Make sure the key exists in your GPG keyring:");
        log_error("  gpg --list-keys");
        return -1;
    }

    // Hash the exported key to get 32 bytes of material
    // Use BLAKE2b for key derivation
    crypto_generichash(material, 32, buffer, total, NULL, 0);

    log_info("Derived key material from GPG key: %s", key_id);
    return 0;
}

// Decrypt a message using GPG (prompts for passphrase via pinentry)
static int gpg_decrypt_to_key(const char* encrypted_file, uint8_t material[32]) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "gpg --decrypt %s 2>/dev/null", encrypted_file);

    FILE* fp = popen(cmd, "r");
    if (!fp) return -1;

    uint8_t buffer[8192];
    size_t total = fread(buffer, 1, sizeof(buffer), fp);
    int status = pclose(fp);

    if (status != 0 || total == 0) return -1;

    // Hash decrypted data to get key material
    crypto_generichash(material, 32, buffer, total, NULL, 0);

    // Zero the buffer
    memset(buffer, 0, sizeof(buffer));

    return 0;
}

// Parse public key from any format (SSH, GPG, X25519, GitHub, etc.)
int parse_public_key(const char* input, public_key_t* key_out) {
    memset(key_out, 0, sizeof(public_key_t));

    // SSH Ed25519
    if (strncmp(input, "ssh-ed25519 ", 12) == 0) {
        uint8_t ed25519_pk[32];
        if (parse_ssh_ed25519_line(input, ed25519_pk) != 0) {
            return -1;
        }
        memcpy(key_out->key.ed25519, ed25519_pk, 32);
        key_out->type = KEY_TYPE_ED25519;
        return 0;

    // SSH RSA
    } else if (strncmp(input, "ssh-rsa ", 8) == 0) {
        return parse_ssh_rsa_line(input, key_out);

    } else if (strncmp(input, "github:", 7) == 0) {
        // GitHub SSH keys
        const char* username = input + 7;

        // Check if .gpg suffix (GPG key) - OPTIONAL feature
        const char* gpg_suffix = strstr(username, ".gpg");
        if (gpg_suffix && gpg_suffix[4] == '\0') {
            // Check if gpg is available first
            if (!is_gpg_available()) {
                log_error("GPG key requested (github:%s) but 'gpg' command not found", username);
                log_error("Install GPG or use SSH keys instead:");
                log_error("  github:%s (without .gpg suffix)", username);
                return -1;
            }

            // Fetch GPG key from GitHub
            char username_copy[256];
            strncpy(username_copy, username, gpg_suffix - username);
            username_copy[gpg_suffix - username] = '\0';

            char** keys;
            size_t num_keys;
            if (fetch_github_gpg_keys(username_copy, &keys, &num_keys) != 0) {
                log_error("Failed to fetch GPG keys for: %s", username_copy);
                return -1;
            }

            // Parse GPG key (TODO: implement GPG key parsing)
            // For now, just store the raw GPG armor
            log_warn("GPG key parsing not yet implemented");
            log_info("Fallback: Try 'github:%s' for SSH keys instead", username_copy);
            free(keys[0]);
            free(keys);
            return -1;
        }

        // Fetch SSH keys
        char** keys;
        size_t num_keys;
        if (fetch_github_keys(username, &keys, &num_keys) != 0) {
            log_error("Failed to fetch GitHub SSH keys for: %s", username);
            return -1;
        }

        // Use first supported key (prefer Ed25519, fallback to RSA)
        int result = -1;
        for (size_t i = 0; i < num_keys; i++) {
            if (strstr(keys[i], "ssh-ed25519")) {
                result = parse_public_key(keys[i], key_out);
                if (result == 0) break;
            }
        }

        // If no Ed25519, try RSA
        if (result != 0) {
            for (size_t i = 0; i < num_keys; i++) {
                if (strstr(keys[i], "ssh-rsa")) {
                    result = parse_public_key(keys[i], key_out);
                    if (result == 0) break;
                }
            }
        }

        // Free keys
        for (size_t i = 0; i < num_keys; i++) {
            free(keys[i]);
        }
        free(keys);

        return result;

    } else if (strncmp(input, "gitlab:", 7) == 0) {
        // GitLab SSH keys (same as GitHub)
        const char* username = input + 7;
        char** keys;
        size_t num_keys;

        if (fetch_gitlab_keys(username, &keys, &num_keys) != 0) {
            log_error("Failed to fetch GitLab keys for: %s", username);
            return -1;
        }

        // Use first supported key (prefer Ed25519, fallback to RSA)
        int result = -1;
        for (size_t i = 0; i < num_keys; i++) {
            if (strstr(keys[i], "ssh-ed25519") || strstr(keys[i], "ssh-rsa")) {
                result = parse_public_key(keys[i], key_out);
                if (result == 0) break;
            }
        }

        // Free keys
        for (size_t i = 0; i < num_keys; i++) {
            free(keys[i]);
        }
        free(keys);

        return result;

    } else if (strncmp(input, "gpg:", 4) == 0) {
        // GPG key ID (use gpg command to get key material)
        // OPTIONAL: Gracefully fails if gpg not installed
        const char* key_id = input + 4;
        uint8_t material[32];

        if (get_gpg_key_material(key_id, material) != 0) {
            // gpg not found or key export failed
            // Error already logged by get_gpg_key_material()
            return -1;
        }

        // Treat as X25519 key (derived from GPG key)
        memcpy(key_out->key.x25519, material, 32);
        key_out->type = KEY_TYPE_X25519;
        snprintf(key_out->comment, sizeof(key_out->comment), "gpg:%s", key_id);

        return 0;

    } else if (access(input, F_OK) == 0) {
        // File exists - read it
        FILE* f = fopen(input, "r");
        if (!f) return -1;

        char line[2048];
        if (fgets(line, sizeof(line), f)) {
            fclose(f);
            return parse_public_key(line, key_out);
        }

        fclose(f);
        return -1;

    } else if (strlen(input) == 64) {
        // Raw hex (X25519 public key)
        if (hex_decode(input, key_out->key.x25519, 32) == 0) {
            key_out->type = KEY_TYPE_X25519;
            return 0;
        }
        return -1;
    }

    log_error("Unknown public key format: %s", input);
    return -1;
}

// Convert public key to X25519 (only works for Ed25519 and X25519 types)
int public_key_to_x25519(const public_key_t* key, uint8_t x25519_pk[32]) {
    switch (key->type) {
        case KEY_TYPE_ED25519:
            return crypto_sign_ed25519_pk_to_curve25519(x25519_pk, key->key.ed25519);

        case KEY_TYPE_X25519:
            memcpy(x25519_pk, key->key.x25519, 32);
            return 0;

        case KEY_TYPE_RSA:
        case KEY_TYPE_GPG_RSA:
            // RSA keys cannot be converted to X25519
            // Must use RSA key encapsulation instead
            log_error("Cannot convert RSA key to X25519 - use key encapsulation");
            return -1;

        default:
            log_error("Unknown key type: %d", key->type);
            return -1;
    }
}

// Free resources for keys with allocated buffers
void free_public_key(public_key_t* key) {
    if (key->type == KEY_TYPE_RSA || key->type == KEY_TYPE_GPG_RSA) {
        if (key->key.rsa.n) {
            free(key->key.rsa.n);
            key->key.rsa.n = NULL;
        }
        if (key->key.rsa.e) {
            free(key->key.rsa.e);
            key->key.rsa.e = NULL;
        }
    }
}

// Parse SSH authorized_keys file
int parse_authorized_keys(const char* path, uint8_t keys[][32], size_t* num_keys, size_t max_keys) {
    FILE* f = fopen(path, "r");
    if (!f) return -1;

    *num_keys = 0;
    char line[2048];

    while (fgets(line, sizeof(line), f) && *num_keys < max_keys) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        // Try to parse as SSH key
        if (parse_ssh_public_key(line, keys[*num_keys]) == 0) {
            (*num_keys)++;
        }
    }

    fclose(f);
    return (*num_keys > 0) ? 0 : -1;
}

// Convert X25519 key back to SSH format for display
void format_ssh_public_key(const uint8_t x25519_pk[32], char* output, size_t output_size) {
    // For now, just show as hex
    // TODO: Could convert back to Ed25519 if we store both
    char hex[65];
    hex_encode(x25519_pk, 32, hex);
    snprintf(output, output_size, "x25519 (from SSH) %s", hex);
}
```

#### 1.2 Update CMakeLists.txt

Add BearSSL dependency:

**Option 1: Vendored BearSSL (recommended)**
```cmake
# Add BearSSL as a submodule/subdirectory
add_subdirectory(deps/bearssl)

target_link_libraries(asciichat_lib
    # ... existing libraries ...
    bearssl  # BearSSL static library
)

target_include_directories(asciichat_lib PRIVATE
    deps/bearssl/inc
)
```

**Option 2: System BearSSL**
```cmake
# Find BearSSL (if installed system-wide)
find_library(BEARSSL_LIBRARY bearssl)
find_path(BEARSSL_INCLUDE_DIR bearssl.h)

if(NOT BEARSSL_LIBRARY OR NOT BEARSSL_INCLUDE_DIR)
    message(FATAL_ERROR "BearSSL not found. Install with: apt-get install libbearssl-dev")
endif()

target_link_libraries(asciichat_lib
    # ... existing libraries ...
    ${BEARSSL_LIBRARY}
)

target_include_directories(asciichat_lib PRIVATE
    ${BEARSSL_INCLUDE_DIR}
)
```

Add keys.c to library:
```cmake
set(LIB_SOURCES
    # ... existing sources ...
    lib/keys.c
)
```

**Note**: BearSSL is lightweight and easy to vendor. Recommended approach:
```bash
# Clone BearSSL into deps/
git submodule add https://www.bearssl.org/git/BearSSL deps/bearssl
cd deps/bearssl
make  # Builds libbearssl.a
```

This gives you a ~200KB static library with zero external dependencies.

### Phase 2: CLI Flags & In-Memory State (2.5 hours)

**Goal**: Parse flags, store crypto config in memory, initialize libsodium, display keys

#### 2.1 Update options.c/h

```c
typedef struct {
    // Existing fields...

    // Crypto options
    bool no_encrypt;        // Disable encryption (opt-out)
    char* key;              // Password for authentication
    char* keyfile;          // Path to file containing password
    char* ssh_key;          // Path to SSH private key file (server only)
    char* server_key;       // Expected server public key (client only) - supports github:user, ssh-ed25519, file
    char* client_keys;      // Allowed client keys (server only) - supports github:users, file, comma-separated
} options_t;
```

**Parsing logic**:
- Default: encryption enabled (if `!no_encrypt && !key && !keyfile`)
- If `--no-encrypt`: encryption disabled
- If `--key` or `--keyfile`: encryption enabled with auth
- If `--ssh-key`: server uses existing SSH key
- If `--server-key`: client verifies server key
- If `--client-keys`: server restricts clients

**Validation**:
- `--key` and `--keyfile` are mutually exclusive
- `--no-encrypt` cannot be used with other crypto flags
- `--ssh-key` is server-only
- `--server-key` is client-only
- `--client-keys` is server-only

#### 2.2 Add crypto state to server (src/server/main.c)

```c
typedef struct {
    bool encryption_enabled;
    uint8_t server_public_key[32];   // X25519 public key
    uint8_t server_private_key[32];  // X25519 private key
    uint8_t password_key[32];        // Derived from password (if provided)
    bool require_auth;               // True if --key/--keyfile provided
    bool using_ssh_key;              // True if --ssh-key provided
} server_crypto_state_t;

// Client whitelist (global)
typedef struct {
    uint8_t keys[MAX_CLIENTS][32];
    size_t num_keys;
} client_whitelist_t;

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

    // Use SSH key if provided, otherwise generate ephemeral
    if (options.ssh_key) {
        // Load SSH private key and convert to X25519
        if (parse_ssh_private_key(options.ssh_key, g_crypto_state.server_private_key) != 0) {
            log_error("Failed to load SSH key: %s", options.ssh_key);
            exit(1);
        }

        // Derive public key
        crypto_scalarmult_base(g_crypto_state.server_public_key, g_crypto_state.server_private_key);

        g_crypto_state.using_ssh_key = true;
        log_info("Using SSH key: %s", options.ssh_key);
    } else {
        // Generate ephemeral keypair
        crypto_generate_keypair(g_crypto_state.server_public_key,
                               g_crypto_state.server_private_key);
        log_info("Generated ephemeral server keypair");
    }

    // Display server public key prominently in SSH format
    // TODO: Convert X25519 back to Ed25519 for proper SSH format
    // For now, display as custom format
    char pubkey_hex[65];
    hex_encode(g_crypto_state.server_public_key, 32, pubkey_hex);

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  SERVER PUBLIC KEY                                             ║\n");
    printf("║  x25519 %-56s║\n", pubkey_hex);
    printf("║                                                                ║\n");
    printf("║  Verify with:                                                  ║\n");
    printf("║    ascii-chat-client --server-key <paste-above>                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Load client whitelist if provided
    if (options.client_keys) {
        // Parse client keys (supports github:, file, comma-separated)
        parse_client_keys_multi_format(options.client_keys, &g_client_whitelist);
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
        log_info("Use --key PASSWORD or --ssh-key for MITM protection");
    }
} else {
    log_warn("Encryption: DISABLED");
    log_warn("All traffic sent in PLAINTEXT");
}
```

**Helper function for parsing client keys**:
```c
static void parse_client_keys_multi_format(const char* input, client_whitelist_t* whitelist) {
    whitelist->num_keys = 0;

    // Check if input is a filepath (file exists)
    FILE* f = fopen(input, "r");
    if (f != NULL) {
        fclose(f);

        // Try SSH authorized_keys format first
        if (parse_authorized_keys(input, whitelist->keys, &whitelist->num_keys, MAX_CLIENTS) == 0) {
            log_info("Loaded %zu client keys from authorized_keys file: %s", whitelist->num_keys, input);
            return;
        }

        // Fallback: treat as single SSH key per line
        f = fopen(input, "r");
        char line[2048];
        while (fgets(line, sizeof(line), f) && whitelist->num_keys < MAX_CLIENTS) {
            if (line[0] == '#' || line[0] == '\n') continue;

            if (parse_ssh_public_key(line, whitelist->keys[whitelist->num_keys]) == 0) {
                whitelist->num_keys++;
            }
        }
        fclose(f);

        if (whitelist->num_keys > 0) {
            log_info("Loaded %zu client keys from file: %s", whitelist->num_keys, input);
            return;
        }
    }

    // Treat as comma-separated list
    char* input_copy = strdup(input);
    char* token = strtok(input_copy, ",");

    while (token != NULL && whitelist->num_keys < MAX_CLIENTS) {
        // Trim whitespace
        while (*token == ' ') token++;
        char* end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';

        // Parse (supports github:, gitlab:, ssh-ed25519, hex)
        if (parse_ssh_public_key(token, whitelist->keys[whitelist->num_keys]) == 0) {
            whitelist->num_keys++;
        } else {
            log_warn("Invalid key in list (skipping): %s", token);
        }

        token = strtok(NULL, ",");
    }
    free(input_copy);

    if (whitelist->num_keys > 0) {
        log_info("Loaded %zu client keys from command line", whitelist->num_keys);
    } else {
        log_error("No valid client keys found in: %s", input);
        exit(1);
    }
}
```

#### 2.3 Add crypto state to client (src/client/main.c)

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
    log_info("Client public key: x25519 %s", pubkey_hex);
    log_info("(share with server operator to be added to whitelist)");

    // Load expected server key if provided
    if (options.server_key) {
        if (parse_ssh_public_key(options.server_key, g_crypto_state.expected_server_key) != 0) {
            log_error("Invalid --server-key format: %s", options.server_key);
            log_error("Supported formats:");
            log_error("  github:username");
            log_error("  gitlab:username");
            log_error("  ssh-ed25519 AAAAC3...");
            log_error("  /path/to/key.pub");
            log_error("  64-char hex string");
            exit(1);
        }
        g_crypto_state.verify_server_key = true;
        log_info("Will verify server key against: %s", options.server_key);
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
} else {
    log_info("Encryption: DISABLED");
}
```

### Phase 3: Known Hosts Integration (2 hours)

**Goal**: Implement SSH-like known_hosts behavior

#### 3.1 Create lib/known_hosts.c/h (NEW FILE)

```c
// lib/known_hosts.h
#ifndef KNOWN_HOSTS_H
#define KNOWN_HOSTS_H

#include <stdint.h>
#include <stdbool.h>

// Check if server key is in known_hosts
// Returns:
//   1 = key matches (all good)
//   0 = server not in known_hosts (first connection)
//  -1 = key mismatch (MITM warning!)
int check_known_host(const char* hostname, uint16_t port, const uint8_t server_key[32]);

// Add server to known_hosts
int add_known_host(const char* hostname, uint16_t port, const uint8_t server_key[32]);

// Remove server from known_hosts
int remove_known_host(const char* hostname, uint16_t port);

// Get known_hosts file path
const char* get_known_hosts_path(void);

#endif
```

```c
// lib/known_hosts.c
#include "known_hosts.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define KNOWN_HOSTS_PATH "~/.ascii-chat/known_hosts"

static char* expand_path(const char* path) {
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (!home) return NULL;

        char* expanded = SAFE_MALLOC(strlen(home) + strlen(path));
        sprintf(expanded, "%s%s", home, path + 1);
        return expanded;
    }
    return strdup(path);
}

const char* get_known_hosts_path(void) {
    static char* path = NULL;
    if (!path) {
        path = expand_path(KNOWN_HOSTS_PATH);
    }
    return path;
}

// Format: hostname:port ssh-ed25519 <base64> [comment]
int check_known_host(const char* hostname, uint16_t port, const uint8_t server_key[32]) {
    const char* path = get_known_hosts_path();
    FILE* f = fopen(path, "r");
    if (!f) return 0;  // File doesn't exist = first connection

    char line[2048];
    char expected_prefix[512];
    snprintf(expected_prefix, sizeof(expected_prefix), "%s:%u ", hostname, port);

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;  // Comment

        if (strncmp(line, expected_prefix, strlen(expected_prefix)) == 0) {
            // Found matching hostname:port
            fclose(f);

            // Parse key from line
            uint8_t stored_key[32];
            if (parse_ssh_public_key(line + strlen(expected_prefix), stored_key) != 0) {
                return -1;  // Parse error = treat as mismatch
            }

            // Compare keys
            if (memcmp(server_key, stored_key, 32) == 0) {
                return 1;  // Match!
            } else {
                return -1;  // Mismatch - MITM warning!
            }
        }
    }

    fclose(f);
    return 0;  // Not found = first connection
}

int add_known_host(const char* hostname, uint16_t port, const uint8_t server_key[32]) {
    const char* path = get_known_hosts_path();

    // Create directory if needed
    char* dir = strdup(path);
    char* last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir, 0700);
    }
    free(dir);

    // Append to file
    FILE* f = fopen(path, "a");
    if (!f) return -1;

    char hex[65];
    hex_encode(server_key, 32, hex);

    fprintf(f, "%s:%u x25519 %s ascii-chat-server\n", hostname, port, hex);
    fclose(f);

    return 0;
}

int remove_known_host(const char* hostname, uint16_t port) {
    // TODO: Read file, filter out matching line, write back
    // For now, user can manually edit
    return 0;
}
```

#### 3.2 Update client to use known_hosts

In client's KEY_EXCHANGE_INIT handler:

```c
// After receiving server's public key
if (g_crypto_state.verify_server_key) {
    // User provided --server-key, verify against it
    if (memcmp(server_public_key, g_crypto_state.expected_server_key, 32) != 0) {
        display_mitm_warning(g_crypto_state.expected_server_key, server_public_key);
        exit(1);
    }
    log_info("✓ Server key verified (matches --server-key)");
} else {
    // Check known_hosts
    int result = check_known_host(server_hostname, server_port, server_public_key);

    if (result == 1) {
        // Key matches known_hosts
        log_info("✓ Server key verified (known host)");
    } else if (result == 0) {
        // First connection - prompt user
        printf("\n");
        printf("╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  FIRST CONNECTION TO THIS SERVER                               ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n");
        printf("\n");

        char hex[65];
        hex_encode(server_public_key, 32, hex);
        printf("Server public key: x25519 %s\n", hex);
        printf("\n");
        printf("Save to known_hosts? [yes/no]: ");

        char response[16];
        if (fgets(response, sizeof(response), stdin)) {
            if (strncmp(response, "yes", 3) == 0 || strncmp(response, "y", 1) == 0) {
                add_known_host(server_hostname, server_port, server_public_key);
                log_info("Server added to known_hosts");
            } else {
                log_warn("Server NOT added to known_hosts");
                log_warn("You will be prompted again on next connection");
            }
        }
    } else {
        // Key mismatch - MITM!
        uint8_t stored_key[32];
        // Re-read to get stored key for display

        fprintf(stderr, "\n");
        fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
        fprintf(stderr, "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @\n");
        fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!\n");
        fprintf(stderr, "Someone could be eavesdropping on you right now (man-in-the-middle attack)!\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "The server key for %s:%u has changed.\n", server_hostname, server_port);
        fprintf(stderr, "\n");
        fprintf(stderr, "If this is expected (server reinstalled), remove old key:\n");
        fprintf(stderr, "  sed -i '/%s:%u/d' ~/.ascii-chat/known_hosts\n", server_hostname, server_port);
        fprintf(stderr, "\n");
        fprintf(stderr, "Connection ABORTED for security.\n");
        fprintf(stderr, "\n");

        exit(1);
    }
}
```

### Phase 4: Key Exchange Protocol (3 hours)

**Goal**: Establish shared secret using Diffie-Hellman key exchange, verify public keys

(Similar to previous plan, but now using SSH key infrastructure)

#### 4.1 Add new packet types (lib/network.h)

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

### Phase 5: Encryption Integration (2 hours)

**Goal**: Integrate crypto handshake into client/server main loops, add encryption/decryption to packet processing, update client/server to use new crypto options

#### 5.1 Update Client Main Loop (src/client/main.c)

**Add crypto state to client**:

```c
// Add to top of main.c
#include "crypto/handshake.h"
#include "crypto/known_hosts.h"

// Global crypto state
static crypto_handshake_context_t g_crypto_ctx = {0};
static bool g_encryption_enabled = false;

// Initialize crypto for client
static int init_client_crypto(void) {
    // Check if encryption is disabled
    if (opt_no_encrypt) {
        log_info("Encryption: DISABLED (--no-encrypt)");
        return 0;
    }

    // Initialize crypto handshake
    if (crypto_handshake_init(&g_crypto_ctx, false) != 0) {
        log_error("Failed to initialize crypto handshake");
        return -1;
    }

    // Set expected server key if provided
    if (strlen(opt_server_key) > 0) {
        if (parse_public_key(opt_server_key, &g_crypto_ctx.expected_server_key) != 0) {
            log_error("Invalid --server-key format: %s", opt_server_key);
            return -1;
        }
        g_crypto_ctx.verify_server_key = true;
        log_info("Will verify server key against: %s", opt_server_key);
    }

    g_encryption_enabled = true;
    log_info("Encryption: ENABLED");
    return 0;
}

// Perform crypto handshake with server
static int perform_crypto_handshake(socket_t server_socket) {
    if (!g_encryption_enabled) return 0;

    log_info("Starting crypto handshake...");

    // Client: Process server's public key and send our public key
    if (crypto_handshake_client_key_exchange(&g_crypto_ctx, server_socket) != 0) {
        log_error("Crypto handshake failed during key exchange");
        return -1;
    }

    // Client: Process auth challenge and send response
    if (crypto_handshake_client_auth_response(&g_crypto_ctx, server_socket) != 0) {
        log_error("Crypto handshake failed during authentication");
        return -1;
    }

    // Check if handshake is complete
    if (!crypto_handshake_is_ready(&g_crypto_ctx)) {
        log_error("Crypto handshake did not complete successfully");
        return -1;
    }

    log_info("✓ Crypto handshake completed - encryption ready");
    return 0;
}

// Encrypt packet before sending
static int send_encrypted_packet(socket_t socket, const uint8_t* data, size_t len) {
    if (!g_encryption_enabled) {
        // Send unencrypted
        return socket_send(socket, data, len, 0);
    }

    // Encrypt packet
    uint8_t encrypted[PACKET_MAX_SIZE];
    size_t encrypted_len;

    if (crypto_handshake_encrypt_packet(&g_crypto_ctx, data, len,
                                       encrypted, sizeof(encrypted), &encrypted_len) != 0) {
        log_error("Failed to encrypt packet");
        return -1;
    }

    return socket_send(socket, encrypted, encrypted_len, 0);
}

// Decrypt packet after receiving
static int recv_encrypted_packet(socket_t socket, uint8_t* data, size_t max_len, size_t* actual_len) {
    uint8_t encrypted[PACKET_MAX_SIZE];
    ssize_t received = socket_recv(socket, encrypted, sizeof(encrypted), 0);
    if (received <= 0) {
        *actual_len = 0;
        return (int)received;
    }

    if (!g_encryption_enabled) {
        // Receive unencrypted
        memcpy(data, encrypted, received);
        *actual_len = received;
        return 0;
    }

    // Decrypt packet
    if (crypto_handshake_decrypt_packet(&g_crypto_ctx, encrypted, received,
                                       data, max_len, actual_len) != 0) {
        log_error("Failed to decrypt packet");
        return -1;
    }

    return 0;
}
```

**Update main() function**:

```c
int main(int argc, char **argv) {
    // ... existing argument parsing ...

    // Initialize crypto BEFORE connecting to server
    if (init_client_crypto() != 0) {
        log_error("Failed to initialize crypto");
        exit(1);
    }

    // ... existing connection code ...

    // Perform crypto handshake AFTER connecting but BEFORE starting video/audio
    if (perform_crypto_handshake(server_socket) != 0) {
        log_error("Crypto handshake failed");
        socket_close(server_socket);
        exit(1);
    }

    // ... existing video/audio setup ...

    // Main loop - replace all socket_send/socket_recv with encrypted versions
    while (running) {
        // Send video frame (encrypted)
        if (send_encrypted_packet(server_socket, video_data, video_len) < 0) {
            log_error("Failed to send encrypted video packet");
            break;
        }

        // Receive audio data (decrypted)
        uint8_t audio_data[PACKET_MAX_SIZE];
        size_t audio_len;
        if (recv_encrypted_packet(server_socket, audio_data, sizeof(audio_data), &audio_len) < 0) {
            log_error("Failed to receive encrypted audio packet");
            break;
        }
        // ... process audio_data ...
    }

    // Cleanup
    if (g_encryption_enabled) {
        crypto_handshake_cleanup(&g_crypto_ctx);
    }

    // ... existing cleanup ...
}
```

#### 5.2 Update Server Main Loop (src/server/main.c)

**Add crypto state to server**:

```c
// Add to top of main.c
#include "crypto/handshake.h"
#include "crypto/keys.h"

// Per-client crypto state
typedef struct {
    socket_t client_socket;
    crypto_handshake_context_t crypto_ctx;
    bool encryption_enabled;
    bool handshake_complete;
} client_crypto_state_t;

// Global server crypto state
static bool g_server_encryption_enabled = false;
static private_key_t g_server_private_key = {0};
static public_key_t g_client_whitelist[MAX_CLIENTS] = {0};
static size_t g_num_whitelisted_clients = 0;

// Initialize crypto for server
static int init_server_crypto(void) {
    // Check if encryption is disabled
    if (opt_no_encrypt) {
        log_info("Encryption: DISABLED (--no-encrypt)");
        return 0;
    }

    // Load server private key if provided
    if (strlen(opt_ssh_key) > 0) {
        if (parse_private_key(opt_ssh_key, &g_server_private_key) != 0) {
            log_error("Failed to load server SSH key: %s", opt_ssh_key);
            return -1;
        }
        log_info("Using SSH key: %s", opt_ssh_key);
    } else {
        // Generate ephemeral keypair
        crypto_generate_keypair(g_server_private_key.key.x25519,
                               g_server_private_key.key.x25519);
        g_server_private_key.type = KEY_TYPE_X25519;
        log_info("Generated ephemeral server keypair");
    }

    // Load client whitelist if provided
    if (strlen(opt_client_keys) > 0) {
        if (parse_authorized_keys(opt_client_keys, g_client_whitelist,
                                 &g_num_whitelisted_clients, MAX_CLIENTS) != 0) {
            log_error("Failed to load client keys: %s", opt_client_keys);
            return -1;
        }
        log_info("Server will only accept %zu whitelisted clients", g_num_whitelisted_clients);
    }

    g_server_encryption_enabled = true;
    log_info("Encryption: ENABLED");
    return 0;
}

// Handle crypto handshake for a new client
static int handle_client_crypto_handshake(client_crypto_state_t* client_state) {
    if (!g_server_encryption_enabled) return 0;

    // Initialize crypto handshake for this client
    if (crypto_handshake_init(&client_state->crypto_ctx, true) != 0) {
        log_error("Failed to initialize crypto handshake for client");
        return -1;
    }

    // Set server keys
    memcpy(&client_state->crypto_ctx.server_private_key, &g_server_private_key, sizeof(private_key_t));

    // Set client whitelist if configured
    if (g_num_whitelisted_clients > 0) {
        client_state->crypto_ctx.require_client_auth = true;
        // TODO: Set client keys in context
    }

    // Server: Start crypto handshake by sending public key
    if (crypto_handshake_server_start(&client_state->crypto_ctx, client_state->client_socket) != 0) {
        log_error("Failed to start crypto handshake");
        return -1;
    }

    // Server: Process client's public key and send auth challenge
    if (crypto_handshake_server_auth_challenge(&client_state->crypto_ctx, client_state->client_socket) != 0) {
        log_error("Failed to process client key exchange");
        return -1;
    }

    // Server: Process auth response and complete handshake
    if (crypto_handshake_server_complete(&client_state->crypto_ctx, client_state->client_socket) != 0) {
        log_error("Failed to complete crypto handshake");
        return -1;
    }

    if (!crypto_handshake_is_ready(&client_state->crypto_ctx)) {
        log_error("Crypto handshake did not complete successfully");
        return -1;
    }

    client_state->encryption_enabled = true;
    client_state->handshake_complete = true;
    log_info("✓ Crypto handshake completed with client - encryption ready");
    return 0;
}

// Encrypt packet before sending to client
static int send_encrypted_packet_to_client(client_crypto_state_t* client_state,
                                          const uint8_t* data, size_t len) {
    if (!client_state->encryption_enabled) {
        // Send unencrypted
        return socket_send(client_state->client_socket, data, len, 0);
    }

    // Encrypt packet
    uint8_t encrypted[PACKET_MAX_SIZE];
    size_t encrypted_len;

    if (crypto_handshake_encrypt_packet(&client_state->crypto_ctx, data, len,
                                       encrypted, sizeof(encrypted), &encrypted_len) != 0) {
        log_error("Failed to encrypt packet to client");
        return -1;
    }

    return socket_send(client_state->client_socket, encrypted, encrypted_len, 0);
}

// Decrypt packet received from client
static int recv_encrypted_packet_from_client(client_crypto_state_t* client_state,
                                            uint8_t* data, size_t max_len, size_t* actual_len) {
    uint8_t encrypted[PACKET_MAX_SIZE];
    ssize_t received = socket_recv(client_state->client_socket, encrypted, sizeof(encrypted), 0);
    if (received <= 0) {
        *actual_len = 0;
        return (int)received;
    }

    if (!client_state->encryption_enabled) {
        // Receive unencrypted
        memcpy(data, encrypted, received);
        *actual_len = received;
        return 0;
    }

    // Decrypt packet
    if (crypto_handshake_decrypt_packet(&client_state->crypto_ctx, encrypted, received,
                                       data, max_len, actual_len) != 0) {
        log_error("Failed to decrypt packet from client");
        return -1;
    }

    return 0;
}
```

**Update server main loop**:

```c
int main(int argc, char **argv) {
    // ... existing argument parsing ...

    // Initialize crypto BEFORE starting server
    if (init_server_crypto() != 0) {
        log_error("Failed to initialize crypto");
        exit(1);
    }

    // ... existing server setup ...

    // Per-client state array
    client_crypto_state_t client_states[MAX_CLIENTS] = {0};
    size_t num_clients = 0;

    // Main server loop
    while (running) {
        // Accept new client
        socket_t client_socket = socket_accept(server_socket);
        if (client_socket != INVALID_SOCKET_VALUE && num_clients < MAX_CLIENTS) {
            client_states[num_clients].client_socket = client_socket;
            client_states[num_clients].encryption_enabled = false;
            client_states[num_clients].handshake_complete = false;
            num_clients++;

            log_info("New client connected (total: %zu)", num_clients);
        }

        // Handle each client
        for (size_t i = 0; i < num_clients; i++) {
            client_crypto_state_t* client = &client_states[i];

            // Perform crypto handshake if not done yet
            if (!client->handshake_complete) {
                if (handle_client_crypto_handshake(client) != 0) {
                    log_error("Crypto handshake failed for client %zu", i);
                    socket_close(client->client_socket);
                    // Remove client from array
                    memmove(&client_states[i], &client_states[i+1],
                           (num_clients - i - 1) * sizeof(client_crypto_state_t));
                    num_clients--;
                    i--;
                    continue;
                }
            }

            // Handle video/audio data (encrypted)
            uint8_t video_data[PACKET_MAX_SIZE];
            size_t video_len;
            if (recv_encrypted_packet_from_client(client, video_data, sizeof(video_data), &video_len) > 0) {
                // Process video data...

                // Send audio data back (encrypted)
                uint8_t audio_data[PACKET_MAX_SIZE];
                size_t audio_len = generate_audio_data(audio_data, sizeof(audio_data));
                send_encrypted_packet_to_client(client, audio_data, audio_len);
            }
        }
    }

    // Cleanup
    for (size_t i = 0; i < num_clients; i++) {
        if (client_states[i].encryption_enabled) {
            crypto_handshake_cleanup(&client_states[i].crypto_ctx);
        }
        socket_close(client_states[i].client_socket);
    }

    // ... existing cleanup ...
}
```

#### 5.3 Update Network Packet Processing

**Add crypto packet types** (lib/network.h):

```c
typedef enum {
    // Existing packet types...
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

**Update packet creation** (lib/network.c):

```c
// Create crypto handshake packet
int create_crypto_packet(packet_type_t type, const uint8_t* data, size_t data_len,
                        uint8_t* packet, size_t packet_size, size_t* packet_len) {
    if (packet_size < sizeof(packet_header_t) + data_len) {
        return -1;
    }

    packet_header_t* header = (packet_header_t*)packet;
    header->type = type;
    header->length = sizeof(packet_header_t) + data_len;
    header->sequence = 0; // Handshake packets don't use sequence numbers
    header->timestamp = get_timestamp();

    if (data && data_len > 0) {
        memcpy(packet + sizeof(packet_header_t), data, data_len);
    }

    *packet_len = header->length;
    return 0;
}

// Parse crypto handshake packet
int parse_crypto_packet(const uint8_t* packet, size_t packet_len,
                        packet_type_t* type, uint8_t* data, size_t data_size, size_t* data_len) {
    if (packet_len < sizeof(packet_header_t)) {
        return -1;
    }

    const packet_header_t* header = (const packet_header_t*)packet;
    *type = header->type;

    size_t payload_len = header->length - sizeof(packet_header_t);
    if (payload_len > data_size) {
        return -1;
    }

    if (data && payload_len > 0) {
        memcpy(data, packet + sizeof(packet_header_t), payload_len);
    }
    *data_len = payload_len;

    return 0;
}
```

#### 5.4 Display Server Public Key

**Add to server startup** (src/server/main.c):

```c
static void display_server_public_key(void) {
    if (!g_server_encryption_enabled) return;

    // Convert server public key to display format
    uint8_t server_pubkey[32];
    if (public_key_to_x25519(&g_server_private_key, server_pubkey) != 0) {
        log_error("Failed to get server public key");
        return;
    }

    // Display in SSH format
    char hex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(hex + i*2, 3, "%02x", server_pubkey[i]);
    }

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  SERVER PUBLIC KEY                                             ║\n");
    printf("║  x25519 %-56s║\n", hex);
    printf("║                                                                ║\n");
    printf("║  Verify with:                                                  ║\n");
    printf("║    ascii-chat-client --server-key <paste-above>                ║\n");
    printf("║  Or save to known_hosts:                                       ║\n");
    printf("║    echo \"hostname x25519 %s\" >> ~/.ascii-chat/known_hosts║\n", hex);
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}
```

**Call in main()**:

```c
int main(int argc, char **argv) {
    // ... existing code ...

    // Initialize crypto
    if (init_server_crypto() != 0) {
        log_error("Failed to initialize crypto");
        exit(1);
    }

    // Display server public key
    display_server_public_key();

    // ... rest of server code ...
}
```

#### 5.5 Testing Integration

**Create test script** (tests/scripts/test_crypto_integration.sh):

```bash
#!/bin/bash
# Test crypto integration end-to-end

echo "Testing crypto integration..."

# Test 1: Default encryption (ephemeral DH)
echo "Test 1: Default encryption"
./ascii-chat-server --port 8080 &
SERVER_PID=$!
sleep 2
./ascii-chat-client --port 8080 --test-pattern &
CLIENT_PID=$!
sleep 5
kill $CLIENT_PID $SERVER_PID
echo "✓ Default encryption test completed"

# Test 2: Password authentication
echo "Test 2: Password authentication"
./ascii-chat-server --port 8081 --key testpassword &
SERVER_PID=$!
sleep 2
./ascii-chat-client --port 8081 --key testpassword --test-pattern &
CLIENT_PID=$!
sleep 5
kill $CLIENT_PID $SERVER_PID
echo "✓ Password authentication test completed"

# Test 3: SSH key pinning
echo "Test 3: SSH key pinning"
./ascii-chat-server --port 8082 --ssh-key ~/.ssh/id_ed25519 &
SERVER_PID=$!
sleep 2
./ascii-chat-client --port 8082 --server-key ~/.ssh/id_ed25519.pub --test-pattern &
CLIENT_PID=$!
sleep 5
kill $CLIENT_PID $SERVER_PID
echo "✓ SSH key pinning test completed"

echo "All crypto integration tests completed!"
```

### Phase 6: Testing & Validation (2 hours)

**Goal**: Comprehensive testing of all crypto modes, performance validation, cross-platform testing

#### 6.1 Unit Test Coverage

**Test all key parsing functions**:

```c
// tests/unit/crypto_keys_comprehensive_test.c
#include <criterion/criterion.h>
#include "crypto/keys.h"

Test(crypto_keys, parse_ssh_ed25519_keys) {
    public_key_t key;

    // Test valid SSH Ed25519 key
    const char* ssh_key = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo... alice@laptop";
    cr_assert_eq(parse_public_key(ssh_key, &key), 0);
    cr_assert_eq(key.type, KEY_TYPE_ED25519);
    cr_assert_str_eq(key.comment, "alice@laptop");

    // Test invalid key
    const char* invalid_key = "ssh-rsa AAAAB3NzaC1yc2E... bob@desktop";
    cr_assert_eq(parse_public_key(invalid_key, &key), -1);
}

Test(crypto_keys, parse_github_keys) {
    public_key_t key;

    // Test GitHub key fetching (with mock)
    const char* github_key = "github:zfogg";
    cr_assert_eq(parse_public_key(github_key, &key), 0);
    cr_assert_eq(key.type, KEY_TYPE_ED25519);
}

Test(crypto_keys, parse_gpg_keys) {
    public_key_t key;

    // Test GPG key parsing (with mock gpg command)
    const char* gpg_key = "gpg:0xABCD1234";
    cr_assert_eq(parse_public_key(gpg_key, &key), 0);
    cr_assert_eq(key.type, KEY_TYPE_GPG);
    cr_assert_str_eq(key.comment, "gpg:0xABCD1234");
}
```

**Test known_hosts behavior**:

```c
// tests/unit/crypto_known_hosts_comprehensive_test.c
#include <criterion/criterion.h>
#include "crypto/known_hosts.h"

Test(crypto_known_hosts, save_and_verify) {
    const char* hostname = "test.example.com";
    uint16_t port = 8080;
    uint8_t server_key[32] = {0x42}; // Test key

    // First connection - should return 0 (not in known_hosts)
    cr_assert_eq(check_known_host(hostname, port, server_key), 0);

    // Add to known_hosts
    cr_assert_eq(add_known_host(hostname, port, server_key), 0);

    // Second connection - should return 1 (match)
    cr_assert_eq(check_known_host(hostname, port, server_key), 1);
}

Test(crypto_known_hosts, mitm_detection) {
    const char* hostname = "test.example.com";
    uint16_t port = 8080;
    uint8_t original_key[32] = {0x42};
    uint8_t different_key[32] = {0x99}; // Different key

    // Add original key
    add_known_host(hostname, port, original_key);

    // Try with different key - should return -1 (MITM warning)
    cr_assert_eq(check_known_host(hostname, port, different_key), -1);
}
```

**Test handshake state machine**:

```c
// tests/unit/crypto_handshake_state_test.c
#include <criterion/criterion.h>
#include "crypto/handshake.h"

Test(crypto_handshake, state_transitions) {
    crypto_handshake_context_t ctx;

    // Initialize
    cr_assert_eq(crypto_handshake_init(&ctx, true), 0);
    cr_assert_eq(ctx.state, CRYPTO_HANDSHAKE_INIT);

    // Test state progression
    ctx.state = CRYPTO_HANDSHAKE_KEY_EXCHANGE;
    cr_assert_eq(crypto_handshake_is_ready(&ctx), false);

    ctx.state = CRYPTO_HANDSHAKE_READY;
    cr_assert_eq(crypto_handshake_is_ready(&ctx), true);

    crypto_handshake_cleanup(&ctx);
}
```

#### 6.2 Integration Testing

**Test all security levels**:

```bash
#!/bin/bash
# tests/scripts/test_all_security_levels.sh

echo "Testing all crypto security levels..."

# Level 1: Default encrypted (ephemeral DH)
echo "Level 1: Default encrypted"
./ascii-chat-server --port 8080 &
SERVER_PID=$!
sleep 2
./ascii-chat-client --port 8080 --test-pattern &
CLIENT_PID=$!
sleep 5
kill $CLIENT_PID $SERVER_PID
echo "✓ Level 1 completed"

# Level 2: Password authentication
echo "Level 2: Password authentication"
./ascii-chat-server --port 8081 --key testpassword &
SERVER_PID=$!
sleep 2
./ascii-chat-client --port 8081 --key testpassword --test-pattern &
CLIENT_PID=$!
sleep 5
kill $CLIENT_PID $SERVER_PID
echo "✓ Level 2 completed"

# Level 3: SSH key pinning
echo "Level 3: SSH key pinning"
./ascii-chat-server --port 8082 --ssh-key ~/.ssh/id_ed25519 &
SERVER_PID=$!
sleep 2
./ascii-chat-client --port 8082 --server-key ~/.ssh/id_ed25519.pub --test-pattern &
CLIENT_PID=$!
sleep 5
kill $CLIENT_PID $SERVER_PID
echo "✓ Level 3 completed"

# Level 4: Server whitelist
echo "Level 4: Server whitelist"
./ascii-chat-server --port 8083 --client-keys ~/.ssh/authorized_keys &
SERVER_PID=$!
sleep 2
./ascii-chat-client --port 8083 --test-pattern &
CLIENT_PID=$!
sleep 5
kill $CLIENT_PID $SERVER_PID
echo "✓ Level 4 completed"

# Level 5: Defense in depth
echo "Level 5: Defense in depth"
./ascii-chat-server --port 8084 --ssh-key ~/.ssh/id_ed25519 --key testpassword --client-keys ~/.ssh/authorized_keys &
SERVER_PID=$!
sleep 2
./ascii-chat-client --port 8084 --key testpassword --server-key ~/.ssh/id_ed25519.pub --test-pattern &
CLIENT_PID=$!
sleep 5
kill $CLIENT_PID $SERVER_PID
echo "✓ Level 5 completed"

# Level 6: Opt-out (no encryption)
echo "Level 6: Opt-out"
./ascii-chat-server --port 8085 --no-encrypt &
SERVER_PID=$!
sleep 2
./ascii-chat-client --port 8085 --no-encrypt --test-pattern &
CLIENT_PID=$!
sleep 5
kill $CLIENT_PID $SERVER_PID
echo "✓ Level 6 completed"

echo "All security levels tested successfully!"
```

**Test GitHub/GitLab integration**:

```c
// tests/integration/crypto_github_integration_test.c
#include <criterion/criterion.h>
#include "crypto/keys.h"

Test(crypto_github, fetch_user_keys) {
    char** keys;
    size_t num_keys;

    // Test GitHub key fetching
    cr_assert_eq(fetch_github_keys("zfogg", &keys, &num_keys), 0);
    cr_assert_gt(num_keys, 0);

    // Verify first key is Ed25519
    cr_assert_not_null(strstr(keys[0], "ssh-ed25519"));

    // Cleanup
    for (size_t i = 0; i < num_keys; i++) {
        free(keys[i]);
    }
    free(keys);
}

Test(crypto_gitlab, fetch_user_keys) {
    char** keys;
    size_t num_keys;

    // Test GitLab key fetching
    cr_assert_eq(fetch_gitlab_keys("zfogg", &keys, &num_keys), 0);
    cr_assert_gt(num_keys, 0);

    // Cleanup
    for (size_t i = 0; i < num_keys; i++) {
        free(keys[i]);
    }
    free(keys);
}
```

#### 6.3 Performance Testing

**Create performance test suite**:

```c
// tests/performance/crypto_performance_test.c
#include <criterion/criterion.h>
#include <time.h>
#include "crypto/handshake.h"

Test(crypto_performance, encryption_overhead) {
    crypto_handshake_context_t ctx;
    crypto_handshake_init(&ctx, true);

    // Test with various packet sizes
    size_t test_sizes[] = {1024, 4096, 16384, 65536}; // 1KB, 4KB, 16KB, 64KB
    size_t num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (size_t i = 0; i < num_sizes; i++) {
        size_t packet_size = test_sizes[i];
        uint8_t* plaintext = malloc(packet_size);
        uint8_t* ciphertext = malloc(packet_size + 32); // Extra space for encryption
        size_t ciphertext_len;

        // Fill with random data
        for (size_t j = 0; j < packet_size; j++) {
            plaintext[j] = rand() % 256;
        }

        // Measure encryption time
        clock_t start = clock();
        int result = crypto_handshake_encrypt_packet(&ctx, plaintext, packet_size,
                                                   ciphertext, packet_size + 32, &ciphertext_len);
        clock_t end = clock();

        cr_assert_eq(result, 0);

        double time_ms = ((double)(end - start)) / CLOCKS_PER_SEC * 1000;
        double throughput_mbps = (packet_size / (1024.0 * 1024.0)) / (time_ms / 1000.0);

        printf("Packet size: %zu bytes, Time: %.2f ms, Throughput: %.2f MB/s\n",
               packet_size, time_ms, throughput_mbps);

        // Encryption overhead should be < 5% for video
        cr_assert_lt(time_ms, packet_size / 1000.0 * 0.05); // 5% overhead max

        free(plaintext);
        free(ciphertext);
    }

    crypto_handshake_cleanup(&ctx);
}

Test(crypto_performance, memory_usage) {
    // Test for memory leaks using AddressSanitizer
    crypto_handshake_context_t ctx;

    for (int i = 0; i < 1000; i++) {
        crypto_handshake_init(&ctx, true);
        crypto_handshake_cleanup(&ctx);
    }

    // If we get here without crashing, no major memory leaks
    cr_assert(true);
}
```

**Create benchmark script**:

```bash
#!/bin/bash
# tests/scripts/benchmark_crypto.sh

echo "Running crypto performance benchmarks..."

# Test encryption overhead
echo "Testing encryption overhead..."
./tests/unit/crypto_performance_test

# Test with high frame rates
echo "Testing high frame rate performance..."
./ascii-chat-server --port 8080 &
SERVER_PID=$!
sleep 2

# Run client with high FPS
./ascii-chat-client --port 8080 --fps 60 --test-pattern &
CLIENT_PID=$!
sleep 10
kill $CLIENT_PID $SERVER_PID

echo "Performance benchmarks completed!"
```

#### 6.4 Cross-Platform Testing

**Create cross-platform test matrix**:

```yaml
# .github/workflows/crypto-test-matrix.yml
name: Crypto Cross-Platform Tests

on: [push, pull_request]

jobs:
  test:
    strategy:
      matrix:
        os: [ubuntu-20.04, ubuntu-22.04, macos-11, macos-12, windows-2019, windows-2022]
        compiler: [gcc, clang, msvc]
        crypto: [default, bearssl, libsodium]

    runs-on: ${{ matrix.os }}

    steps:
    - uses: actions/checkout@v3

    - name: Install dependencies
      run: |
        if [ "${{ matrix.os }}" = "ubuntu-20.04" ]; then
          sudo apt-get update
          sudo apt-get install -y libsodium-dev libbearssl-dev
        elif [ "${{ matrix.os }}" = "macos-11" ]; then
          brew install libsodium bearssl
        fi

    - name: Configure build
      run: |
        mkdir build
        cd build
        cmake .. -DCRYPTO_BACKEND=${{ matrix.crypto }}

    - name: Build
      run: |
        cd build
        make -j$(nproc)

    - name: Test
      run: |
        cd build
        ./tests/scripts/test_all_security_levels.sh
```

### Phase 7: Documentation & Polish (1 hour)

**Goal**: Update documentation, add examples, create migration guide

#### 7.1 Update README

**Add comprehensive crypto section**:

```markdown
# ASCII-Chat with End-to-End Encryption

ASCII-Chat now includes **end-to-end encryption by default** using modern cryptography (Ed25519/X25519). No configuration required - just run and chat securely!

## Quick Start

```bash
# Server (encrypted by default)
./ascii-chat-server

# Client (connects with encryption)
./ascii-chat-client
```

## Security Levels

### Level 1: Default Encrypted (Privacy)
```bash
# Server
./ascii-chat-server

# Client
./ascii-chat-client
```
✅ **Protects against**: Passive eavesdropping (ISP, WiFi snooping)
⚠️ **Vulnerable to**: Active MITM attacks

### Level 2: Password Authentication (Security)
```bash
# Server with password
./ascii-chat-server --key mypassword

# Client with matching password
./ascii-chat-client --key mypassword
```
✅ **Protects against**: Passive eavesdropping + Active MITM attacks
🔐 **Use case**: "I need actual security, not just privacy"

### Level 3: SSH Key Pinning (Strong Security)
```bash
# Server uses existing SSH key
./ascii-chat-server --ssh-key ~/.ssh/id_ed25519

# Client verifies server key
./ascii-chat-client --server-key github:zfogg
```
✅ **Protects against**: All attacks + Cryptographically verified identity
🔑 **Use case**: "Want SSH-like security, already have SSH keys"

### Level 4: Server Whitelist (Restricted Access)
```bash
# Server only accepts specific clients
./ascii-chat-server --client-keys github:alice,github:bob

# Client displays their key for server operator
./ascii-chat-client
```
✅ **Protects server from**: Unauthorized clients
📋 **Use case**: "Private server, only my friends can connect"

### Level 5: Defense in Depth (Maximum Security)
```bash
# Server: Password + SSH key + client whitelist
./ascii-chat-server --ssh-key ~/.ssh/id_ed25519 --key mypassword --client-keys ~/.ssh/authorized_keys

# Client: Password + server key verification
./ascii-chat-client --key mypassword --server-key github:zfogg
```
✅ **Maximum security**: Multiple layers of protection
🛡️ **Use case**: "Paranoid security for sensitive communications"

### Level 6: Opt-Out (Debugging Only)
```bash
# Disable encryption for debugging
./ascii-chat-server --no-encrypt
./ascii-chat-client --no-encrypt
```
⚠️ **No protection** - All traffic sent in plaintext
🔧 **Use case**: "I'm debugging and need to see raw packets"

## CLI Options

### Server Options
- `--ssh-key FILE` - Use existing SSH Ed25519 key (recommended)
- `--key PASSWORD` - Require password authentication
- `--keyfile FILE` - Password from file
- `--client-keys FORMAT` - Whitelist allowed clients
- `--no-encrypt` - Disable encryption (debugging only)

### Client Options
- `--server-key FORMAT` - Verify server identity
- `--key PASSWORD` - Provide password for authentication
- `--keyfile FILE` - Password from file
- `--no-encrypt` - Disable encryption (debugging only)

### Server Key Formats
- `github:username` - Fetch from GitHub
- `gitlab:username` - Fetch from GitLab
- `ssh-ed25519 AAAAC3...` - Direct SSH key
- `~/.ssh/id_ed25519.pub` - SSH public key file
- `64-char-hex-string` - Raw X25519 key

### Client Key Formats
- `github:user1,github:user2` - GitHub usernames
- `~/.ssh/authorized_keys` - SSH authorized_keys file
- `ssh-ed25519 AAAA...,ssh-ed25519 BBBB...` - Direct SSH keys

## Examples

### Basic Encrypted Chat
```bash
# Terminal 1: Start server
./ascii-chat-server

# Terminal 2: Connect client
./ascii-chat-client
```

### Secure Chat with Password
```bash
# Terminal 1: Server with password
./ascii-chat-server --key mysecretpassword

# Terminal 2: Client with same password
./ascii-chat-client --key mysecretpassword
```

### SSH Key Setup
```bash
# Generate SSH key (if you don't have one)
ssh-keygen -t ed25519 -C "your_email@example.com"

# Terminal 1: Server using SSH key
./ascii-chat-server --ssh-key ~/.ssh/id_ed25519

# Terminal 2: Client verifying server key
./ascii-chat-client --server-key ~/.ssh/id_ed25519.pub
```

### GitHub Key Verification
```bash
# Terminal 1: Server using SSH key
./ascii-chat-server --ssh-key ~/.ssh/id_ed25519

# Terminal 2: Client verifying via GitHub
./ascii-chat-client --server-key github:zfogg
```

### Server Whitelist
```bash
# Terminal 1: Server with client whitelist
./ascii-chat-server --client-keys github:alice,github:bob

# Terminal 2: Client (displays their key for server operator)
./ascii-chat-client
# Copy the displayed key to server operator

# Terminal 1: Server operator adds key to authorized_keys
echo "ssh-ed25519 AAAAC3... alice@laptop" >> ~/.ssh/authorized_keys
```

## Security Recommendations

### For Maximum Security
1. **Use SSH keys**: `--ssh-key ~/.ssh/id_ed25519`
2. **Verify server identity**: `--server-key github:username`
3. **Use client whitelist**: `--client-keys ~/.ssh/authorized_keys`
4. **Combine with password**: `--key mypassword`

### For Easy Setup
1. **Just use passwords**: `--key mypassword` (both sides)
2. **Save server keys**: Use known_hosts integration
3. **Share passwords securely**: Signal, phone call, in person

### Key Management
- **Generate Ed25519 keys**: `ssh-keygen -t ed25519`
- **Add to GitHub**: Upload your public key to GitHub
- **Use SSH agent**: `ssh-add ~/.ssh/id_ed25519`
- **Backup keys**: Store private keys securely

## Troubleshooting

### "GPG command not found"
```bash
# Install GPG
sudo apt-get install gnupg        # Ubuntu/Debian
brew install gnupg               # macOS
pacman -S gnupg                   # Arch Linux
```

### "Failed to verify server key"
- Check server is using the expected key
- Verify the key format is correct
- Try `--server-key github:username` instead of file path

### "Client not in whitelist"
- Server operator needs to add your public key
- Run client to see your public key
- Send key to server operator via secure channel

### "Connection failed during handshake"
- Check both sides have compatible crypto options
- Ensure no `--no-encrypt` on one side and encryption on the other
- Check network connectivity

## Migration Guide

### Upgrading from Unencrypted
1. **No changes needed** - encryption is enabled by default
2. **Add password for security**: `--key mypassword` (both sides)
3. **Use SSH keys for strong security**: `--ssh-key` and `--server-key`

### SSH Key Setup
```bash
# Generate new Ed25519 key
ssh-keygen -t ed25519 -C "your_email@example.com"

# Add to SSH agent
ssh-add ~/.ssh/id_ed25519

# Add to GitHub (optional)
# Upload ~/.ssh/id_ed25519.pub to GitHub Settings > SSH Keys
```

### GPG Setup (Optional)
```bash
# Generate GPG key
gpg --full-generate-key

# List keys
gpg --list-keys

# Use with ASCII-Chat
./ascii-chat-server --key gpg:0xYOUR_KEY_ID
```

## Technical Details

- **Encryption**: XSalsa20-Poly1305 (authenticated encryption)
- **Key Exchange**: X25519 Diffie-Hellman
- **Key Types**: Ed25519 (converted to X25519), X25519, GPG-derived
- **Password Hashing**: Argon2id
- **Forward Secrecy**: ✅ (ephemeral keys)
- **MITM Protection**: ✅ (SSH key pinning)
- **Performance**: <5% overhead for video

## Dependencies

- **Required**: libsodium (Ed25519/X25519 crypto)
- **Optional**: BearSSL (HTTPS for GitHub/GitLab), GPG (GPG key support)
- **Not used**: OpenSSL, RSA keys (modern crypto only)
```

#### 7.2 Create Examples

**Basic encrypted chat example**:

```bash
#!/bin/bash
# examples/basic_encrypted_chat.sh

echo "Starting basic encrypted chat example..."

# Terminal 1: Start server
echo "Server starting on port 8080..."
./ascii-chat-server --port 8080 &
SERVER_PID=$!

# Wait for server to start
sleep 2

# Terminal 2: Connect client
echo "Client connecting..."
./ascii-chat-client --port 8080 --test-pattern &
CLIENT_PID=$!

# Let them run for 10 seconds
sleep 10

# Cleanup
echo "Stopping chat..."
kill $CLIENT_PID $SERVER_PID
echo "Basic encrypted chat example completed!"
```

**SSH key setup example**:

```bash
#!/bin/bash
# examples/ssh_key_setup.sh

echo "SSH key setup example..."

# Generate SSH key if it doesn't exist
if [ ! -f ~/.ssh/id_ed25519 ]; then
    echo "Generating SSH Ed25519 key..."
    ssh-keygen -t ed25519 -C "ascii-chat@example.com" -f ~/.ssh/id_ed25519 -N ""
fi

# Start server with SSH key
echo "Server starting with SSH key..."
./ascii-chat-server --port 8080 --ssh-key ~/.ssh/id_ed25519 &
SERVER_PID=$!

sleep 2

# Connect client with server key verification
echo "Client connecting with server key verification..."
./ascii-chat-client --port 8080 --server-key ~/.ssh/id_ed25519.pub --test-pattern &
CLIENT_PID=$!

sleep 10

# Cleanup
kill $CLIENT_PID $SERVER_PID
echo "SSH key setup example completed!"
```

#### 7.3 Migration Guide

**Create migration documentation**:

```markdown
# Migration Guide: Adding Encryption to ASCII-Chat

## Overview

ASCII-Chat now includes **end-to-end encryption by default**. This guide helps you upgrade from unencrypted to encrypted chat.

## Automatic Migration

**Good news**: No changes required! Encryption is enabled by default.

```bash
# This now includes encryption automatically
./ascii-chat-server
./ascii-chat-client
```

## Security Upgrade Path

### Step 1: Basic Encryption (Default)
```bash
# Server
./ascii-chat-server

# Client
./ascii-chat-client
```
✅ **Protection**: Passive eavesdropping
⚠️ **Limitation**: Vulnerable to MITM attacks

### Step 2: Add Password Authentication
```bash
# Server
./ascii-chat-server --key mypassword

# Client
./ascii-chat-client --key mypassword
```
✅ **Protection**: Passive + Active attacks
🔐 **Security**: Password-based authentication

### Step 3: Use SSH Keys (Recommended)
```bash
# Generate SSH key (if needed)
ssh-keygen -t ed25519 -C "your_email@example.com"

# Server
./ascii-chat-server --ssh-key ~/.ssh/id_ed25519

# Client
./ascii-chat-client --server-key ~/.ssh/id_ed25519.pub
```
✅ **Protection**: Maximum security
🔑 **Security**: Cryptographically verified identity

## SSH Key Setup

### Generate New SSH Key
```bash
# Generate Ed25519 key (recommended)
ssh-keygen -t ed25519 -C "your_email@example.com"

# Add to SSH agent
ssh-add ~/.ssh/id_ed25519

# Test SSH key
ssh -T git@github.com
```

### Add to GitHub (Optional)
1. Copy your public key: `cat ~/.ssh/id_ed25519.pub`
2. Go to GitHub Settings > SSH Keys
3. Click "New SSH key"
4. Paste your public key
5. Use with ASCII-Chat: `--server-key github:username`

## GPG Setup (Advanced)

### Install GPG
```bash
# Ubuntu/Debian
sudo apt-get install gnupg

# macOS
brew install gnupg

# Arch Linux
pacman -S gnupg
```

### Generate GPG Key
```bash
# Generate GPG key
gpg --full-generate-key

# List keys
gpg --list-keys

# Use with ASCII-Chat
./ascii-chat-server --key gpg:0xYOUR_KEY_ID
```

## Server Configuration

### Basic Server
```bash
# Default encrypted server
./ascii-chat-server --port 8080
```

### Secure Server
```bash
# Server with SSH key and client whitelist
./ascii-chat-server --port 8080 --ssh-key ~/.ssh/id_ed25519 --client-keys ~/.ssh/authorized_keys
```

### Password-Protected Server
```bash
# Server with password authentication
./ascii-chat-server --port 8080 --key mypassword
```

## Client Configuration

### Basic Client
```bash
# Default encrypted client
./ascii-chat-client --port 8080
```

### Secure Client
```bash
# Client with server key verification
./ascii-chat-client --port 8080 --server-key github:zfogg
```

### Password Client
```bash
# Client with password authentication
./ascii-chat-client --port 8080 --key mypassword
```

## Troubleshooting

### "Encryption failed"
- Check both sides have compatible options
- Ensure no `--no-encrypt` on one side and encryption on the other
- Verify network connectivity

### "Server key verification failed"
- Check server is using the expected key
- Verify key format is correct
- Try `--server-key github:username` instead of file path

### "Client not authorized"
- Server operator needs to add your public key to whitelist
- Run client to see your public key
- Send key to server operator via secure channel

### "GPG command not found"
- Install GPG: `sudo apt-get install gnupg`
- Or use SSH keys instead: `--ssh-key ~/.ssh/id_ed25519`

## Performance Considerations

- **Encryption overhead**: <5% for video
- **Memory usage**: Minimal increase
- **CPU usage**: Negligible for modern hardware
- **Network**: Slight increase due to encryption

## Security Best Practices

1. **Use SSH keys**: More secure than passwords
2. **Verify server identity**: Always use `--server-key`
3. **Use client whitelist**: Restrict server access
4. **Share keys securely**: Signal, phone call, in person
5. **Keep keys updated**: Regenerate if compromised
6. **Use strong passwords**: If using password auth
7. **Backup keys**: Store private keys securely

## Rollback Plan

If you need to disable encryption temporarily:

```bash
# Server
./ascii-chat-server --no-encrypt

# Client
./ascii-chat-client --no-encrypt
```

⚠️ **Warning**: This disables all security - use only for debugging!
```

## Timeline Estimate

## Timeline Estimate

| Phase | Time | Cumulative |
|-------|------|------------|
| Phase 1: SSH/GPG Key Infrastructure + BearSSL Integration | 4 hours | 4 hours |
| Phase 2: CLI Flags + Display + GPG Integration | 2 hours | 6 hours |
| Phase 3: Known Hosts Integration | 2 hours | 8 hours |
| Phase 4: DH Key Exchange (Ed25519→X25519 only) | 2 hours | 10 hours |
| Phase 5: Encryption + Password/GPG Auth | 2 hours | 12 hours |
| Phase 6: Testing (Ed25519, X25519, GPG modes) | 2 hours | 14 hours |
| Phase 7: Documentation | 1 hour | 15 hours |

**Total**: 12-15 hours for complete implementation

**Time saved by dropping RSA**: ~6 hours (no OpenSSL/RSA complexity)

## Success Criteria

**Key Parsing & Conversion**:
- [ ] Parse SSH Ed25519 keys (ssh-ed25519 format)
- [ ] Parse raw X25519 keys (hex format)
- [ ] Parse GPG keys via `gpg` command integration
- [ ] Convert Ed25519 → X25519 using libsodium (one function call!)
- [ ] Warn users with RSA keys to regenerate with Ed25519

**Remote Key Fetching with BearSSL**:
- [ ] Fetch SSH keys from GitHub (`github:username`)
- [ ] Fetch SSH keys from GitLab (`gitlab:username`)
- [ ] Fetch GPG keys from GitHub (`github:username.gpg`)
- [ ] Fetch GPG keys from GitLab (`gitlab:username.gpg`)
- [ ] BearSSL HTTPS integration (stack-based, no malloc)
- [ ] Parse SSH authorized_keys files (Ed25519 only)

**Authentication**:
- [ ] Password authentication with `--key password` (works everywhere)
- [ ] Keyfile support with `--keyfile /path/to/file` (works everywhere)
- [ ] GPG key authentication with `--key gpg:0xABCD1234` (OPTIONAL - graceful fallback)
- [ ] GPG prompts for passphrase via pinentry (if gpg installed)
- [ ] Detect gpg availability with `is_gpg_available()`
- [ ] Helpful error messages when gpg not found

**Known Hosts & Identity**:
- [ ] Implement known_hosts behavior (save, verify, warn on change)
- [ ] Server can use Ed25519 SSH key with `--ssh-key ~/.ssh/id_ed25519`
- [ ] Client can verify with `--server-key github:username`
- [ ] Server can whitelist with `--client-keys github:alice,github:bob`

**Protocol & Encryption**:
- [ ] Default mode works (ephemeral Ed25519→X25519 DH, encrypted)
- [ ] Password mode works (challenge/response)
- [ ] GPG mode works (pinentry prompt, agent caching)
- [ ] Ed25519→X25519 DH key exchange
- [ ] All security levels work together (defense in depth)

**Testing & Quality**:
- [ ] MITM warnings display correctly
- [ ] No memory leaks (AddressSanitizer clean)
- [ ] All key types tested (Ed25519, X25519, GPG)
- [ ] BearSSL integration tested
- [ ] Cross-platform (Linux, macOS, Windows)
- [ ] README updated with modern crypto philosophy
- [ ] All existing tests still pass

---

**Ready to implement! Start with Phase 1 (SSH/GPG key infrastructure). 🔐**

**The vibe**:
> "Modern crypto only. Ed25519 or GTFO.
>
> Use your existing SSH keys. Verify servers like you verify GitHub.
> Authenticate with passwords or GPG (pinentry FTW).
>
> Stack-based everything. BearSSL does HTTPS with zero malloc.
> Pure libsodium DH. No OpenSSL bloat.
>
> If you have RSA keys: regenerate. It's 2025.
>
> No new tools, no custom formats, just crypto that works."

## Dependencies Summary

**Required dependencies:**
1. ✅ **libsodium** (already have it) - Ed25519↔X25519, XSalsa20-Poly1305, Argon2id
2. ✅ **BearSSL** (~200KB) - HTTPS for GitHub/GitLab key fetching

**Optional dependencies:**
3. ⭕ **gpg command** (system binary) - GPG key export, pinentry
   - Only needed if using `--key gpg:...` or `github:username.gpg`
   - Gracefully skipped if not installed
   - All other auth methods work without it

**What we're NOT using:**
- ❌ OpenSSL (~3MB, malloc-heavy, RSA bloat)
- ❌ libcurl (~500KB, complex API)
- ❌ Any RSA code whatsoever

**Total required dependencies:** 1 (BearSSL)
**Total optional dependencies:** 1 (gpg command)
