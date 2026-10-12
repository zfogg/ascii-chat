/**
 * @file server/crypto.c
 * @ingroup server_crypto
 * @brief 🔐 Server cryptography: per-client handshake, X25519 key exchange, and session encryption management
 *
 * 1. Initialize server crypto system and validate encryption configuration
 * 2. Perform cryptographic handshake with each connecting client
 * 3. Manage per-client crypto contexts stored in client_info_t structures
 * 4. Provide encryption/decryption functions for secure packet transmission
 * 5. Support multiple authentication modes (password, SSH key, passwordless)
 * 6. Integrate with client whitelist for authenticated access control
 *
 * CRYPTOGRAPHIC HANDSHAKE ARCHITECTURE:
 * ======================================
 * The handshake follows a multi-phase protocol:
 *
 * PHASE 0: PROTOCOL NEGOTIATION:
 * - Step 0a: Receive client protocol version
 * - Step 0b: Send server protocol version
 * - Step 0c: Receive client crypto capabilities
 * - Step 0d: Select algorithms and send crypto parameters
 *
 * PHASE 1: KEY EXCHANGE:
 * - Step 1: Send server's ephemeral public key (X25519)
 * - Server generates ephemeral key pair for this client session
 * - Both sides derive shared secret using X25519 key exchange
 *
 * PHASE 2: AUTHENTICATION:
 * - Step 2: Receive client's public key and send auth challenge
 * - Server verifies client identity (if whitelist enabled)
 * - Server signs challenge with identity key (if server has identity key)
 * - Step 3: Receive auth response and complete handshake
 *
 * SUPPORTED AUTHENTICATION MODES:
 * ================================
 * The server supports three authentication modes:
 *
 * 1. PASSWORD AUTHENTICATION:
 *    - Uses Argon2id key derivation from shared password
 *    - Both server and client derive same key from password
 *    - No identity keys required (password-only mode)
 *
 * 2. SSH KEY AUTHENTICATION:
 *    - Server uses Ed25519 private key for identity verification
 *    - Client provides Ed25519 public key for authentication
 *    - Identity verification via known_hosts and whitelist
 *
 * 3. PASSWORDLESS MODE:
 *    - Ephemeral keys only (no long-term identity)
 *    - Key exchange provides confidentiality but not authentication
 *    - Suitable for trusted networks or testing
 *
 * PER-CLIENT CRYPTO CONTEXTS:
 * ===========================
 * Each client has an independent crypto context stored in client_info_t:
 * - crypto_handshake_ctx: Handshake state machine and cryptographic operations
 * - crypto_initialized: Flag indicating handshake completion
 * - Context is created during connection and cleaned up on disconnect
 *
 * INTEGRATION WITH CLIENT WHITELIST:
 * ==================================
 * When client whitelist is enabled:
 * - Server requires client authentication during handshake
 * - Client public key must be in whitelist array
 * - Verification happens in crypto_handshake_server_auth_challenge()
 * - Clients not in whitelist are rejected during handshake
 *
 * ENCRYPTION/DECRYPTION OPERATIONS:
 * ==================================
 * After handshake completion:
 * - crypto_server_encrypt_packet(): Encrypts packets before transmission
 * - crypto_server_decrypt_packet(): Decrypts received packets
 * - Both functions use per-client crypto contexts
 * - Automatic passthrough when encryption disabled (--no-encrypt)
 *
 * ALGORITHM SUPPORT:
 * ==================
 * The server currently supports:
 * - Key Exchange: X25519 (Elliptic Curve Diffie-Hellman)
 * - Cipher: XSalsa20-Poly1305 (Authenticated Encryption)
 * - Authentication: Ed25519 (when server has identity key)
 * - Key Derivation: Argon2id (for password-based authentication)
 * - HMAC: HMAC-SHA256 (for additional integrity protection)
 *
 * ERROR HANDLING:
 * ==============
 * Handshake errors are handled gracefully:
 * - Client disconnection during handshake: Log and return error
 * - Protocol mismatch: Log detailed error and disconnect client
 * - Authentication failure: Log and disconnect client (whitelist rejection)
 * - Network errors: Detect and handle gracefully (don't crash server)
 * - Invalid packets: Validate size and format before processing
 *
 * THREAD SAFETY:
 * ==============
 * Crypto operations are thread-safe:
 * - Each client has independent crypto context (no shared state)
 * - Socket access protected by client_state_mutex
 * - Per-client encryption/decryption operations are isolated
 * - Global server crypto state (g_server_private_key) read-only after init
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - main.c: Calls server_crypto_init() during server startup
 * - client.c: Calls server_crypto_handshake() for each new client
 * - protocol.c: Uses encryption functions for secure packet transmission
 * - crypto/handshake.h: Core handshake protocol implementation
 * - crypto/keys/keys.h: Key management and parsing functions
 *
 * WHY THIS MODULAR DESIGN:
 * =========================
 * The original server.c mixed cryptographic operations with connection
 * management and packet processing, making it difficult to:
 * - Add new authentication methods
 * - Modify handshake protocol
 * - Debug cryptographic issues
 * - Test encryption/decryption independently
 *
 * This separation provides:
 * - Clear cryptographic interface
 * - Easier handshake protocol evolution
 * - Better error isolation
 * - Improved security auditing
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 * @see client.c For client lifecycle management and crypto context storage
 * @see crypto/handshake.h For handshake protocol implementation
 * @see crypto/keys/keys.h For key parsing and management
 */

