#include "connection_internal.h"
#include <ascii-chat/network/errors.h>
/**
 * @file client/crypto.c
 * @ingroup client_crypto
 * @brief 🔐 Client cryptography: handshake integration, X25519 key exchange, and per-session encryption
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Initialize client crypto context with authentication credentials
 * 2. Perform cryptographic handshake with server during connection
 * 3. Manage global crypto context for client connection
 * 4. Provide encryption/decryption functions for secure packet transmission
 * 5. Support multiple authentication modes (password, SSH key, passwordless)
 * 6. Handle session rekeying for long-lived connections
 *
 * CRYPTOGRAPHIC HANDSHAKE ARCHITECTURE:
 * ======================================
 * The handshake follows a multi-phase protocol:
 *
 * PHASE 0: PROTOCOL NEGOTIATION:
 * - Step 0a: Send client protocol version
 * - Step 0b: Receive server protocol version
 * - Step 0c: Send client crypto capabilities
 * - Step 0d: Receive server crypto parameters
 *
 * PHASE 1: KEY EXCHANGE:
 * - Step 1: Receive server's ephemeral public key and send our public key
 * - Client generates ephemeral key pair for this session
 * - Both sides derive shared secret using X25519 key exchange
 *
 * PHASE 2: AUTHENTICATION:
 * - Step 2: Receive auth challenge and send response
 * - Client signs challenge with identity key (if client has identity key)
 * - Server verifies client identity (if whitelist enabled)
 * - Step 3: Receive handshake complete message
 *
 * CRYPTO INITIALIZATION:
 * ======================
 * The client supports three initialization modes:
 *
 * 1. SSH KEY MODE (--key specified):
 *    - Parses Ed25519 private key from file or gpg:keyid format
 *    - Extracts public key for authentication
 *    - Supports password-protected keys (SSH agent or prompt)
 *    - Optional password for dual authentication (key + password)
 *
 * 2. PASSWORD MODE (--password specified):
 *    - Uses Argon2id key derivation from shared password
 *    - Both client and server derive same key from password
 *    - No identity keys required
 *
 * 3. PASSWORDLESS MODE (no credentials):
 *    - Generates random ephemeral keys
 *    - No long-term identity (no authentication)
 *    - Suitable for trusted networks or testing
 *
 * GLOBAL CRYPTO CONTEXT:
 * =====================
 * The client uses a single global crypto context (g_crypto_ctx):
 * - Shared across all connection attempts (reused on reconnection)
 * - Initialized once per program execution
 * - Cleaned up on program shutdown
 * - Stores server connection info for known_hosts verification
 *
 * SERVER IDENTITY VERIFICATION:
 * =============================
 * Client verifies server identity using known_hosts:
 * - Checks server's identity key against ~/.ascii-chat/known_hosts
 * - First connection: Prompts user to accept server key
 * - Subsequent connections: Verifies key matches stored value
 * - Key mismatch: Warns user about potential MITM attack
 * - Optional --server-key for explicit server key verification
 *
 * CLIENT AUTHENTICATION REQUIREMENTS:
 * ====================================
 * When server requires client authentication (whitelist enabled):
 * - Client must provide identity key with --key option
 * - Client's public key must be in server's --client-keys list
 * - Authentication failure results in connection rejection
 * - Interactive prompt warns user if no identity key provided
 *
 * SESSION REKEYING:
 * =================
 * Long-lived connections support periodic rekeying:
 * - crypto_client_should_rekey(): Checks if rekey is needed
 * - crypto_client_initiate_rekey(): Client-initiated rekeying
 * - crypto_client_process_rekey_request(): Handles server-initiated rekey
 * - Rekeying refreshes encryption keys without reconnecting
 *
 * ENCRYPTION/DECRYPTION OPERATIONS:
 * ==================================
 * After handshake completion:
 * - crypto_client_encrypt_packet(): Encrypts packets before transmission
 * - crypto_client_decrypt_packet(): Decrypts received packets
 * - Both functions use global crypto context
 * - Automatic passthrough when encryption disabled (--no-encrypt)
 *
 * ALGORITHM SUPPORT:
 * ==================
 * The client currently supports:
 * - Key Exchange: X25519 (Elliptic Curve Diffie-Hellman)
 * - Cipher: XSalsa20-Poly1305 (Authenticated Encryption)
 * - Authentication: Ed25519 (when client has identity key)
 * - Key Derivation: Argon2id (for password-based authentication)
 * - HMAC: HMAC-SHA256 (for additional integrity protection)
 *
 * ERROR HANDLING:
 * ==============
 * Handshake errors are handled appropriately:
 * - Server disconnection during handshake: Log and return error
 * - Protocol mismatch: Log detailed error and abort connection
 * - Authentication failure: Log and return CONNECTION_ERROR_AUTH_FAILED
 * - Network errors: Detect and handle gracefully (reconnection)
 * - Invalid packets: Validate size and format before processing
 *
 * THREAD SAFETY:
 * ==============
 * Crypto operations are thread-safe:
 * - Global crypto context protected by initialization flag
 * - Single crypto context per client process (no concurrent handshakes)
 * - Encryption/decryption operations are safe for concurrent use
 * - Rekeying operations coordinate with connection thread
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - main.c: Calls client_crypto_init() during client startup
 * - server.c: Uses the shared network negotiation during connection
 * - protocol.c: Uses encryption functions for secure packet transmission
 * - crypto/handshake.h: Core handshake protocol implementation
 * - crypto/keys/keys.h: Key parsing and management functions
 * - crypto/known_hosts.h: Server identity verification
 *
 * WHY THIS MODULAR DESIGN:
 * =========================
 * Separating cryptographic operations from connection management provides:
 * - Clear cryptographic interface for application code
 * - Easier handshake protocol evolution
 * - Better error isolation and debugging
 * - Improved security auditing capabilities
 * - Independent testing of crypto functionality
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 * @version 2.0
 * @see server.c For connection establishment and crypto handshake timing
 * @see crypto/handshake.h For handshake protocol implementation
 * @see crypto/keys/keys.h For key parsing and management
 * @see crypto/known_hosts.h For server identity verification
 */

