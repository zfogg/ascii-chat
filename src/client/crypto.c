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
 * - server.c: Calls client_crypto_handshake() during connection
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

#include "crypto.h"
#include "server.h"
#include "options/options.h"
#include "options/rcu.h" // For RCU-based options access
#include "common.h"
#include "util/endian.h"
#include "crypto/handshake/common.h"
#include "crypto/handshake/client.h"
#include "crypto/crypto.h"
#include "crypto/keys.h"
#include "buffer_pool.h"
#include "network/packet.h"
#include "network/acds_client.h"
#include "util/time.h"
#include "util/endian.h"
#include "capture.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sodium.h>

#include "platform/question.h"

// Global crypto handshake context for this client connection
// NOTE: We use the crypto context from server.c to match the handshake
extern crypto_handshake_context_t g_crypto_ctx;

/**
 * @brief Flag indicating if crypto subsystem has been initialized
 *
 * Set to true after successful initialization of cryptographic components.
 * Used to prevent multiple initialization attempts and ensure proper cleanup.
 *
 * @ingroup client_crypto
 */
static bool g_crypto_initialized = false;

/**
 * Initialize client crypto handshake
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int client_crypto_init(void) {
  // Get options from RCU state
  const options_t *opts = options_get();
  if (!opts) {
    log_error("Options not initialized");
    return -1;
  }

  log_debug("CLIENT_CRYPTO_INIT: Starting crypto initialization");
  if (g_crypto_initialized) {
    log_debug("CLIENT_CRYPTO_INIT: Already initialized, cleaning up and reinitializing");
    crypto_handshake_cleanup(&g_crypto_ctx);
    g_crypto_initialized = false;
  }

  // Check if encryption is disabled
  if (opts && opts->no_encrypt) {
    log_info("Encryption disabled via --no-encrypt");
    log_debug("CLIENT_CRYPTO_INIT: Encryption disabled, returning 0");
    return 0;
  }

  log_debug("CLIENT_CRYPTO_INIT: Initializing crypto handshake context");

  // Check if we have an SSH key, password, or neither
  int result;
  bool is_ssh_key = false;
  private_key_t private_key;

  // Load client private key if provided via --key
  const char *encrypt_key = opts ? opts->encrypt_key : "";
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
      log_info("Successfully parsed SSH private key");
      log_debug("CLIENT_CRYPTO_INIT: Parsed key type=%d, KEY_TYPE_ED25519=%d", private_key.type, KEY_TYPE_ED25519);
      is_ssh_key = true;
    } else {
      log_error("Failed to parse SSH key file: %s", encrypt_key);
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
    result = crypto_handshake_init(&g_crypto_ctx, false); // false = client
    if (result != ASCIICHAT_OK) {
      FATAL(result, "Failed to initialize crypto handshake");
    }

    // Store the Ed25519 keys for authentication
    memcpy(&g_crypto_ctx.client_private_key, &private_key, sizeof(private_key_t));

    // Extract Ed25519 public key from private key
    g_crypto_ctx.client_public_key.type = KEY_TYPE_ED25519;
    memcpy(g_crypto_ctx.client_public_key.key, private_key.public_key, ED25519_PUBLIC_KEY_SIZE);
    SAFE_STRNCPY(g_crypto_ctx.client_public_key.comment, private_key.key_comment,
                 sizeof(g_crypto_ctx.client_public_key.comment) - 1);

    // Extract GPG key ID if this is a GPG key (format: "gpg:KEYID")
    if (strncmp(encrypt_key, "gpg:", 4) == 0) {
      const char *key_id = encrypt_key + 4;
      size_t key_id_len = strlen(key_id);
      // Accept 8, 16, or 40 character GPG key IDs (short/long/full fingerprint)
      if (key_id_len == 8 || key_id_len == 16 || key_id_len == 40) {
        SAFE_STRNCPY(g_crypto_ctx.client_gpg_key_id, key_id, sizeof(g_crypto_ctx.client_gpg_key_id));
        log_debug("CLIENT_CRYPTO_INIT: Extracted client GPG key ID (%zu chars): %s", key_id_len,
                  g_crypto_ctx.client_gpg_key_id);
      } else {
        log_warn("CLIENT_CRYPTO_INIT: Invalid GPG key ID length: %zu (expected 8, 16, or 40)", key_id_len);
        g_crypto_ctx.client_gpg_key_id[0] = '\0';
      }
    } else {
      // Not a GPG key, clear the field
      g_crypto_ctx.client_gpg_key_id[0] = '\0';
    }

    // SSH key is already configured in the handshake context above
    // No additional setup needed - SSH keys are used only for authentication

    // Clear the temporary private_key variable (we've already copied it to g_crypto_ctx)
    sodium_memzero(&private_key, sizeof(private_key));

    // If password is also provided, derive password key for dual authentication
    const char *password = opts ? opts->password : "";
    if (strlen(password) > 0) {
      log_debug("CLIENT_CRYPTO_INIT: Password also provided, deriving password key");
      crypto_result_t crypto_result = crypto_derive_password_key(&g_crypto_ctx.crypto_ctx, password);
      if (crypto_result != CRYPTO_OK) {
        log_error("Failed to derive password key: %s", crypto_result_to_string(crypto_result));
        return -1;
      }
      g_crypto_ctx.crypto_ctx.has_password = true;
      log_info("Password authentication enabled alongside SSH key");
    }

  } else if (opts && strlen(opts->password) > 0) {
    // Password provided - use password-based initialization
    log_debug("CLIENT_CRYPTO_INIT: Using password authentication");
    result = crypto_handshake_init_with_password(&g_crypto_ctx, false, opts->password); // false = client
    if (result != ASCIICHAT_OK) {
      FATAL(result, "Failed to initialize crypto handshake with password");
    }
  } else {
    // No password or SSH key - use standard initialization with random keys
    log_debug("CLIENT_CRYPTO_INIT: Using standard initialization");
    result = crypto_handshake_init(&g_crypto_ctx, false); // false = client
    if (result != ASCIICHAT_OK) {
      FATAL(result, "Failed to initialize crypto handshake");
    }
  }

  log_debug("CLIENT_CRYPTO_INIT: crypto_handshake_init succeeded");

  // Set up server connection info for known_hosts
  const char *address = opts ? opts->address : "localhost";
  SAFE_STRNCPY(g_crypto_ctx.server_hostname, address, sizeof(g_crypto_ctx.server_hostname) - 1);
  const char *server_ip = server_connection_get_ip();
  log_debug("CLIENT_CRYPTO_INIT: server_connection_get_ip() returned: '%s'", server_ip ? server_ip : "NULL");
  SAFE_STRNCPY(g_crypto_ctx.server_ip, server_ip ? server_ip : "", sizeof(g_crypto_ctx.server_ip) - 1);
  const char *port = opts ? opts->port : "27224";
  g_crypto_ctx.server_port = (uint16_t)strtoint_safe(port);
  log_debug("CLIENT_CRYPTO_INIT: Set server_ip='%s', server_port=%u", g_crypto_ctx.server_ip, g_crypto_ctx.server_port);

  // Configure server key verification if specified
  const char *server_key = opts ? opts->server_key : "";
  if (strlen(server_key) > 0) {
    g_crypto_ctx.verify_server_key = true;
    SAFE_STRNCPY(g_crypto_ctx.expected_server_key, server_key, sizeof(g_crypto_ctx.expected_server_key) - 1);
    log_info("Server key verification enabled: %s", server_key);
  }

  // If --require-client-verify is set, perform ACDS session lookup for server identity
  if (opts && opts->require_client_verify && strlen(opts->session_string) > 0) {
    log_info("--require-client-verify enabled: performing ACDS session lookup for '%s'", opts->session_string);

    // Connect to ACDS server (default: localhost:27225)
    // TODO: Make ACDS server address configurable via --acds-server option
    acds_client_config_t acds_config;
    acds_client_config_init_defaults(&acds_config);
    SAFE_STRNCPY(acds_config.server_address, "127.0.0.1", sizeof(acds_config.server_address));
    acds_config.server_port = 27225;
    acds_config.timeout_ms = 5000;

    acds_client_t acds_client;
    asciichat_error_t acds_result = acds_client_connect(&acds_client, &acds_config);
    if (acds_result != ASCIICHAT_OK) {
      log_error("Failed to connect to ACDS server at %s:%d", acds_config.server_address, acds_config.server_port);
      return -1;
    }

    // Perform SESSION_LOOKUP to get server's identity
    acds_session_lookup_result_t lookup_result;
    acds_result = acds_session_lookup(&acds_client, opts->session_string, &lookup_result);
    acds_client_disconnect(&acds_client);

    if (acds_result != ASCIICHAT_OK || !lookup_result.found) {
      log_error("ACDS session lookup failed for '%s': %s", opts->session_string,
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
    g_crypto_ctx.verify_server_key = true;
    SAFE_STRNCPY(g_crypto_ctx.expected_server_key, server_key_hex, sizeof(g_crypto_ctx.expected_server_key) - 1);
    log_info("ACDS session lookup succeeded - server identity will be verified");
    log_debug("Expected server key (from ACDS): %s", server_key_hex);
  }

  g_crypto_initialized = true;
  log_info("Client crypto handshake initialized");
  log_debug("CLIENT_CRYPTO_INIT: Initialization complete, g_crypto_initialized=true");
  return 0;
}

/**
 * Perform crypto handshake with server
 *
 * @param socket Connected socket to server
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int client_crypto_handshake(socket_t socket) {
  // Get options from RCU state
  const options_t *opts = options_get();
  if (!opts) {
    log_error("Options not initialized");
    return -1;
  }

  // If client has --no-encrypt, skip handshake entirely
  if (opts && opts->no_encrypt) {
    log_debug("Client has --no-encrypt, skipping crypto handshake");
    return 0;
  }

  // If we reach here, crypto must be initialized for encryption
  if (!g_crypto_initialized) {
    log_error("Crypto not initialized but server requires encryption");
    log_error("Server requires encrypted connection but client has no encryption configured");
    log_error("Use --key to specify a client key or --password for password authentication");
    return CONNECTION_ERROR_AUTH_FAILED; // No retry - configuration error
  }

  log_info("Starting crypto handshake with server...");

  START_TIMER("client_crypto_handshake");

  // Step 0a: Send protocol version to server
  protocol_version_packet_t client_version = {0};
  client_version.protocol_version = HOST_TO_NET_U16(1);  // Protocol version 1
  client_version.protocol_revision = HOST_TO_NET_U16(0); // Revision 0
  client_version.supports_encryption = 1;                // We support encryption
  client_version.compression_algorithms = 0;             // No compression for now
  client_version.compression_threshold = 0;
  client_version.feature_flags = 0;

  int result = send_protocol_version_packet(socket, &client_version);
  if (result != 0) {
    log_error("Failed to send protocol version to server");
    STOP_TIMER("client_crypto_handshake");
    return -1;
  }
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Protocol version sent successfully");

  // Step 0b: Receive server's protocol version
  packet_type_t packet_type;
  void *payload = NULL;
  size_t payload_len = 0;

  result = receive_packet(socket, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_PROTOCOL_VERSION) {
    log_error("Failed to receive server protocol version (got type %u)", packet_type);
    log_error("Packet type 0x%x (decimal %u) - Expected 0x%x (decimal %d)", packet_type, packet_type,
              PACKET_TYPE_PROTOCOL_VERSION, PACKET_TYPE_PROTOCOL_VERSION);
    log_error("This suggests a protocol mismatch or packet corruption");
    log_error("Raw packet type bytes: %02x %02x %02x %02x", (packet_type >> 0) & 0xFF, (packet_type >> 8) & 0xFF,
              (packet_type >> 16) & 0xFF, (packet_type >> 24) & 0xFF);
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    STOP_TIMER("client_crypto_handshake");
    return -1;
  }

  if (payload_len != sizeof(protocol_version_packet_t)) {
    log_error("Invalid protocol version packet size: %zu, expected %zu", payload_len,
              sizeof(protocol_version_packet_t));
    buffer_pool_free(NULL, payload, payload_len);
    STOP_TIMER("client_crypto_handshake");
    return -1;
  }

  protocol_version_packet_t server_version;
  memcpy(&server_version, payload, sizeof(protocol_version_packet_t));
  buffer_pool_free(NULL, payload, payload_len);

  // Convert from network byte order
  uint16_t server_proto_version = NET_TO_HOST_U16(server_version.protocol_version);
  uint16_t server_proto_revision = NET_TO_HOST_U16(server_version.protocol_revision);

  log_info("Server protocol version: %u.%u (encryption: %s)", server_proto_version, server_proto_revision,
           server_version.supports_encryption ? "yes" : "no");

  if (!server_version.supports_encryption) {
    log_error("Server does not support encryption");
    STOP_TIMER("client_crypto_handshake");
    return CONNECTION_ERROR_AUTH_FAILED;
  }

  // Step 0c: Send crypto capabilities to server
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Sending crypto capabilities");
  crypto_capabilities_packet_t client_caps = {0};
  client_caps.supported_kex_algorithms = HOST_TO_NET_U16(KEX_ALGO_X25519);
  client_caps.supported_auth_algorithms = HOST_TO_NET_U16(AUTH_ALGO_ED25519 | AUTH_ALGO_NONE);
  client_caps.supported_cipher_algorithms = HOST_TO_NET_U16(CIPHER_ALGO_XSALSA20_POLY1305);
  client_caps.requires_verification = 0; // Client doesn't require server verification (uses known_hosts)
  client_caps.preferred_kex = KEX_ALGO_X25519;
  client_caps.preferred_auth = AUTH_ALGO_ED25519;
  client_caps.preferred_cipher = CIPHER_ALGO_XSALSA20_POLY1305;

  result = send_crypto_capabilities_packet(socket, &client_caps);
  if (result != 0) {
    log_error("Failed to send crypto capabilities to server");
    STOP_TIMER("client_crypto_handshake");
    return -1;
  }
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Crypto capabilities sent successfully");

  // Step 0d: Receive server's crypto parameters
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Receiving server crypto parameters");
  payload = NULL;
  payload_len = 0;

  result = receive_packet(socket, &packet_type, &payload, &payload_len);
  if (result != ASCIICHAT_OK || packet_type != PACKET_TYPE_CRYPTO_PARAMETERS) {
    log_error("Failed to receive server crypto parameters (got type %u)", packet_type);
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    STOP_TIMER("client_crypto_handshake");
    return -1;
  }

  if (payload_len != sizeof(crypto_parameters_packet_t)) {
    log_error("Invalid crypto parameters packet size: %zu, expected %zu", payload_len,
              sizeof(crypto_parameters_packet_t));
    buffer_pool_free(NULL, payload, payload_len);
    STOP_TIMER("client_crypto_handshake");
    return -1;
  }

  crypto_parameters_packet_t server_params;
  memcpy(&server_params, payload, sizeof(crypto_parameters_packet_t));
  buffer_pool_free(NULL, payload, payload_len);

  // Convert from network byte order
  uint16_t kex_pubkey_size = NET_TO_HOST_U16(server_params.kex_public_key_size);
  uint16_t auth_pubkey_size = NET_TO_HOST_U16(server_params.auth_public_key_size);
  uint16_t signature_size = NET_TO_HOST_U16(server_params.signature_size);
  uint16_t shared_secret_size = NET_TO_HOST_U16(server_params.shared_secret_size);

  log_info("Server crypto parameters: KEX=%u, Auth=%u, Cipher=%u (key_size=%u, auth_size=%u, sig_size=%u, "
           "secret_size=%u, verification=%u)",
           server_params.selected_kex, server_params.selected_auth, server_params.selected_cipher, kex_pubkey_size,
           auth_pubkey_size, signature_size, shared_secret_size, server_params.verification_enabled);
  log_debug("Raw server_params.kex_public_key_size = %u (network byte order)", server_params.kex_public_key_size);

  // Set the crypto parameters in the handshake context
  result = crypto_handshake_set_parameters(&g_crypto_ctx, &server_params);
  if (result != ASCIICHAT_OK) {
    FATAL(result, "Failed to set crypto parameters");
  }

  // Store verification flag - server will verify client identity (whitelist check)
  // This is independent of whether server provides its own identity
  if (server_params.verification_enabled) {
    g_crypto_ctx.server_uses_client_auth = true;
    g_crypto_ctx.require_client_auth = true;
    log_info("Server will verify client identity (whitelist enabled)");
  }

  // Validate that server chose algorithms we support
  if (server_params.selected_kex != KEX_ALGO_X25519) {
    log_error("Server selected unsupported KEX algorithm: %u", server_params.selected_kex);
    STOP_TIMER("client_crypto_handshake");
    return CONNECTION_ERROR_AUTH_FAILED;
  }

  if (server_params.selected_cipher != CIPHER_ALGO_XSALSA20_POLY1305) {
    log_error("Server selected unsupported cipher algorithm: %u", server_params.selected_cipher);
    STOP_TIMER("client_crypto_handshake");
    return CONNECTION_ERROR_AUTH_FAILED;
  }

  log_debug("CLIENT_CRYPTO_HANDSHAKE: Protocol negotiation completed successfully");

  // Step 1: Receive server's public key and send our public key
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Starting key exchange");
  result = crypto_handshake_client_key_exchange(&g_crypto_ctx, socket);
  if (result != ASCIICHAT_OK) {
#ifdef _WIN32
    // On Windows: Cleanup capture resources before exiting to prevent Media Foundation threads from hanging exit()
    // Media Foundation creates background COM threads that can block exit() if not properly shut down
    capture_cleanup();
#endif
    FATAL(result, "Crypto key exchange failed");
  }
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Key exchange completed successfully");

  // SECURITY: Warn when server requires client verification but client has no identity key
  bool client_has_identity = (g_crypto_ctx.client_public_key.type == KEY_TYPE_ED25519);

  if (g_crypto_ctx.require_client_auth && !client_has_identity) {
    // Server requires client verification but client has no identity key
    log_warn("Server requires client verification but client has no identity key");

    // Check if we're running interactively (stdin is a terminal)
    // In debug builds with CLAUDECODE, skip interactive prompts (LLM can't type)
#ifndef NDEBUG
    bool skip_interactive = platform_getenv("CLAUDECODE") != NULL;
#else
    bool skip_interactive = false;
#endif
    if (!skip_interactive && platform_is_interactive()) {
      // Interactive mode - prompt user for confirmation
      // Lock terminal for the warning message
      bool previous_terminal_state = log_lock_terminal();

      log_plain("\n"
                "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
                "@  WARNING: CLIENT AUTHENTICATION REQUIRED                                    @\n"
                "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
                "\n"
                "The server requires client authentication (--client-keys enabled),\n"
                "but you have not provided a client identity key with --key.\n"
                "\n"
                "To connect to this server, you need to:\n"
                "  1. Generate an Ed25519 key: ssh-keygen -t ed25519\n"
                "  2. Add the public key to the server's --client-keys list\n"
                "  3. Connect with: ascii-chat client --key /path/to/private/key\n");

      // Unlock before prompt (prompt_yes_no handles its own terminal locking)
      log_unlock_terminal(previous_terminal_state);

      // Prompt user - default is No since this will likely fail
      if (!platform_prompt_yes_no("Do you want to continue anyway (this will likely fail)", false)) {
        log_plain("Connection aborted by user.");
        exit(0); // User declined - exit cleanly
      }

      log_plain("Warning: Continuing without client identity key (connection may fail).\n");
    } else {
      // Non-interactive mode (background/script) - just log warning and continue
      log_warn("Non-interactive mode: Continuing without client identity key (connection may fail)");
    }
  }

  // Step 2: Receive auth challenge and send response
  log_debug("CLIENT_CRYPTO: Sending auth response to server...");
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Starting auth response");
  result = crypto_handshake_client_auth_response(&g_crypto_ctx, socket);
  if (result != ASCIICHAT_OK) {
    FATAL(result, "Crypto authentication failed");
  }
  log_debug("CLIENT_CRYPTO: Auth response sent successfully");
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Auth response completed successfully");

  // Check if handshake completed during auth response (no authentication needed)
  if (g_crypto_ctx.state == CRYPTO_HANDSHAKE_READY) {
    STOP_TIMER_AND_LOG("client_crypto_handshake", log_info,
                       "Crypto handshake completed successfully (no authentication)");
    return 0;
  }

  // Step 3: Receive handshake complete message
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Waiting for handshake complete message");
  result = crypto_handshake_client_complete(&g_crypto_ctx, socket);
  if (result != ASCIICHAT_OK) {
    FATAL(result, "Crypto handshake completion failed");
  }

  STOP_TIMER_AND_LOG("client_crypto_handshake", log_info, "Crypto handshake completed successfully");
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Handshake completed successfully, state=%d", g_crypto_ctx.state);
  return 0;
}

/**
 * Check if crypto handshake is ready
 *
 * @return true if encryption is ready, false otherwise
 *
 * @ingroup client_crypto
 */