#include <ascii-chat/crypto/key_identity/display.h>
#include "main.h"
#include "client.h"
#include "crypto.h"
#include <ascii-chat/network/acip/send.h>

#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/network/mdns/discovery.h> // For pubkey_to_hex
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/endian.h>

#include <string.h>
#include <stdio.h>
#include <sodium.h>

// Per-client crypto contexts are now stored in client_info_t structure
// No global crypto context needed

/**
 * Initialize server crypto system (global initialization)
 *
 * @return 0 on success, -1 on failure
 */
int server_crypto_init(void) {
  // Only skip handshake if BOTH encryption and authentication are disabled
  bool no_encrypt = GET_OPTION(no_encrypt);
  bool no_auth = GET_OPTION(no_auth);

  if (no_encrypt && no_auth) {
    log_info("ACIP handshake disabled (both encryption and authentication disabled)");
    return 0;
  }

  log_debug("Server crypto system initialized (per-client contexts will be created on demand)");
  return 0;
}

/**
 * Advance the server handshake for one complete packet.
 * Steps negotiate the protocol, select an identity, exchange keys, and verify
 * authentication. Only a READY context is published to application senders.
 * A single-key server also accepts older clients that omit CLIENT_HELLO.
 */
asciichat_error_t server_crypto_event(client_info_t *client, unsigned *step, packet_type_t type, const void *payload,
                                      size_t length, bool *ready) {
  *ready = false;
  crypto_handshake_context_t *ctx = &client->crypto_handshake_ctx;
  acip_transport_t *transport = client->transport;
  asciichat_error_t result = ASCIICHAT_OK;
  if (*step == 0) {
    if (type != PACKET_TYPE_PROTOCOL_VERSION) {
      if (type != PACKET_TYPE_CLIENT_CAPABILITIES || !GET_OPTION(no_encrypt) || !GET_OPTION(no_auth))
        return SET_ERRNO(ERROR_CRYPTO, "Expected protocol version before application traffic");
      *ready = true;
      return ASCIICHAT_OK;
    }
    if (length != sizeof(protocol_version_packet_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid protocol version size");
    protocol_version_packet_t version;
    memcpy(&version, payload, sizeof(version));
    if (version.supports_encryption == ACIP_CRYPTO_NONE)
      return SET_ERRNO(ERROR_CRYPTO, "Client does not support crypto negotiation");
    const options_t *opts = options_get();
    const char *password = opts ? opts->password : "";
    result = password[0] ? crypto_handshake_init_with_password("server_async", ctx, true, password)
                         : crypto_handshake_init("server_async", ctx, true);
    if (result != ASCIICHAT_OK)
      return result;
    client->encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(version.supports_encryption);
    if (g_server_encryption_enabled && g_server_private_key.type == KEY_TYPE_ED25519) {
      ctx->server_private_key = g_server_private_key;
      ctx->server_public_key.type = KEY_TYPE_ED25519;
      memcpy(ctx->server_public_key.key, g_server_private_key.public_key, sizeof(g_server_private_key.public_key));
    }
    ctx->require_client_auth = g_num_whitelisted_clients > 0 || GET_OPTION(require_server_verify);
    ctx->client_whitelist = g_client_whitelist;
    ctx->num_whitelisted_clients = g_num_whitelisted_clients;
    version.protocol_version = HOST_TO_NET_U16(1);
    version.protocol_revision = 0;
    version.compression_algorithms = 0;
    version.compression_threshold = 0;
    version.feature_flags = HOST_TO_NET_U16(PROTOCOL_FEATURE_RECOVERABLE_ERRORS);
    result = packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version), 0);
    *step = 1;
    return result;
  }
  if (*step == 1) {
    if (type != PACKET_TYPE_CRYPTO_CAPABILITIES || length != sizeof(crypto_capabilities_packet_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected crypto capabilities");
    crypto_capabilities_packet_t capabilities;
    memcpy(&capabilities, payload, sizeof(capabilities));
    crypto_parameters_packet_t parameters = {0};
    parameters.selected_kex = KEX_ALGO_X25519;
    parameters.selected_cipher = client->encrypt_data ? CIPHER_ALGO_XSALSA20_POLY1305 : CIPHER_ALGO_NONE;
    parameters.selected_auth = g_server_encryption_enabled && g_server_private_key.type == KEY_TYPE_ED25519
                                   ? AUTH_ALGO_ED25519
                                   : AUTH_ALGO_NONE;
    parameters.verification_enabled = g_num_whitelisted_clients > 0;
    parameters.kex_public_key_size = CRYPTO_PUBLIC_KEY_SIZE;
    parameters.shared_secret_size = CRYPTO_PUBLIC_KEY_SIZE;
    parameters.nonce_size = CRYPTO_NONCE_SIZE;
    parameters.mac_size = CRYPTO_MAC_SIZE;
    parameters.hmac_size = CRYPTO_HMAC_SIZE;
    if (parameters.selected_auth == AUTH_ALGO_ED25519) {
      parameters.auth_public_key_size = ED25519_PUBLIC_KEY_SIZE;
      parameters.signature_size = ED25519_SIGNATURE_SIZE;
    }
    result = crypto_handshake_set_parameters(ctx, &parameters);
    if (result != ASCIICHAT_OK)
      return result;
    parameters.kex_public_key_size = HOST_TO_NET_U16(parameters.kex_public_key_size);
    parameters.auth_public_key_size = HOST_TO_NET_U16(parameters.auth_public_key_size);
    parameters.signature_size = HOST_TO_NET_U16(parameters.signature_size);
    parameters.shared_secret_size = HOST_TO_NET_U16(parameters.shared_secret_size);
    result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_PARAMETERS, &parameters, sizeof(parameters), 0);
    if (result != ASCIICHAT_OK)
      return result;
    if (g_num_server_identity_keys > 0 && capabilities.requires_verification) {
      *step = 2;
      return ASCIICHAT_OK;
    }
    *step = 3;
    return crypto_handshake_server_start(ctx, transport);
  }
  if (*step == 2) {
    if (type != PACKET_TYPE_CRYPTO_CLIENT_HELLO) {
      if (g_num_server_identity_keys != 1 || type != PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP)
        return SET_ERRNO(ERROR_CRYPTO, "Expected pinned server identity");
      // Keep this packet for the key-exchange step after sending the default key.
      ctx->server_private_key = g_server_identity_keys[0];
      ctx->server_public_key.type = KEY_TYPE_ED25519;
      memcpy(ctx->server_public_key.key, g_server_identity_keys[0].public_key,
             sizeof(g_server_identity_keys[0].public_key));
      result = crypto_handshake_server_start(ctx, transport);
      if (result != ASCIICHAT_OK)
        return result;
      *step = 3;
    } else {
      if (length != ED25519_PUBLIC_KEY_SIZE)
        return SET_ERRNO(ERROR_CRYPTO, "Expected pinned server identity");
      bool found = false;
      for (size_t i = 0; i < g_num_server_identity_keys; ++i) {
        if (sodium_memcmp(payload, g_server_identity_keys[i].public_key, ED25519_PUBLIC_KEY_SIZE) == 0) {
          ctx->server_private_key = g_server_identity_keys[i];
          ctx->server_public_key.type = KEY_TYPE_ED25519;
          memcpy(ctx->server_public_key.key, g_server_identity_keys[i].public_key,
                 sizeof(g_server_identity_keys[i].public_key));
          found = true;
          break;
        }
      }
      if (!found)
        return SET_ERRNO(ERROR_CRYPTO, "Unknown pinned server identity");
      *step = 3;
      return crypto_handshake_server_start(ctx, transport);
    }
  }
  if (*step == 3) {
    result = crypto_handshake_server_auth_challenge(ctx, transport, type, payload, length);
    *step = 4;
  } else if (*step == 4) {
    result = crypto_handshake_server_complete(ctx, transport, type, payload, length);
  } else {
    return SET_ERRNO(ERROR_INVALID_STATE, "Unexpected server handshake step");
  }
  if (result == ASCIICHAT_OK && ctx->state == CRYPTO_HANDSHAKE_READY) {
    client->crypto_initialized = true;
    ctx->crypto_ctx.encrypt_data = client->encrypt_data;
    transport->crypto_ctx = (crypto_context_t *)crypto_handshake_get_context(ctx);
    *ready = true;
    *step = 5;
  }
  return result;
}

bool crypto_server_is_ready(const char *client_id) {
  if (GET_OPTION(no_encrypt)) {
    return false;
  }

  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    return false;
  }

  if (!client->crypto_initialized) {
    return false;
  }

  bool ready = crypto_handshake_is_ready(&client->crypto_handshake_ctx);
  return ready;
}