#include <ascii-chat/ui/notice.h>

#include "main.h"
#include "crypto.h"
#include "server.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/crypto/discovery_keys.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/mdns/discovery.h> // For pubkey_to_hex
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/endian.h>
#include "capture.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sodium.h>

#include <ascii-chat/platform/question.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/lifecycle.h>

/**
 * @brief Crypto subsystem lifecycle (supports reset/reinit for reconnection)
 *
 * Tracks initialization state of cryptographic components.
 * Supports reset cycle for client reconnection scenarios.
 *
 * @ingroup client_crypto
 */

/**
 * Set crypto mode for handshake (encryption + authentication)
 *
 * @param mode Crypto mode bitmask (ACIP_CRYPTO_*)
 *
 * @ingroup client_crypto
 */
void client_crypto_set_mode(client_connection_t *connection, uint8_t mode) {
  log_debug("CLIENT_CRYPTO: Setting crypto mode to 0x%02x", mode);
  connection->crypto_mode = mode;
}

/**
 * Initialize client crypto handshake
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int client_crypto_init(client_connection_t *connection) {
  log_debug("CLIENT_CRYPTO_INIT: Starting crypto initialization");

  // Reset if already initialized (allows reinit on reconnection)
  if (lifecycle_reset(&connection->crypto_lc)) {
    log_debug("CLIENT_CRYPTO_INIT: Already initialized, resetting for reinit");
    crypto_handshake_destroy(&connection->crypto_ctx);
  }

  log_debug("CLIENT_CRYPTO_INIT: Initializing crypto handshake context");
  log_debug("CLIENT_CRYPTO_INIT: crypto_mode=%d (NONE=0, ENCRYPT=1, AUTH=2, FULL=3)", connection->crypto_mode);

  // Check if we have an SSH key, password, or neither
  int result;
  bool is_ssh_key = false;
  private_key_t private_key;

  // Get string options safely
  const options_t *opts = options_get();
  const char *encrypt_key = opts && opts->encrypt_key[0] != '\0' ? opts->encrypt_key : "";
  const char *password = opts && opts->password[0] != '\0' ? opts->password : "";
  const char *address = opts && opts->address[0] != '\0' ? opts->address : "localhost";
  int port = opts ? opts->port : OPT_PORT_INT_DEFAULT;
  const char *server_key = opts && opts->server_key[0] != '\0' ? opts->server_key : "";

  // Load client private key if provided via --key
  if (strlen(encrypt_key) > 0) {
    // --key supports file-based authentication (SSH keys, GPG keys via gpg:keyid)

    // For SSH key files (not gpg:keyid format), validate the file exists
    if (strncmp(encrypt_key, "gpg:", 4) != 0) {
      if (validate_ssh_key_file(encrypt_key) != 0) {
        return -1;
      }
    }

    // Parse key (handles SSH files and gpg:keyid format)
    log_debug("CLIENT_CRYPTO_INIT: Loading private key for authentication: %s", encrypt_key);
    if (parse_private_key(encrypt_key, &private_key) == ASCIICHAT_OK) {
      log_debug("Successfully parsed SSH private key");
      log_debug("CLIENT_CRYPTO_INIT: Parsed key type=%d, KEY_TYPE_ED25519=%d", private_key.type, KEY_TYPE_ED25519);
      is_ssh_key = true;
    } else {
      NOTICE(DANGER, "IDENTITY KEY COULD NOT BE LOADED", "Failed to parse SSH key file: %s", encrypt_key);
      log_error("This may be due to:");
      log_error("  - Wrong password for encrypted key");
      log_error("  - Unsupported key type (only Ed25519 is currently supported)");
      log_error("  - Corrupted key file");
      log_error("");
      log_error("Note: RSA and ECDSA keys are not yet supported");
      log_error("To generate an Ed25519 key: ssh-keygen -t ed25519");
      return -1;
    }
  }

  if (is_ssh_key) {
    // Use SSH private key for authentication
    log_debug("CLIENT_CRYPTO_INIT: Using SSH key for authentication");

    // Initialize crypto context (generates ephemeral X25519 keys)
    result = crypto_handshake_init("crypto_client_local", &connection->crypto_ctx, false); // false = client
    if (result != ASCIICHAT_OK) {
      FATAL(result, "Failed to initialize crypto handshake");
    }

    // Store the Ed25519 keys for authentication
    memcpy(&connection->crypto_ctx.client_private_key, &private_key, sizeof(private_key_t));

    // Extract Ed25519 public key from private key
    connection->crypto_ctx.client_public_key.type = KEY_TYPE_ED25519;
    memcpy(connection->crypto_ctx.client_public_key.key, private_key.public_key, ED25519_PUBLIC_KEY_SIZE);
    SAFE_STRNCPY(connection->crypto_ctx.client_public_key.comment, private_key.key_comment,
                 sizeof(connection->crypto_ctx.client_public_key.comment) - 1);

    // Extract GPG key ID if this is a GPG key (format: "gpg:KEYID")
    if (strncmp(encrypt_key, "gpg:", 4) == 0) {
      const char *key_id = encrypt_key + 4;
      size_t key_id_len = strlen(key_id);
      // Accept 8, 16, or 40 character GPG key IDs (short/long/full fingerprint)
      if (key_id_len == 8 || key_id_len == 16 || key_id_len == 40) {
        SAFE_STRNCPY(connection->crypto_ctx.client_gpg_key_id, key_id,
                     sizeof(connection->crypto_ctx.client_gpg_key_id));
        log_debug("CLIENT_CRYPTO_INIT: Extracted client GPG key ID (%zu chars): %s", key_id_len,
                  connection->crypto_ctx.client_gpg_key_id);
      } else {
        log_warn("CLIENT_CRYPTO_INIT: Invalid GPG key ID length: %zu (expected 8, 16, or 40)", key_id_len);
        connection->crypto_ctx.client_gpg_key_id[0] = '\0';
      }
    } else {
      // Not a GPG key, clear the field
      connection->crypto_ctx.client_gpg_key_id[0] = '\0';
    }

    // SSH key is already configured in the handshake context above
    // No additional setup needed - SSH keys are used only for authentication

    // Clear the temporary private_key variable (we've already copied it to g_crypto_ctx)
    sodium_memzero(&private_key, sizeof(private_key));

    // If password is also provided, derive password key for dual authentication
    if (strlen(password) > 0) {
      log_debug("CLIENT_CRYPTO_INIT: Password also provided, deriving password key");
      crypto_result_t crypto_result = crypto_derive_password_key(&connection->crypto_ctx.crypto_ctx, password);
      if (crypto_result != CRYPTO_OK) {
        log_error("Failed to derive password key: %s", crypto_result_to_string(crypto_result));
        return -1;
      }
      connection->crypto_ctx.crypto_ctx.has_password = true;
      log_debug("Password authentication enabled alongside SSH key");
    }

  } else if (strlen(GET_OPTION(password)) > 0) {
    // Password provided - use password-based initialization
    log_debug("CLIENT_CRYPTO_INIT: Using password authentication");
    result = crypto_handshake_init_with_password("crypto_client_local", &connection->crypto_ctx, false,
                                                 GET_OPTION(password)); // false = client
    if (result != ASCIICHAT_OK) {
      FATAL(result, "Failed to initialize crypto handshake with password");
    }
  } else {
    // No password or SSH key - use standard initialization with random keys
    log_debug("CLIENT_CRYPTO_INIT: Using standard initialization");
    result = crypto_handshake_init("crypto_client_local", &connection->crypto_ctx, false); // false = client
    if (result != ASCIICHAT_OK) {
      FATAL(result, "Failed to initialize crypto handshake");
    }
  }

  if (client_connection_checkpoint("crypto") != ASCIICHAT_OK)
    return -1;
  log_debug("CLIENT_CRYPTO_INIT: crypto_handshake_init succeeded");

  // Set up server connection info for known_hosts
  SAFE_STRNCPY(connection->crypto_ctx.server_hostname, address, sizeof(connection->crypto_ctx.server_hostname) - 1);
  const char *server_ip = server_connection_get_ip(connection);
  log_debug("CLIENT_CRYPTO_INIT: server_connection_get_ip() returned: '%s'", server_ip ? server_ip : "NULL");
  SAFE_STRNCPY(connection->crypto_ctx.server_ip, server_ip ? server_ip : "",
               sizeof(connection->crypto_ctx.server_ip) - 1);

  // Use port from server_connection_set_port() if available (e.g., from WebSocket URL),
  // otherwise use the port from options (TCP/default path)
  int connection_port = server_connection_get_port(connection);
  connection->crypto_ctx.server_port = (uint16_t)(connection_port > 0 ? connection_port : port);
  log_debug("CLIENT_CRYPTO_INIT: Set server_ip='%s', server_port=%u", connection->crypto_ctx.server_ip,
            connection->crypto_ctx.server_port);

  // Configure server key verification if specified
  if (strlen(server_key) > 0) {
    connection->crypto_ctx.verify_server_key = true;
    SAFE_STRNCPY(connection->crypto_ctx.expected_server_key, server_key,
                 sizeof(connection->crypto_ctx.expected_server_key) - 1);
    log_debug("Server key verification enabled: %s", server_key);
  }

  // If --require-client-verify is set, perform ACDS session lookup for server identity
  if (GET_OPTION(require_client_verify) && strlen(GET_OPTION(session_string)) > 0) {
    log_debug("--require-client-verify enabled: performing ACDS session lookup for '%s'", GET_OPTION(session_string));

    // Connect to ACDS server (configurable via --acds-server and --acds-port options)
    acds_client_config_t acds_config;
    acds_client_config_init_defaults(&acds_config);
    SAFE_STRNCPY(acds_config.server_address, GET_OPTION(discovery_server), sizeof(acds_config.server_address));
    acds_config.server_port = GET_OPTION(discovery_port);
    acds_config.timeout_ms = 5 * MS_PER_SEC_INT;
    acds_config.identity_key = GET_OPTION(encrypt_key);

    // ACDS key verification (optional in debug builds, only if --discovery-service-key is provided)
    if (strlen(GET_OPTION(discovery_service_key)) > 0) {
      log_debug("Verifying ACDS server key for %s...", acds_config.server_address);
      uint8_t acds_pubkey[32];
      asciichat_error_t verify_result =
          discovery_keys_verify(acds_config.server_address, GET_OPTION(discovery_service_key), acds_pubkey);
      if (verify_result != ASCIICHAT_OK) {
        NOTICE(DANGER, "ACDS VERIFICATION FAILED", "ACDS key verification failed for %s", acds_config.server_address);
        return -1;
      }
      log_debug("ACDS server key verified successfully");
    }
#ifndef NDEBUG
    else {
      log_debug("Skipping ACDS key verification (debug build, no --discovery-service-key provided)");
    }
#endif

    acds_client_t acds_client;
    asciichat_error_t acds_result = acds_client_connect(&acds_client, &acds_config);
    if (acds_result != ASCIICHAT_OK) {
      log_error("Failed to connect to ACDS server at %s:%d", acds_config.server_address, acds_config.server_port);
      return -1;
    }

    // Perform SESSION_LOOKUP to get server's identity
    acds_session_lookup_result_t lookup_result;
    acds_result = acds_session_lookup(&acds_client, GET_OPTION(session_string), &lookup_result);
    acds_client_disconnect(&acds_client);

    if (acds_result != ASCIICHAT_OK || !lookup_result.found) {
      log_error("ACDS session lookup failed for '%s': %s", GET_OPTION(session_string),
                lookup_result.found ? "session not found" : "lookup error");
      return -1;
    }

    // Convert server's Ed25519 public key (32 bytes) to hex string
    char server_key_hex[65]; // 32 bytes * 2 + null terminator
    for (size_t i = 0; i < 32; i++) {
      SAFE_SNPRINTF(&server_key_hex[i * 2], 3, "%02x", lookup_result.host_pubkey[i]);
    }
    server_key_hex[64] = '\0';

    // Set expected server key for verification during handshake
    connection->crypto_ctx.verify_server_key = true;
    SAFE_STRNCPY(connection->crypto_ctx.expected_server_key, server_key_hex,
                 sizeof(connection->crypto_ctx.expected_server_key) - 1);
    log_debug("ACDS session lookup succeeded - server identity will be verified");
    log_debug("Expected server key (from ACDS): %s", server_key_hex);
  }

  // Mark initialization as complete
  lifecycle_init(&connection->crypto_lc, "client_crypto");

  log_debug("Client crypto handshake initialized");
  log_debug("CLIENT_CRYPTO_INIT: Initialization complete");
  return 0;
}

bool crypto_client_is_ready(client_connection_t *connection) {
  if (!lifecycle_is_initialized(&connection->crypto_lc) || connection->crypto_mode == ACIP_CRYPTO_NONE) {
    return false;
  }

  return crypto_handshake_is_ready(&connection->crypto_ctx);
}

/**
 * Get crypto context for encryption/decryption
 *
 * @return crypto context or NULL if not ready
 *
 * @ingroup client_crypto
 */