bool crypto_client_is_ready(void) {
  // Get options from RCU state
  const options_t *opts = options_get();
  if (!opts) {
    log_error("Options not initialized");
    return false;
  }

  if (!g_crypto_initialized || (opts && opts->no_encrypt)) {
    return false;
  }

  return crypto_handshake_is_ready(&g_crypto_ctx);
}

/**
 * Get crypto context for encryption/decryption
 *
 * @return crypto context or NULL if not ready
 *
 * @ingroup client_crypto
 */
const crypto_context_t *crypto_client_get_context(void) {
  if (!crypto_client_is_ready()) {
    return NULL;
  }

  return crypto_handshake_get_context(&g_crypto_ctx);
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
int crypto_client_encrypt_packet(const uint8_t *plaintext, size_t plaintext_len, uint8_t *ciphertext,
                                 size_t ciphertext_size, size_t *ciphertext_len) {
  return crypto_encrypt_packet_or_passthrough(&g_crypto_ctx, crypto_client_is_ready(), plaintext, plaintext_len,
                                              ciphertext, ciphertext_size, ciphertext_len);
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
int crypto_client_decrypt_packet(const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *plaintext,
                                 size_t plaintext_size, size_t *plaintext_len) {
  return crypto_decrypt_packet_or_passthrough(&g_crypto_ctx, crypto_client_is_ready(), ciphertext, ciphertext_len,
                                              plaintext, plaintext_size, plaintext_len);
}

/**
 * Cleanup crypto client resources
 *
 * @ingroup client_crypto
 */
void crypto_client_cleanup(void) {
  if (g_crypto_initialized) {
    crypto_handshake_cleanup(&g_crypto_ctx);
    g_crypto_initialized = false;
    log_debug("Client crypto handshake cleaned up");
  }
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
bool crypto_client_should_rekey(void) {
  if (!g_crypto_initialized || !crypto_client_is_ready()) {
    return false;
  }
  return crypto_handshake_should_rekey(&g_crypto_ctx);
}

/**
 * Initiate session rekeying (client-initiated)
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_initiate_rekey(void) {
  if (!g_crypto_initialized || !crypto_client_is_ready()) {
    log_error("Cannot initiate rekey: crypto not initialized or not ready");
    return -1;
  }

  socket_t socket = server_connection_get_socket();
  if (socket == INVALID_SOCKET_VALUE) {
    log_error("Cannot initiate rekey: invalid socket");
    return -1;
  }

  asciichat_error_t result = crypto_handshake_rekey_request(&g_crypto_ctx, socket);
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
int crypto_client_process_rekey_request(const uint8_t *packet, size_t packet_len) {
  if (!g_crypto_initialized || !crypto_client_is_ready()) {
    log_error("Cannot process rekey request: crypto not initialized or not ready");
    return -1;
  }

  asciichat_error_t result = crypto_handshake_process_rekey_request(&g_crypto_ctx, packet, packet_len);
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
int crypto_client_send_rekey_response(void) {
  if (!g_crypto_initialized || !crypto_client_is_ready()) {
    log_error("Cannot send rekey response: crypto not initialized or not ready");
    return -1;
  }

  socket_t socket = server_connection_get_socket();
  if (socket == INVALID_SOCKET_VALUE) {
    log_error("Cannot send rekey response: invalid socket");
    return -1;
  }

  asciichat_error_t result = crypto_handshake_rekey_response(&g_crypto_ctx, socket);
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
int crypto_client_process_rekey_response(const uint8_t *packet, size_t packet_len) {
  if (!g_crypto_initialized || !crypto_client_is_ready()) {
    log_error("Cannot process rekey response: crypto not initialized or not ready");
    return -1;
  }

  asciichat_error_t result = crypto_handshake_process_rekey_response(&g_crypto_ctx, packet, packet_len);
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
int crypto_client_send_rekey_complete(void) {
  if (!g_crypto_initialized || !crypto_client_is_ready()) {
    log_error("Cannot send rekey complete: crypto not initialized or not ready");
    return -1;
  }

  socket_t socket = server_connection_get_socket();
  if (socket == INVALID_SOCKET_VALUE) {
    log_error("Cannot send rekey complete: invalid socket");
    return -1;
  }

  asciichat_error_t result = crypto_handshake_rekey_complete(&g_crypto_ctx, socket);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_COMPLETE: %d", result);
    return -1;
  }

  return 0;
}