/**
 * Get crypto context for encryption/decryption for a specific client
 *
 * @param client_id Client ID to get context for
 * @return crypto context or NULL if not ready
 */
const crypto_context_t *crypto_server_get_context(const char *client_id) {
  if (!crypto_server_is_ready(client_id)) {
    return NULL;
  }

  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    return NULL;
  }

  return crypto_handshake_get_context(&client->crypto_handshake_ctx);
}

/**
 * Encrypt a packet for transmission to a specific client
 *
 * @param client_id Client ID to encrypt for
 * @param plaintext Plaintext data to encrypt
 * @param plaintext_len Length of plaintext data
 * @param ciphertext Output buffer for encrypted data
 * @param ciphertext_size Size of output buffer
 * @param ciphertext_len Output length of encrypted data
 * @return 0 on success, -1 on failure
 */
int crypto_server_encrypt_packet(const char *client_id, const uint8_t *plaintext, size_t plaintext_len,
                                 uint8_t *ciphertext, size_t ciphertext_size, size_t *ciphertext_len) {
  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    return -1;
  }

  return crypto_encrypt_packet_or_passthrough(&client->crypto_handshake_ctx, crypto_server_is_ready(client_id),
                                              plaintext, plaintext_len, ciphertext, ciphertext_size, ciphertext_len);
}