const crypto_context_t *crypto_client_get_context(client_connection_t *connection) {
  if (!crypto_client_is_ready(connection)) {
    return NULL;
  }

  return crypto_handshake_get_context(&connection->crypto_ctx);
}

/**
 * Encrypt a packet for transmission
 *
 * @param plaintext Plaintext data to encrypt
 * @param plaintext_len Length of plaintext data
 * @param ciphertext Output buffer for encrypted data
 * @param ciphertext_size Size of output buffer
 * @param ciphertext_len Output length of encrypted data
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_encrypt_packet(client_connection_t *connection, const uint8_t *plaintext, size_t plaintext_len,
                                 uint8_t *ciphertext, size_t ciphertext_size, size_t *ciphertext_len) {
  return crypto_encrypt_packet_or_passthrough(&connection->crypto_ctx, crypto_client_is_ready(connection), plaintext,
                                              plaintext_len, ciphertext, ciphertext_size, ciphertext_len);
}

/**
 * Decrypt a received packet
 *
 * @param ciphertext Encrypted data to decrypt
 * @param ciphertext_len Length of encrypted data
 * @param plaintext Output buffer for decrypted data
 * @param plaintext_size Size of output buffer
 * @param plaintext_len Output length of decrypted data
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_decrypt_packet(client_connection_t *connection, const uint8_t *ciphertext, size_t ciphertext_len,
                                 uint8_t *plaintext, size_t plaintext_size, size_t *plaintext_len) {
  return crypto_decrypt_packet_or_passthrough(&connection->crypto_ctx, crypto_client_is_ready(connection), ciphertext,
                                              ciphertext_len, plaintext, plaintext_size, plaintext_len);
}

/**
 * Cleanup crypto client resources
 *
 * @ingroup client_crypto
 */
