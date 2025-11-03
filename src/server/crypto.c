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

#include "client.h"
#include "crypto.h"

#include "options.h"
#include "common.h"
#include "crypto/handshake.h"
#include "crypto/crypto.h"
#include "crypto/keys/keys.h"

#include <string.h>
#include <stdio.h>
#include <sodium.h>

// External references to global server crypto state
extern bool g_server_encryption_enabled;
extern private_key_t g_server_private_key;

// External references to client whitelist (defined in main.c)
extern public_key_t g_client_whitelist[];
extern size_t g_num_whitelisted_clients;

// Per-client crypto contexts are now stored in client_info_t structure
// No global crypto context needed

/**
 * Initialize server crypto system (global initialization)
 *
 * @return 0 on success, -1 on failure
 */
int server_crypto_init(void) {
  // Check if encryption is disabled
  if (opt_no_encrypt) {
    log_info("Encryption disabled via --no-encrypt");
    return 0;
  }

  log_info("Server crypto system initialized (per-client contexts will be created on demand)");
  return 0;
}

/**
 * Perform crypto handshake with client
 *
 * @param client Client info structure
 * @return 0 on success, -1 on failure
 */
int server_crypto_handshake(client_info_t *client) {
  if (opt_no_encrypt) {
    log_debug("Crypto handshake skipped (disabled)");
    return 0;
  }

  if (!client) {
    FATAL(ERROR_CRYPTO_HANDSHAKE, "Client is NULL for crypto handshake");
    return -1;
  }

  // Initialize crypto context for this specific client
  int init_result;
  if (strlen(opt_password) > 0) {
    // Password provided - use password-based encryption (even if SSH key is also provided)
    log_debug("SERVER_CRYPTO_HANDSHAKE: Using password-based encryption");
    init_result =
        crypto_handshake_init_with_password(&client->crypto_handshake_ctx, true, opt_password); // true = server
  } else {
    // Server has SSH key - use standard initialization
    log_debug("SERVER_CRYPTO_HANDSHAKE: Using passwordless-based encryption");
    init_result = crypto_handshake_init(&client->crypto_handshake_ctx, true); // true = server
  }

  if (init_result != ASCIICHAT_OK) {
    FATAL(init_result, "Failed to initialize crypto handshake for client %u", atomic_load(&client->client_id));
  }
  client->crypto_initialized = true;

  // Set up server keys in the handshake context
  if (g_server_encryption_enabled && g_server_private_key.type == KEY_TYPE_ED25519) {
    // Copy server private key to handshake context for signing
    memcpy(&client->crypto_handshake_ctx.server_private_key, &g_server_private_key, sizeof(private_key_t));

    // Extract Ed25519 public key from private key for identity
    client->crypto_handshake_ctx.server_public_key.type = KEY_TYPE_ED25519;
    memcpy(client->crypto_handshake_ctx.server_public_key.key, g_server_private_key.public_key,
           ED25519_PUBLIC_KEY_SIZE);

    // SSH key is already configured in the handshake context above
    // No additional setup needed - SSH keys are used only for authentication

    log_debug("Server identity keys configured for client %u", atomic_load(&client->client_id));
  }

  // Set up client whitelist if specified
  if (g_num_whitelisted_clients > 0) {
    client->crypto_handshake_ctx.require_client_auth = true;
    client->crypto_handshake_ctx.client_whitelist = g_client_whitelist;
    client->crypto_handshake_ctx.num_whitelisted_clients = g_num_whitelisted_clients;
    log_info("Client whitelist enabled: %zu authorized keys", g_num_whitelisted_clients);
  }

  log_info("Starting crypto handshake with client %u...", atomic_load(&client->client_id));

  // Step 0a: Receive client's protocol version
  log_debug("SERVER_CRYPTO_HANDSHAKE: Receiving client protocol version");
  packet_type_t packet_type;
  void *payload = NULL;
  size_t payload_len = 0;

  log_debug("SERVER_CRYPTO_HANDSHAKE: About to receive packet from client %u", atomic_load(&client->client_id));

  // Protect socket access during crypto handshake
  mutex_lock(&client->client_state_mutex);
  socket_t socket = client->socket;
  mutex_unlock(&client->client_state_mutex);

  if (socket == INVALID_SOCKET_VALUE) {
    log_debug("SERVER_CRYPTO_HANDSHAKE: Socket is invalid for client %u", atomic_load(&client->client_id));
    return -1;
  }

  int result = receive_packet(socket, &packet_type, &payload, &payload_len);
  log_debug("SERVER_CRYPTO_HANDSHAKE: Received packet from client %u: result=%d, type=%u",
            atomic_load(&client->client_id), result, packet_type);

  // Handle client disconnection gracefully
  if (result != ASCIICHAT_OK) {
    log_info("Client %u disconnected during crypto handshake (connection error)", atomic_load(&client->client_id));
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return -1; // Return error but don't crash the server
  }

  if (packet_type != PACKET_TYPE_PROTOCOL_VERSION) {
    log_error("Server received packet type 0x%x (decimal %u) - Expected 0x%x (decimal %d)", packet_type, packet_type,
              PACKET_TYPE_PROTOCOL_VERSION, PACKET_TYPE_PROTOCOL_VERSION);
    log_error("This suggests a protocol mismatch or packet corruption");
    log_error("Raw packet type bytes: %02x %02x %02x %02x", (packet_type >> 0) & 0xFF, (packet_type >> 8) & 0xFF,
              (packet_type >> 16) & 0xFF, (packet_type >> 24) & 0xFF);
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    log_info("Client %u disconnected due to protocol mismatch", atomic_load(&client->client_id));
    return -1; // Return error but don't crash the server
  }

  if (payload_len != sizeof(protocol_version_packet_t)) {
    log_error("Invalid protocol version packet size: %zu, expected %zu", payload_len,
              sizeof(protocol_version_packet_t));
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  protocol_version_packet_t client_version;
  memcpy(&client_version, payload, sizeof(protocol_version_packet_t));
  log_debug("SERVER_CRYPTO_HANDSHAKE: About to free payload for client %u", atomic_load(&client->client_id));
  buffer_pool_free(payload, payload_len);
  log_debug("SERVER_CRYPTO_HANDSHAKE: Payload freed for client %u", atomic_load(&client->client_id));

  // Convert from network byte order
  uint16_t client_proto_version = ntohs(client_version.protocol_version);
  uint16_t client_proto_revision = ntohs(client_version.protocol_revision);

  log_info("Client %u protocol version: %u.%u (encryption: %s)", atomic_load(&client->client_id), client_proto_version,
           client_proto_revision, client_version.supports_encryption ? "yes" : "no");

  log_debug("SERVER_CRYPTO_HANDSHAKE: About to check encryption support for client %u",
            atomic_load(&client->client_id));

  if (!client_version.supports_encryption) {
    log_error("Client %u does not support encryption", atomic_load(&client->client_id));
    log_info("Client %u disconnected - encryption not supported", atomic_load(&client->client_id));
    return -1; // Return error but don't crash the server
  }

  // Step 0b: Send our protocol version to client
  log_debug("SERVER_CRYPTO_HANDSHAKE: About to prepare server protocol version for client %u",
            atomic_load(&client->client_id));
  protocol_version_packet_t server_version = {0};
  log_debug("SERVER_CRYPTO_HANDSHAKE: Initialized server_version struct for client %u",
            atomic_load(&client->client_id));
  server_version.protocol_version = htons(1);  // Protocol version 1
  server_version.protocol_revision = htons(0); // Revision 0
  server_version.supports_encryption = 1;      // We support encryption
  server_version.compression_algorithms = 0;   // No compression for now
  server_version.compression_threshold = 0;
  server_version.feature_flags = 0;

  log_debug("SERVER_CRYPTO_HANDSHAKE: About to call send_protocol_version_packet for client %u",
            atomic_load(&client->client_id));
  result = send_protocol_version_packet(socket, &server_version);
  log_debug("SERVER_CRYPTO_HANDSHAKE: send_protocol_version_packet returned %d for client %u", result,
            atomic_load(&client->client_id));
  if (result != 0) {
    log_error("Failed to send protocol version to client %u", atomic_load(&client->client_id));
    log_info("Client %u disconnected - failed to send protocol version", atomic_load(&client->client_id));
    return -1; // Return error but don't crash the server
  }
  log_debug("SERVER_CRYPTO_HANDSHAKE: Protocol version sent successfully to client %u",
            atomic_load(&client->client_id));

  // Step 0c: Receive client's crypto capabilities
  payload = NULL;
  payload_len = 0;

  result = receive_packet(socket, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    log_info("Client %u disconnected during crypto capabilities exchange", atomic_load(&client->client_id));
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return -1; // Return error but don't crash the server
  }

  if (packet_type != PACKET_TYPE_CRYPTO_CAPABILITIES) {
    log_error("Server received packet type 0x%x (decimal %u) - Expected 0x%x (decimal %d)", packet_type, packet_type,
              PACKET_TYPE_CRYPTO_CAPABILITIES, PACKET_TYPE_CRYPTO_CAPABILITIES);
    log_error("Raw packet type bytes: %02x %02x %02x %02x", (packet_type >> 0) & 0xFF, (packet_type >> 8) & 0xFF,
              (packet_type >> 16) & 0xFF, (packet_type >> 24) & 0xFF);
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    log_info("Client %u disconnected due to protocol mismatch in crypto capabilities", atomic_load(&client->client_id));
    return -1; // Return error but don't crash the server
  }

  if (payload_len != sizeof(crypto_capabilities_packet_t)) {
    log_error("Invalid crypto capabilities packet size: %zu, expected %zu", payload_len,
              sizeof(crypto_capabilities_packet_t));
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    FATAL(ERROR_CRYPTO_HANDSHAKE, "Invalid crypto capabilities packet size: %zu, expected %zu", payload_len,
          sizeof(crypto_capabilities_packet_t));
  }

  crypto_capabilities_packet_t client_caps;
  memcpy(&client_caps, payload, sizeof(crypto_capabilities_packet_t));
  buffer_pool_free(payload, payload_len);

  // Convert from network byte order
  uint16_t supported_kex = ntohs(client_caps.supported_kex_algorithms);
  uint16_t supported_auth = ntohs(client_caps.supported_auth_algorithms);
  uint16_t supported_cipher = ntohs(client_caps.supported_cipher_algorithms);

  log_info("Client %u crypto capabilities: KEX=0x%04x, Auth=0x%04x, Cipher=0x%04x", atomic_load(&client->client_id),
           supported_kex, supported_auth, supported_cipher);

  // Step 0d: Select crypto algorithms and send parameters to client
  crypto_parameters_packet_t server_params = {0};

  // Select algorithms (for now, we only support X25519 + Ed25519 + XSalsa20-Poly1305)
  server_params.selected_kex = KEX_ALGO_X25519;
  server_params.selected_cipher = CIPHER_ALGO_XSALSA20_POLY1305;

  // Select authentication algorithm based on server configuration
  // Note: Password authentication is not a separate algorithm - it's a mode of operation
  // that affects key derivation. The authentication algorithm refers to signature verification.
  //
  // CRITICAL: We require Ed25519 authentication if:
  //   - Server has an identity key (g_server_encryption_enabled AND g_server_private_key is Ed25519)
  //   - This is needed to send authenticated KEY_EXCHANGE_INIT with identity + signature
  //   - Client whitelist verification happens during authentication phase, not key exchange
  if (g_server_encryption_enabled && g_server_private_key.type == KEY_TYPE_ED25519) {
    // SSH key authentication (Ed25519 signatures) - server has identity key
    server_params.selected_auth = AUTH_ALGO_ED25519;
  } else {
    // No signature-based authentication during key exchange
    // Client authentication will be required during auth phase if whitelist is enabled
    server_params.selected_auth = AUTH_ALGO_NONE;
  }

  // Set verification flag based on client whitelist
  server_params.verification_enabled = (g_num_whitelisted_clients > 0) ? 1 : 0;

  // Set crypto parameters for current algorithms
  server_params.kex_public_key_size = CRYPTO_PUBLIC_KEY_SIZE; // X25519 public key size

  // Only set auth/signature sizes if we're using authentication
  if (server_params.selected_auth == AUTH_ALGO_ED25519) {
    server_params.auth_public_key_size = ED25519_PUBLIC_KEY_SIZE; // Ed25519 public key size
    server_params.signature_size = ED25519_SIGNATURE_SIZE;        // Ed25519 signature size
  } else {
    server_params.auth_public_key_size = 0; // No authentication
    server_params.signature_size = 0;       // No signature
  }

  server_params.shared_secret_size = CRYPTO_PUBLIC_KEY_SIZE; // X25519 shared secret size
  server_params.nonce_size = CRYPTO_NONCE_SIZE;              // XSalsa20 nonce size
  server_params.mac_size = CRYPTO_MAC_SIZE;                  // Poly1305 MAC size
  server_params.hmac_size = CRYPTO_HMAC_SIZE;                // HMAC-SHA256 size

  log_debug("SERVER_CRYPTO_HANDSHAKE: Sending crypto parameters to client %u", atomic_load(&client->client_id));
  result = send_crypto_parameters_packet(socket, &server_params);
  if (result != 0) {
    log_error("Failed to send crypto parameters to client %u", atomic_load(&client->client_id));
    return -1;
  }
  log_info("Server selected crypto for client %u: KEX=%u, Auth=%u, Cipher=%u", atomic_load(&client->client_id),
           server_params.selected_kex, server_params.selected_auth, server_params.selected_cipher);

  // Set the crypto parameters in the handshake context
  result = crypto_handshake_set_parameters(&client->crypto_handshake_ctx, &server_params);
  if (result != ASCIICHAT_OK) {
    FATAL(result, "Failed to set crypto parameters for client %u", atomic_load(&client->client_id));
  }

  // Step 1: Send our public key to client
  log_debug("About to call crypto_handshake_server_start");
  result = crypto_handshake_server_start(&client->crypto_handshake_ctx, socket);
  if (result != ASCIICHAT_OK) {
    FATAL(result, "Failed to send server public key to client %u", atomic_load(&client->client_id));
  }

  // Step 2: Receive client's public key and send auth challenge
  result = crypto_handshake_server_auth_challenge(&client->crypto_handshake_ctx, socket);
  if (result != ASCIICHAT_OK) {
    log_error("Crypto authentication challenge failed for client %u: %s", atomic_load(&client->client_id),
              asciichat_error_string(result));
    return -1; // Return error to disconnect client gracefully
  }

  // Check if handshake completed during auth challenge (no authentication needed)
  if (client->crypto_handshake_ctx.state == CRYPTO_HANDSHAKE_READY) {
    log_info("Crypto handshake completed successfully for client %u (no authentication)",
             atomic_load(&client->client_id));
    return 0;
  }

  // Step 3: Receive auth response and complete handshake
  result = crypto_handshake_server_complete(&client->crypto_handshake_ctx, socket);
  if (result != ASCIICHAT_OK) {
    // Handle network errors (like client disconnection) gracefully
    if (result == ERROR_NETWORK) {
      log_info("Client %u disconnected during authentication", atomic_load(&client->client_id));
      return -1; // Return error but don't crash the server
    }
    FATAL(result, "Crypto authentication response failed for client %u", atomic_load(&client->client_id));
  }

  log_info("Crypto handshake completed successfully for client %u", atomic_load(&client->client_id));
  return 0;
}

/**
 * Check if crypto handshake is ready for a specific client
 *
 * @param client_id Client ID to check
 * @return true if encryption is ready, false otherwise
 */
bool crypto_server_is_ready(uint32_t client_id) {
  if (opt_no_encrypt) {
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
const crypto_context_t *crypto_server_get_context(uint32_t client_id) {
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
int crypto_server_encrypt_packet(uint32_t client_id, const uint8_t *plaintext, size_t plaintext_len,
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
int crypto_server_decrypt_packet(uint32_t client_id, const uint8_t *ciphertext, size_t ciphertext_len,
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
void crypto_server_cleanup_client(uint32_t client_id) {
  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    return;
  }

  if (client->crypto_initialized) {
    crypto_handshake_cleanup(&client->crypto_handshake_ctx);
    client->crypto_initialized = false;
    log_debug("Crypto handshake cleaned up for client %u", client_id);
  }
}