/**
 * Decrypt a received packet from a specific client
 *
 * @param client_id Client ID that sent the packet
 * @param ciphertext Encrypted data to decrypt
 * @param ciphertext_len Length of encrypted data
 * @param plaintext Output buffer for decrypted data
 * @param plaintext_size Size of output buffer
 * @param plaintext_len Output length of decrypted data
 * @return 0 on success, -1 on failure
 */
int crypto_server_decrypt_packet(const char *client_id, const uint8_t *ciphertext, size_t ciphertext_len,
                                 uint8_t *plaintext, size_t plaintext_size, size_t *plaintext_len) {
  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    return -1;
  }

  return crypto_decrypt_packet_or_passthrough(&client->crypto_handshake_ctx, crypto_server_is_ready(client_id),
                                              ciphertext, ciphertext_len, plaintext, plaintext_size, plaintext_len);
}

/**
 * Cleanup crypto resources for a specific client
 *
 * @param client_id Client ID to cleanup crypto for
 */
void crypto_server_cleanup_client(const char *client_id) {
  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    return;
  }

  if (client->crypto_initialized) {
    crypto_handshake_destroy(&client->crypto_handshake_ctx);
    client->crypto_initialized = false;
    log_debug("Crypto handshake cleaned up for client %s", client_id);
  }
}