void crypto_client_cleanup(client_connection_t *connection) {
  // Initialization may fail after creating keys but before committing lifecycle.
  lifecycle_shutdown(&connection->crypto_lc);
  crypto_handshake_destroy(&connection->crypto_ctx);
}

// =============================================================================
// Session Rekeying Functions
// =============================================================================

/**
 * Check if session rekeying should be triggered
 *
 * @return true if rekey should be initiated, false otherwise
 *
 * @ingroup client_crypto
 */
bool crypto_client_should_rekey(client_connection_t *connection) {
  if (!lifecycle_is_initialized(&connection->crypto_lc) || !crypto_client_is_ready(connection) ||
      !connection->crypto_ctx.crypto_ctx.encrypt_data) {
    return false;
  }
  return crypto_handshake_should_rekey(&connection->crypto_ctx);
}

/**
 * Initiate session rekeying (client-initiated)
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_initiate_rekey(client_connection_t *connection) {
  if (!lifecycle_is_initialized(&connection->crypto_lc) || !crypto_client_is_ready(connection)) {
    log_error("Cannot initiate rekey: crypto not initialized or not ready");
    return -1;
  }

  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport) {
    log_error("Cannot initiate rekey: invalid transport");
    return -1;
  }

  asciichat_error_t result = crypto_handshake_rekey_request(&connection->crypto_ctx, transport);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_REQUEST: %d", result);
    return -1;
  }

  return 0;
}

/**
 * Process received REKEY_REQUEST packet from server
 *
 * @param packet Packet data
 * @param packet_len Packet length
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_process_rekey_request(client_connection_t *connection, const uint8_t *packet, size_t packet_len) {
  if (!lifecycle_is_initialized(&connection->crypto_lc) || !crypto_client_is_ready(connection)) {
    log_error("Cannot process rekey request: crypto not initialized or not ready");
    return -1;
  }

  asciichat_error_t result = crypto_handshake_process_rekey_request(&connection->crypto_ctx, packet, packet_len);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_REQUEST: %d", result);
    return -1;
  }

  return 0;
}

/**
 * Send REKEY_RESPONSE packet to server
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_send_rekey_response(client_connection_t *connection) {
  if (!lifecycle_is_initialized(&connection->crypto_lc) || !crypto_client_is_ready(connection)) {
    log_error("Cannot send rekey response: crypto not initialized or not ready");
    return -1;
  }

  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport) {
    log_error("Cannot send rekey response: invalid transport");
    return -1;
  }

  asciichat_error_t result = crypto_handshake_rekey_response(&connection->crypto_ctx, transport);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_RESPONSE: %d", result);
    return -1;
  }

  return 0;
}

/**
 * Process received REKEY_RESPONSE packet from server
 *
 * @param packet Packet data
 * @param packet_len Packet length
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_process_rekey_response(client_connection_t *connection, const uint8_t *packet, size_t packet_len) {
  if (!lifecycle_is_initialized(&connection->crypto_lc) || !crypto_client_is_ready(connection)) {
    log_error("Cannot process rekey response: crypto not initialized or not ready");
    return -1;
  }

  asciichat_error_t result = crypto_handshake_process_rekey_response(&connection->crypto_ctx, packet, packet_len);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_RESPONSE: %d", result);
    return -1;
  }

  return 0;
}

/**
 * Send REKEY_COMPLETE packet to server and commit to new key
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_send_rekey_complete(client_connection_t *connection) {
  if (!lifecycle_is_initialized(&connection->crypto_lc) || !crypto_client_is_ready(connection)) {
    log_error("Cannot send rekey complete: crypto not initialized or not ready");
    return -1;
  }

  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport) {
    log_error("Cannot send rekey complete: invalid transport");
    return -1;
  }

  asciichat_error_t result = crypto_handshake_rekey_complete(&connection->crypto_ctx, transport);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_COMPLETE: %d", result);
    return -1;
  }

  return 0;
}
