#include <ascii-chat/network/compression.h>
#include "session/frame_validation.h"
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/crypto/policy.h>
#include <emscripten.h>
#include <ascii-chat/common.h>
#include <string.h>
#include <ascii-chat/network/acip/settings.h>

static acip_render_settings_t remote_render_settings;

/** Effective remote state is separate from this participant's saved preferences. */
EMSCRIPTEN_KEEPALIVE asciichat_error_t client_settings_receive(const void *payload, size_t length) {
  acip_settings_header_t header;
  const char *json;
  size_t json_length;
  asciichat_error_t result = acip_settings_decode(payload, length, &header, &json, &json_length);
  if (result != ASCIICHAT_OK)
    return result;
  options_t *current = SAFE_MALLOC(sizeof(*current), options_t *),
            *candidate = SAFE_MALLOC(sizeof(*candidate), options_t *);
  acip_render_settings_options(&remote_render_settings, current);
  result = options_prepare_json(current, json, json_length, candidate, true);
  if (result == ASCIICHAT_OK)
    result = acip_render_settings_prepare(&remote_render_settings, candidate);
  if (result == ASCIICHAT_OK)
    remote_render_settings.revision = header.revision;
  SAFE_FREE(current);
  SAFE_FREE(candidate);
  return result;
}
EMSCRIPTEN_KEEPALIVE char *client_settings_values(void) {
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  acip_render_settings_options(&remote_render_settings, values);
  char *json = options_values_json(values, true);
  SAFE_FREE(values);
  return json;
}
/**
 * @file client_wasm.c
 * @brief WASM entry point for ascii-chat client mode
 */

#include <emscripten.h>
#include <emscripten/html5.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "common/init.h"

// JavaScript callback for sending complete ACIP packets from WASM to WebSocket
// This will be called by the WASM transport to send complete packets (header + payload)
// clang-format off
EM_JS(void, js_send_raw_packet, (const uint8_t *packet_data, size_t packet_len), {
  if (!Module.sendPacketCallback) {
    console.error('[WASM] sendPacketCallback not registered - cannot send packet');
    return;
  }

  // Copy complete packet from WASM memory to JavaScript
  const packetArray = new Uint8Array(HEAPU8.buffer, packet_data, packet_len);
  const packetCopy = new Uint8Array(packetArray);

  // Send as raw binary packet via WebSocket
  Module.sendPacketCallback(packetCopy);
});
// clang-format on

#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/network/acip/acds_protocol.h>
#include <ascii-chat/crypto/ssh/ssh_keys.h>
#include <ascii-chat/crypto/gpg/openpgp.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/font.h>

/* Browser terminal rendering reads the embedded font directly from WASM memory. */
const unsigned char *get_font_default_ptr(int selection) {
  bool matrix = selection == 0 ? GET_OPTION(matrix_rain) : selection == 2;
  return matrix ? g_font_matrix_resurrected : g_font_default;
}

unsigned int get_font_default_size(int selection) {
  bool matrix = selection == 0 ? GET_OPTION(matrix_rain) : selection == 2;
  return (unsigned int)(matrix ? g_font_matrix_resurrected_size : g_font_default_size);
}
#include <ascii-chat/common.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/magic.h>
#include <opus.h>
#include <sodium.h>

// ============================================================================
// WASM Transport Implementation
// ============================================================================

/**
 * WASM transport that forwards complete ACIP packets to JavaScript
 */
static asciichat_error_t wasm_transport_send(acip_transport_t *transport, const void *data, size_t len) {

  // Forward complete packet (header + payload) to JavaScript WebSocket bridge
  js_send_raw_packet((const uint8_t *)data, len);

  return ASCIICHAT_OK;
}

static asciichat_error_t wasm_transport_recv(acip_transport_t *transport, void **buffer, size_t *out_len,
                                             void **out_allocated_buffer) {
  // Not used - packets arrive via JavaScript callbacks
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "recv not supported on WASM transport");
}

static asciichat_error_t wasm_transport_close(acip_transport_t *transport) {
  return ASCIICHAT_OK; // Nothing to close
}

static acip_transport_type_t wasm_transport_get_type(acip_transport_t *transport) {
  return ACIP_TRANSPORT_WEBSOCKET; // Closest match
}

static socket_t wasm_transport_get_socket(acip_transport_t *transport) {
  return INVALID_SOCKET_VALUE;
}

static bool wasm_transport_is_connected(acip_transport_t *transport) {
  return true; // Always "connected" from WASM perspective
}

static const acip_transport_methods_t wasm_transport_methods = {.send = wasm_transport_send,
                                                                .recv = wasm_transport_recv,
                                                                .close = wasm_transport_close,
                                                                .get_type = wasm_transport_get_type,
                                                                .get_socket = wasm_transport_get_socket,
                                                                .is_connected = wasm_transport_is_connected,
                                                                .destroy_impl = NULL};

static acip_transport_t g_wasm_transport = {.methods = &wasm_transport_methods, .crypto_ctx = NULL, .impl_data = NULL};

// ============================================================================
// Global State
// ============================================================================

// Global state for client session
static crypto_handshake_context_t g_crypto_handshake_ctx = {0};
static bool g_initialized = false;
static bool g_handshake_complete = false;
static uint8_t g_crypto_mode = ACIP_CRYPTO_ENCRYPT;
static bool g_version_received = false;
static char g_crypto_password[256] = {0};
static uint8_t g_client_identity_key[crypto_sign_SECRETKEYBYTES] = {0};
static uint8_t g_expected_server_identity_key[crypto_sign_PUBLICKEYBYTES] = {0};
static bool g_has_client_identity_key = false;
static bool g_has_expected_server_identity_key = false;

// Opus codec state
static OpusEncoder *g_opus_encoder = NULL;
static OpusDecoder *g_opus_decoder = NULL;

// Connection state enum (exposed to JS)
typedef enum {
  CONNECTION_STATE_DISCONNECTED = 0,
  CONNECTION_STATE_CONNECTING = 1,
  CONNECTION_STATE_HANDSHAKE = 2,
  CONNECTION_STATE_CONNECTED = 3,
  CONNECTION_STATE_ERROR = 4
} connection_state_t;

static connection_state_t g_connection_state = CONNECTION_STATE_DISCONNECTED;

static int apply_client_crypto_options(void) {
  crypto_handshake_context_t *ctx = &g_crypto_handshake_ctx;
  if (ctx->state != CRYPTO_HANDSHAKE_INIT)
    return -1;

  if (g_crypto_password[0] != '\0') {
    crypto_result_t result = crypto_derive_password_key(&ctx->crypto_ctx, g_crypto_password);
    if (result != CRYPTO_OK)
      return -1;
    ctx->crypto_ctx.has_password = true;
    ctx->has_password = true;
    SAFE_STRNCPY(ctx->password, g_crypto_password, sizeof(ctx->password) - 1);
  }

  if (g_has_client_identity_key) {
    ctx->client_private_key.type = KEY_TYPE_ED25519;
    memcpy(ctx->client_private_key.key.ed25519, g_client_identity_key, sizeof(g_client_identity_key));
    memcpy(ctx->client_private_key.public_key, g_client_identity_key + crypto_sign_SEEDBYTES,
           crypto_sign_PUBLICKEYBYTES);
    memcpy(ctx->client_public_key.key, g_client_identity_key + crypto_sign_SEEDBYTES, crypto_sign_PUBLICKEYBYTES);
    ctx->client_public_key.type = KEY_TYPE_ED25519;
  }

  if (g_has_expected_server_identity_key) {
    memcpy(ctx->expected_server_key_bytes, g_expected_server_identity_key, sizeof(ctx->expected_server_key_bytes));
    ctx->expected_server_key_bytes_set = true;
    ctx->verify_server_key = true;
  }
  return 0;
}

/** Configure browser-provided authentication material for the next handshake. */
EMSCRIPTEN_KEEPALIVE
int client_configure_crypto_options(const char *password, const uint8_t *identity_secret_key,
                                    const uint8_t *expected_server_public_key) {
  if (!g_initialized)
    return -1;

  sodium_memzero(g_crypto_password, sizeof(g_crypto_password));
  sodium_memzero(g_client_identity_key, sizeof(g_client_identity_key));
  sodium_memzero(g_expected_server_identity_key, sizeof(g_expected_server_identity_key));
  g_has_client_identity_key = false;
  g_has_expected_server_identity_key = false;

  if (password && password[0] != '\0') {
    size_t length = strnlen(password, sizeof(g_crypto_password));
    if (length < 8 || length >= sizeof(g_crypto_password))
      return -1;
    SAFE_STRNCPY(g_crypto_password, password, sizeof(g_crypto_password) - 1);
  }

  if (identity_secret_key) {
    uint8_t derived_public_key[crypto_sign_PUBLICKEYBYTES];
    if (crypto_sign_ed25519_sk_to_pk(derived_public_key, identity_secret_key) != 0 ||
        sodium_memcmp(derived_public_key, identity_secret_key + crypto_sign_SEEDBYTES, crypto_sign_PUBLICKEYBYTES) !=
            0) {
      sodium_memzero(derived_public_key, sizeof(derived_public_key));
      return -1;
    }
    memcpy(g_client_identity_key, identity_secret_key, sizeof(g_client_identity_key));
    sodium_memzero(derived_public_key, sizeof(derived_public_key));
    g_has_client_identity_key = true;
  }

  if (expected_server_public_key) {
    memcpy(g_expected_server_identity_key, expected_server_public_key, sizeof(g_expected_server_identity_key));
    g_has_expected_server_identity_key = true;
  }
  return 0;
}

/** Parse an OpenSSH Ed25519 private-key file through the shared SSH parser. */
EMSCRIPTEN_KEEPALIVE
int client_parse_ssh_private_key(const char *key_path, uint8_t *secret_key_out) {
  if (!key_path || !secret_key_out)
    return -1;
  CLEAR_ERRNO_ALL();
#ifdef EMSCRIPTEN_BUILD
  static const char wasm_key_prefix[] = "/tmp/ascii-chat-key-";
  if (strncmp(key_path, wasm_key_prefix, sizeof(wasm_key_prefix) - 1) != 0)
    return -1;
#endif
  private_key_t key = {0};
  asciichat_error_t result = parse_ssh_private_key(key_path, &key);
  if (result != ASCIICHAT_OK || key.type != KEY_TYPE_ED25519 || key.use_ssh_agent || key.use_gpg_agent) {
    sodium_memzero(&key, sizeof(key));
    return -1;
  }
  memcpy(secret_key_out, key.key.ed25519, crypto_sign_SECRETKEYBYTES);
  sodium_memzero(&key, sizeof(key));
  return 0;
}

/** Parse an OpenSSH Ed25519 public-key line through the shared SSH parser. */
EMSCRIPTEN_KEEPALIVE
int client_parse_ssh_public_key(const char *key_line, uint8_t *public_key_out) {
  if (!key_line || !public_key_out)
    return -1;
  CLEAR_ERRNO_ALL();
  return parse_ssh_ed25519_line(key_line, public_key_out) == ASCIICHAT_OK ? 0 : -1;
}

/** Parse an armored OpenPGP Ed25519 public key through the shared C parser. */
EMSCRIPTEN_KEEPALIVE
int client_parse_gpg_public_key(const char *armored_key, uint8_t *public_key_out) {
  if (!armored_key || !public_key_out)
    return -1;
  CLEAR_ERRNO_ALL();
  return openpgp_parse_armored_pubkey(armored_key, public_key_out) == ASCIICHAT_OK ? 0 : -1;
}

/** Parse a binary OpenPGP Ed25519 public-key packet stream. */
EMSCRIPTEN_KEEPALIVE
int client_parse_gpg_public_key_binary(const uint8_t *key_data, size_t key_data_len, uint8_t *public_key_out) {
  if (!key_data || key_data_len == 0 || !public_key_out)
    return -1;
  CLEAR_ERRNO_ALL();
  return openpgp_parse_binary_pubkey(key_data, key_data_len, public_key_out) == ASCIICHAT_OK ? 0 : -1;
}

/** Parse an unencrypted armored OpenPGP Ed25519 private key through the shared C parser. */
EMSCRIPTEN_KEEPALIVE
int client_parse_gpg_private_key(const char *armored_key, uint8_t *secret_key_out) {
  if (!armored_key || !secret_key_out)
    return -1;
  CLEAR_ERRNO_ALL();

  uint8_t public_key[crypto_sign_PUBLICKEYBYTES] = {0};
  uint8_t seed[crypto_sign_SEEDBYTES] = {0};
  uint8_t derived_public_key[crypto_sign_PUBLICKEYBYTES] = {0};
  uint8_t secret_key[crypto_sign_SECRETKEYBYTES] = {0};
  int result = -1;

  if (openpgp_parse_armored_seckey(armored_key, public_key, seed) != ASCIICHAT_OK)
    goto cleanup;
  if (crypto_sign_seed_keypair(derived_public_key, secret_key, seed) != 0 ||
      sodium_memcmp(derived_public_key, public_key, sizeof(public_key)) != 0) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "OpenPGP private key does not match its Ed25519 public key");
    goto cleanup;
  }

  memcpy(secret_key_out, secret_key, sizeof(secret_key));
  result = 0;

cleanup:
  sodium_memzero(public_key, sizeof(public_key));
  sodium_memzero(seed, sizeof(seed));
  sodium_memzero(derived_public_key, sizeof(derived_public_key));
  sodium_memzero(secret_key, sizeof(secret_key));
  return result;
}

/** Parse an unencrypted binary OpenPGP Ed25519 secret-key packet stream. */
EMSCRIPTEN_KEEPALIVE
int client_parse_gpg_private_key_binary(const uint8_t *key_data, size_t key_data_len, uint8_t *secret_key_out) {
  if (!key_data || key_data_len == 0 || !secret_key_out)
    return -1;
  CLEAR_ERRNO_ALL();

  uint8_t public_key[crypto_sign_PUBLICKEYBYTES] = {0};
  uint8_t seed[crypto_sign_SEEDBYTES] = {0};
  uint8_t derived_public_key[crypto_sign_PUBLICKEYBYTES] = {0};
  uint8_t secret_key[crypto_sign_SECRETKEYBYTES] = {0};
  int result = -1;

  if (openpgp_parse_binary_seckey(key_data, key_data_len, public_key, seed) != ASCIICHAT_OK)
    goto cleanup;
  if (crypto_sign_seed_keypair(derived_public_key, secret_key, seed) != 0 ||
      sodium_memcmp(derived_public_key, public_key, sizeof(public_key)) != 0) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "OpenPGP private key does not match its Ed25519 public key");
    goto cleanup;
  }

  memcpy(secret_key_out, secret_key, sizeof(secret_key));
  result = 0;

cleanup:
  sodium_memzero(public_key, sizeof(public_key));
  sodium_memzero(seed, sizeof(seed));
  sodium_memzero(derived_public_key, sizeof(derived_public_key));
  sodium_memzero(secret_key, sizeof(secret_key));
  return result;
}

/** Copy the most recent C crypto error into a caller-provided buffer. */
EMSCRIPTEN_KEEPALIVE
int client_get_crypto_error_message(char *output, size_t output_size) {
  if (!output || output_size == 0)
    return -1;
  asciichat_error_context_t context = {0};
  if (!HAS_ERRNO(&context) || !context.context_message[0])
    return -1;
  safe_snprintf(output, output_size, "%s", context.context_message);
  return 0;
}

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize client mode with command-line style arguments
 * @param args_json Space-separated string of argument strings, e.g. "client --width 80 --height 40"
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_init_with_args(const char *args_json) {
  // The module, renderers, and allocator outlive individual connections.
  if (g_initialized)
    return 0;
  // Parse space-separated arguments
  char *args_copy = NULL;
  char *argv[64] = {NULL};
  int argc = wasm_parse_args(args_json, argv, 64, &args_copy);
  if (argc < 0) {
    return -1;
  }

  // Initialize options (sets up RCU, defaults, etc.)
  asciichat_error_t err = options_init(argc, argv);
  free(args_copy);

  // Initialize platform layer
  err = platform_init();
  if (err != ASCIICHAT_OK) {
    return -1;
  }

  // Honor the requested verbosity and let INFO use stdout (console.log).
  log_init(NULL, GET_OPTION(log_level), false, false);
  // Use debug-style format in WASM so browser console shows full log headers
  log_set_format(OPT_LOG_TEMPLATE_DEFAULT_DEBUG, false);
  log_info("WASM client initialized via logging system");

  if (err != ASCIICHAT_OK) {
    return -1;
  }

  // Initialize ANSI color code generation
  ansi_fast_init();

  g_initialized = true;
  g_connection_state = CONNECTION_STATE_DISCONNECTED;

  return 0;
}

EMSCRIPTEN_KEEPALIVE
void client_cleanup(void) {
  remote_render_settings = (acip_render_settings_t){0};
  errno_destroy();

  // Clean up crypto handshake context
  crypto_handshake_destroy(&g_crypto_handshake_ctx);
  memset(&g_crypto_handshake_ctx, 0, sizeof(g_crypto_handshake_ctx));

  g_handshake_complete = false;
  g_wasm_transport.crypto_ctx = NULL;
  g_connection_state = CONNECTION_STATE_DISCONNECTED;
  sodium_memzero(g_crypto_password, sizeof(g_crypto_password));
  sodium_memzero(g_client_identity_key, sizeof(g_client_identity_key));
  sodium_memzero(g_expected_server_identity_key, sizeof(g_expected_server_identity_key));
  g_has_client_identity_key = false;
  g_has_expected_server_identity_key = false;
  // Keep module-wide options and buffer domains alive while browser renderers
  // still reference them. Connection cleanup only resets authentication state.
}

// ============================================================================
// Cryptography API
// ============================================================================

/**
 * Generate client keypair for handshake
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_generate_keypair(void) {

  if (!g_initialized) {
    return -1;
  }

  // Reset handshake context if previously used (reconnection / React Strict Mode remount)
  if (g_crypto_handshake_ctx.state != CRYPTO_HANDSHAKE_DISABLED) {
    crypto_handshake_destroy(&g_crypto_handshake_ctx);
    memset(&g_crypto_handshake_ctx, 0, sizeof(g_crypto_handshake_ctx));
    g_handshake_complete = false;
  }

  // Initialize crypto handshake context
  asciichat_error_t result = crypto_handshake_init("wasm-client", &g_crypto_handshake_ctx, false /* is_server */);
  if (result != ASCIICHAT_OK) {
    return -1;
  }
  if (apply_client_crypto_options() != 0) {
    crypto_handshake_destroy(&g_crypto_handshake_ctx);
    memset(&g_crypto_handshake_ctx, 0, sizeof(g_crypto_handshake_ctx));
    return -1;
  }

  g_connection_state = CONNECTION_STATE_DISCONNECTED;
  return 0;
}

/**
 * Set server address for known_hosts verification
 * @param server_host Server hostname or IP address
 * @param server_port Server port number
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_set_server_address(const char *server_host, int server_port) {
  if (!g_initialized) {
    return -1;
  }

  if (!server_host || server_port <= 0 || server_port > 65535) {
    return -1;
  }

  // Set server IP and port in handshake context
  SAFE_STRNCPY(g_crypto_handshake_ctx.server_ip, server_host, sizeof(g_crypto_handshake_ctx.server_ip));
  g_crypto_handshake_ctx.server_port = server_port;

  return 0;
}

/**
 * Get client public key as hex string
 * @return Allocated hex string (caller must free with client_free_string), or NULL on error
 */
EMSCRIPTEN_KEEPALIVE
char *client_get_public_key_hex(void) {
  if (g_crypto_handshake_ctx.state == CRYPTO_HANDSHAKE_DISABLED) {
    return NULL;
  }

  // Get public key from handshake context
  const uint8_t *pubkey = g_crypto_handshake_ctx.crypto_ctx.public_key;
  if (!pubkey) {
    return NULL;
  }

  // Allocate buffer for hex string (32 bytes = 64 hex chars + null terminator)
  char *hex_buffer = SAFE_MALLOC(65, char *);
  if (!hex_buffer) {
    return NULL;
  }

  // Convert public key to hex
  for (int i = 0; i < 32; i++) {
    snprintf(hex_buffer + (i * 2), 3, "%02x", pubkey[i]);
  }
  hex_buffer[64] = '\0';

  return hex_buffer;
}

/**
 * Handle CRYPTO_KEY_EXCHANGE_INIT packet from server
 * This is the first step of the crypto handshake
 * @param packet Raw packet data including header
 * @param packet_len Total packet length
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_handle_key_exchange_init(const uint8_t *packet, size_t packet_len) {

  // Safety check: if handshake context is not in INIT state, reinitialize it
  // This handles cases where previous handshakes weren't properly cleaned up
  if (g_crypto_handshake_ctx.state != CRYPTO_HANDSHAKE_INIT) {

    // Destroy and reset
    if (g_crypto_handshake_ctx.state != CRYPTO_HANDSHAKE_DISABLED) {
      crypto_handshake_destroy(&g_crypto_handshake_ctx);
    }
    memset(&g_crypto_handshake_ctx, 0, sizeof(g_crypto_handshake_ctx));

    // Reinitialize to INIT state
    asciichat_error_t init_result = crypto_handshake_init("wasm-client", &g_crypto_handshake_ctx, false);
    if (init_result != ASCIICHAT_OK) {
      return -1;
    }
    if (apply_client_crypto_options() != 0)
      return -1;
  }

  if (!packet || packet_len == 0) {
    return -1;
  }

  // Extract packet type and payload
  if (packet_len < sizeof(packet_header_t)) {
    return -1;
  }

  const packet_header_t *header = (const packet_header_t *)packet;
  packet_type_t packet_type = ntohs(header->type);
  const uint8_t *payload_src = packet + sizeof(packet_header_t);
  size_t payload_len = packet_len - sizeof(packet_header_t);

  // Allocate payload copy from buffer domain (crypto function takes ownership and frees it)
  // The raw packet pointer from JS cannot be passed directly because the crypto
  // handshake function calls buffer_release() on the payload when done.
  uint8_t *payload = NULL;
  if (payload_len > 0) {
    payload = buffer_alloc(NULL, payload_len);
    if (!payload) {
      g_connection_state = CONNECTION_STATE_ERROR;
      return -1;
    }
    memcpy(payload, payload_src, payload_len);
  }

  // Process key exchange using transport-abstracted handshake

  asciichat_error_t result = crypto_handshake_client_key_exchange(&g_crypto_handshake_ctx, &g_wasm_transport,
                                                                  packet_type, payload, payload_len);

  if (result != ASCIICHAT_OK) {
    g_connection_state = CONNECTION_STATE_ERROR;
    return -1;
  }

  g_connection_state = CONNECTION_STATE_HANDSHAKE;
  return 0;
}

/**
 * Handle CRYPTO_PARAMETERS packet from server
 * Sets up the key size negotiation before KEY_EXCHANGE_INIT
 * @param packet Raw packet data including header
 * @param packet_len Total packet length
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_handle_crypto_parameters(const uint8_t *packet, size_t packet_len) {

  if (!packet || packet_len == 0) {
    return -1;
  }

  // Discovery sends CRYPTO_PARAMETERS before KEY_EXCHANGE_INIT. Initialize
  // here so the negotiated values survive the subsequent key exchange packet.
  if (g_crypto_handshake_ctx.state != CRYPTO_HANDSHAKE_INIT) {
    if (g_crypto_handshake_ctx.state != CRYPTO_HANDSHAKE_DISABLED) {
      crypto_handshake_destroy(&g_crypto_handshake_ctx);
    }
    memset(&g_crypto_handshake_ctx, 0, sizeof(g_crypto_handshake_ctx));
    if (crypto_handshake_init("wasm-client", &g_crypto_handshake_ctx, false) != ASCIICHAT_OK) {
      return -1;
    }
    if (apply_client_crypto_options() != 0)
      return -1;
  }

  // Extract packet type and payload
  if (packet_len < sizeof(packet_header_t)) {
    return -1;
  }

  const packet_header_t *header = (const packet_header_t *)packet;
  packet_type_t packet_type = ntohs(header->type);
  const uint8_t *payload_src = packet + sizeof(packet_header_t);
  size_t payload_len = packet_len - sizeof(packet_header_t);

  // Allocate payload copy from buffer domain
  uint8_t *payload = NULL;
  if (payload_len > 0) {
    payload = buffer_alloc(NULL, payload_len);
    if (!payload) {
      g_connection_state = CONNECTION_STATE_ERROR;
      return -1;
    }
    memcpy(payload, payload_src, payload_len);
  }

  // Process crypto parameters
  asciichat_error_t result =
      crypto_handshake_set_parameters(&g_crypto_handshake_ctx, (const crypto_parameters_packet_t *)payload);

  // Free payload
  if (payload) {
    buffer_release(payload);
  }

  if (result != ASCIICHAT_OK) {
    g_connection_state = CONNECTION_STATE_ERROR;
    return -1;
  }

  return 0;
}

/**
 * Handle CRYPTO_AUTH_CHALLENGE packet from server
 * @param packet Raw packet data including header
 * @param packet_len Total packet length
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_handle_auth_challenge(const uint8_t *packet, size_t packet_len) {

  if (!packet || packet_len == 0) {
    return -1;
  }

  // Extract packet type and payload
  if (packet_len < sizeof(packet_header_t)) {
    return -1;
  }

  const packet_header_t *header = (const packet_header_t *)packet;
  packet_type_t packet_type = ntohs(header->type);
  const uint8_t *payload_src = packet + sizeof(packet_header_t);
  size_t payload_len = packet_len - sizeof(packet_header_t);

  // Allocate payload copy from buffer domain (crypto function takes ownership and frees it)
  uint8_t *payload = NULL;
  if (payload_len > 0) {
    payload = buffer_alloc(NULL, payload_len);
    if (!payload) {
      g_connection_state = CONNECTION_STATE_ERROR;
      return -1;
    }
    memcpy(payload, payload_src, payload_len);
  }

  // Process auth challenge
  asciichat_error_t result = crypto_handshake_client_auth_response(&g_crypto_handshake_ctx, &g_wasm_transport,
                                                                   packet_type, payload, payload_len);

  if (result != ASCIICHAT_OK) {
    g_connection_state = CONNECTION_STATE_ERROR;
    return -1;
  }

  return 0;
}

/**
 * Handle CRYPTO_HANDSHAKE_COMPLETE packet from server
 * @param packet Raw packet data including header
 * @param packet_len Total packet length
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_handle_handshake_complete(const uint8_t *packet, size_t packet_len) {

  if (!packet || packet_len == 0) {
    return -1;
  }

  // Extract packet type and payload
  if (packet_len < sizeof(packet_header_t)) {
    return -1;
  }

  const packet_header_t *header = (const packet_header_t *)packet;
  packet_type_t packet_type = ntohs(header->type);
  const uint8_t *payload_src = packet + sizeof(packet_header_t);
  size_t payload_len = packet_len - sizeof(packet_header_t);

  // Allocate payload copy from buffer domain (crypto function takes ownership and frees it)
  uint8_t *payload = NULL;
  if (payload_len > 0) {
    payload = buffer_alloc(NULL, payload_len);
    if (!payload) {
      g_connection_state = CONNECTION_STATE_ERROR;
      return -1;
    }
    memcpy(payload, payload_src, payload_len);
  }

  // Complete handshake (takes ownership of payload and will free it)
  asciichat_error_t result =
      crypto_handshake_client_complete(&g_crypto_handshake_ctx, &g_wasm_transport, packet_type, payload, payload_len);

  if (result != ASCIICHAT_OK) {
    g_connection_state = CONNECTION_STATE_ERROR;
    return -1;
  }

  g_handshake_complete = true;
  g_connection_state = CONNECTION_STATE_CONNECTED;

  // Update transport with the crypto context so ACIP can encrypt/decrypt packets
  g_wasm_transport.crypto_ctx = &g_crypto_handshake_ctx.crypto_ctx;

  return 0;
}

// ============================================================================
// Packet Processing API
// ============================================================================

/**
 * Encrypt a plaintext packet
 * @param plaintext Input plaintext data
 * @param plaintext_len Length of plaintext
 * @param ciphertext Output buffer (allocated by caller, must be at least plaintext_len + crypto_aead_overhead)
 * @param ciphertext_size Size of ciphertext buffer
 * @param out_len Output parameter for ciphertext length
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_encrypt_packet(const uint8_t *plaintext, size_t plaintext_len, uint8_t *ciphertext, size_t ciphertext_size,
                          size_t *out_len) {
  if (!g_handshake_complete) {
    return -1;
  }

  // Encrypt the packet using handshake context's crypto context
  size_t ciphertext_len = 0;
  crypto_result_t result = crypto_encrypt(&g_crypto_handshake_ctx.crypto_ctx, plaintext, plaintext_len, ciphertext,
                                          ciphertext_size, &ciphertext_len);
  if (result != CRYPTO_OK) {
    return -1;
  }

  *out_len = ciphertext_len;
  return 0;
}

/**
 * Decrypt a ciphertext packet
 * @param ciphertext Input ciphertext data
 * @param ciphertext_len Length of ciphertext
 * @param plaintext Output buffer (allocated by caller, must be at least ciphertext_len)
 * @param plaintext_size Size of plaintext buffer
 * @param out_len Output parameter for plaintext length
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_decrypt_packet(const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *plaintext, size_t plaintext_size,
                          size_t *out_len) {
  if (!g_handshake_complete) {
    return -1;
  }

  // Decrypt the packet using handshake context's crypto context
  size_t plaintext_len = 0;
  crypto_result_t result = crypto_decrypt(&g_crypto_handshake_ctx.crypto_ctx, ciphertext, ciphertext_len, plaintext,
                                          plaintext_size, &plaintext_len);
  if (result != CRYPTO_OK) {
    return -1;
  }

  *out_len = plaintext_len;
  return 0;
}

/**
 * Parse a raw packet and return JSON metadata
 * @param raw_packet Raw packet bytes including header
 * @param packet_len Length of raw packet
 * @return Allocated JSON string with packet metadata (caller must free), or NULL on error
 */
EMSCRIPTEN_KEEPALIVE
char *client_parse_packet(const uint8_t *raw_packet, size_t packet_len) {
  if (!raw_packet || packet_len < sizeof(packet_header_t)) {
    return NULL;
  }

  // Parse packet header directly from bytes
  const packet_header_t *header = (const packet_header_t *)raw_packet;

  // Validate magic number (convert from network byte order)
  uint64_t magic = NET_TO_HOST_U64(header->magic);
  if (magic != PACKET_MAGIC) {
    return NULL;
  }

  // Convert header fields from network byte order
  uint16_t type = NET_TO_HOST_U16(header->type);
  uint32_t length = NET_TO_HOST_U32(header->length);
  uint32_t client_id = NET_TO_HOST_U32(header->client_id);
  uint32_t crc32 = NET_TO_HOST_U32(header->crc32);

  if (length != packet_len - sizeof(packet_header_t) ||
      (length > 0 && asciichat_crc32_sw(raw_packet + sizeof(packet_header_t), length) != crc32)) {
    return NULL;
  }

  // Build JSON response with packet metadata
  char *json = SAFE_MALLOC(1024, char *);
  if (!json) {
    return NULL;
  }

  snprintf(json, 1024, "{\"type\":%u,\"length\":%u,\"client_id\":%u,\"crc32\":%u}", type, length, client_id, crc32);

  return json;
}

/**
 * Serialize a packet structure to raw bytes
 * @param packet_type Type of packet
 * @param payload Packet payload data
 * @param payload_len Length of payload
 * @param client_id Client ID to include in header
 * @param output Output buffer (allocated by caller)
 * @param out_len Output parameter for total packet length
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_serialize_packet(uint16_t packet_type, const uint8_t *payload, size_t payload_len, uint32_t client_id,
                            uint8_t *output, size_t *out_len) {
  if (!output || !out_len) {
    return -1;
  }

  // Calculate CRC32 of payload (use software version for WASM)
  uint32_t crc = payload && payload_len > 0 ? asciichat_crc32_sw(payload, payload_len) : 0;

  // Build packet header (convert to network byte order)
  packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                            .type = HOST_TO_NET_U16(packet_type),
                            .length = HOST_TO_NET_U32((uint32_t)payload_len),
                            .crc32 = HOST_TO_NET_U32(crc),
                            .client_id = HOST_TO_NET_U32(client_id)};

  // Copy header to output
  memcpy(output, &header, sizeof(packet_header_t));

  // Copy payload to output
  if (payload && payload_len > 0) {
    memcpy(output + sizeof(packet_header_t), payload, payload_len);
  }

  *out_len = sizeof(packet_header_t) + payload_len;
  return 0;
}

/**
 * Process a video frame and prepare it for sending
 * @param rgba_data RGBA pixel data from canvas
 * @param width Frame width
 * @param height Frame height
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_send_video_frame(const uint8_t *rgba_data, int width, int height) {
  if (!g_initialized) {
    return -1;
  }

  // This is a placeholder - actual implementation would:
  // 1. Convert RGBA to target format (e.g., compress with libjpeg-turbo)
  // 2. Build frame packet
  // 3. Encrypt if handshake complete
  // 4. Return serialized packet via callback to JS

  return 0;
}

// ============================================================================
// Connection State API
// ============================================================================

/**
 * Get current connection state
 * @return Connection state enum value
 */
EMSCRIPTEN_KEEPALIVE
int client_get_connection_state(void) {
  return (int)g_connection_state;
}

// ============================================================================
// Memory Management
// ============================================================================

EMSCRIPTEN_KEEPALIVE
void client_free_string(char *ptr) {
  SAFE_FREE(ptr);
}

// ============================================================================
// Opus Audio Codec API
// ============================================================================

/**
 * Initialize Opus encoder
 * @param sample_rate Sample rate (8000, 16000, 24000, or 48000)
 * @param channels Number of channels (1=mono, 2=stereo)
 * @param bitrate Target bitrate in bits/sec
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_opus_encoder_init(int sample_rate, int channels, int bitrate) {
  if (g_opus_encoder) {
    opus_encoder_destroy(g_opus_encoder);
    g_opus_encoder = NULL;
  }

  int error;
  g_opus_encoder = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_VOIP, &error);
  if (error != OPUS_OK || !g_opus_encoder) {
    return -1;
  }

  // Set bitrate
  opus_encoder_ctl(g_opus_encoder, OPUS_SET_BITRATE(bitrate));

  return 0;
}

/**
 * Initialize Opus decoder
 * @param sample_rate Sample rate (8000, 16000, 24000, or 48000)
 * @param channels Number of channels (1=mono, 2=stereo)
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_opus_decoder_init(int sample_rate, int channels) {
  if (g_opus_decoder) {
    opus_decoder_destroy(g_opus_decoder);
    g_opus_decoder = NULL;
  }

  int error;
  g_opus_decoder = opus_decoder_create(sample_rate, channels, &error);
  if (error != OPUS_OK || !g_opus_decoder) {
    return -1;
  }

  return 0;
}

/**
 * Encode PCM audio to Opus
 * @param pcm_data Input PCM data (Int16 samples)
 * @param frame_size Number of samples per channel
 * @param opus_data Output buffer for Opus data
 * @param max_opus_bytes Maximum size of output buffer
 * @return Number of bytes encoded, or -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_opus_encode(const int16_t *pcm_data, int frame_size, uint8_t *opus_data, int max_opus_bytes) {
  if (!g_opus_encoder) {
    return -1;
  }

  int encoded_bytes = opus_encode(g_opus_encoder, pcm_data, frame_size, opus_data, max_opus_bytes);
  if (encoded_bytes < 0) {
    return -1;
  }

  return encoded_bytes;
}

/**
 * Decode Opus audio to PCM
 * @param opus_data Input Opus data
 * @param opus_bytes Length of Opus data
 * @param pcm_data Output buffer for PCM samples (Int16)
 * @param frame_size Maximum number of samples per channel
 * @param decode_fec Whether to use FEC
 * @return Number of samples decoded per channel, or -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_opus_decode(const uint8_t *opus_data, int opus_bytes, int16_t *pcm_data, int frame_size, int decode_fec) {
  if (!g_opus_decoder) {
    return -1;
  }

  int decoded_samples = opus_decode(g_opus_decoder, opus_data, opus_bytes, pcm_data, frame_size, decode_fec);
  if (decoded_samples < 0) {
    return -1;
  }

  return decoded_samples;
}

/**
 * Cleanup Opus encoder
 */
EMSCRIPTEN_KEEPALIVE
void client_opus_encoder_cleanup(void) {
  if (g_opus_encoder) {
    opus_encoder_destroy(g_opus_encoder);
    g_opus_encoder = NULL;
  }
}

/**
 * Cleanup Opus decoder
 */
EMSCRIPTEN_KEEPALIVE
void client_opus_decoder_cleanup(void) {
  if (g_opus_decoder) {
    opus_decoder_destroy(g_opus_decoder);
    g_opus_decoder = NULL;
  }
}

EMSCRIPTEN_KEEPALIVE asciichat_error_t client_acds_handshake_begin(uint8_t mode) {
  g_crypto_handshake_ctx.crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(mode);
  return acds_handshake_begin(&g_wasm_transport, mode);
}
EMSCRIPTEN_KEEPALIVE asciichat_error_t client_acds_handshake_receive(uint32_t type, const void *payload,
                                                                     size_t length) {
  asciichat_error_t result = acds_handshake_receive(&g_crypto_handshake_ctx, &g_wasm_transport, type, payload, length);
  if (result != ASCIICHAT_OK) {
    g_connection_state = CONNECTION_STATE_ERROR;
    return result;
  }
  if (g_crypto_handshake_ctx.state == CRYPTO_HANDSHAKE_READY) {
    g_handshake_complete = true;
    g_wasm_transport.crypto_ctx = &g_crypto_handshake_ctx.crypto_ctx;
    g_connection_state = CONNECTION_STATE_CONNECTED;
  } else {
    g_connection_state = CONNECTION_STATE_HANDSHAKE;
  }
  return ASCIICHAT_OK;
}

/** Resolve browser inputs through the same policy as native and mobile clients. */
EMSCRIPTEN_KEEPALIVE int client_resolve_crypto_policy(bool secure, int encryption) {
  crypto_policy_t policy = {
      .transport_secure = secure,
      .encrypt = encryption == 1,
      .no_encrypt = encryption == 0,
      .has_password = g_crypto_password[0] != '\0',
      .has_identity = g_has_client_identity_key,
      .has_verification_key = g_has_expected_server_identity_key,
  };
  uint8_t mode;
  if (crypto_policy_resolve(&policy, &mode) != ASCIICHAT_OK)
    return -1;
  g_crypto_mode = mode;
  return mode;
}

EMSCRIPTEN_KEEPALIVE asciichat_error_t client_handshake_begin(void) {
  g_connection_state = CONNECTION_STATE_CONNECTING;
  g_version_received = false;
  g_crypto_handshake_ctx.crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(g_crypto_mode);
  return acds_handshake_begin(&g_wasm_transport, g_crypto_mode);
}

EMSCRIPTEN_KEEPALIVE asciichat_error_t client_handshake_receive(uint32_t type, const void *payload, size_t length) {
  if (!g_version_received && type != PACKET_TYPE_PROTOCOL_VERSION)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected server protocol version first");
  if (type == PACKET_TYPE_PROTOCOL_VERSION) {
    if (g_version_received)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Duplicate server protocol version");
    if (!payload || length != sizeof(protocol_version_packet_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid server protocol version");
    const protocol_version_packet_t *version = payload;
    if (NET_TO_HOST_U16(version->protocol_version) != 1 || version->supports_encryption != g_crypto_mode)
      return SET_ERRNO(ERROR_CRYPTO, "Server rejected requested security policy");
    g_version_received = true;
    if (g_crypto_mode == ACIP_CRYPTO_NONE) {
      g_connection_state = CONNECTION_STATE_CONNECTED;
      return ASCIICHAT_OK;
    }
    crypto_capabilities_packet_t caps = {0};
    caps.supported_kex_algorithms = HOST_TO_NET_U16(KEX_ALGO_X25519);
    caps.supported_auth_algorithms = HOST_TO_NET_U16(AUTH_ALGO_ED25519 | AUTH_ALGO_NONE);
    caps.supported_cipher_algorithms = HOST_TO_NET_U16(CIPHER_ALGO_XSALSA20_POLY1305 | CIPHER_ALGO_NONE);
    caps.preferred_kex = KEX_ALGO_X25519;
    caps.preferred_auth = ACIP_CRYPTO_HAS_AUTH(g_crypto_mode) ? AUTH_ALGO_ED25519 : AUTH_ALGO_NONE;
    caps.preferred_cipher = ACIP_CRYPTO_HAS_ENCRYPT(g_crypto_mode) ? CIPHER_ALGO_XSALSA20_POLY1305 : CIPHER_ALGO_NONE;
    caps.requires_verification = g_has_expected_server_identity_key;
    asciichat_error_t result =
        packet_send_via_transport(&g_wasm_transport, PACKET_TYPE_CRYPTO_CAPABILITIES, &caps, sizeof(caps), 0);
    if (result != ASCIICHAT_OK || !g_has_expected_server_identity_key)
      return result;
    return packet_send_via_transport(&g_wasm_transport, PACKET_TYPE_CRYPTO_CLIENT_HELLO, g_expected_server_identity_key,
                                     sizeof(g_expected_server_identity_key), 0);
  }
  if (type == PACKET_TYPE_CRYPTO_PARAMETERS) {
    if (!payload || length != sizeof(crypto_parameters_packet_t) ||
        (((const crypto_parameters_packet_t *)payload)->selected_cipher != CIPHER_ALGO_NONE) !=
            ACIP_CRYPTO_HAS_ENCRYPT(g_crypto_mode))
      return SET_ERRNO(ERROR_CRYPTO, "Server changed requested payload encryption");
  }
  return client_acds_handshake_receive(type, payload, length);
}

/* Browser transport marshals framing, then invokes the SAME network content
 * contract as native before any terminal parser or recording receives bytes. */
EMSCRIPTEN_KEEPALIVE asciichat_error_t client_validate_frame(const char *text, size_t length, uint32_t width,
                                                             uint32_t height, uint32_t checksum, uint32_t flags,
                                                             const void *metadata, size_t metadata_length) {
  if (flags & ~(FRAME_FLAG_RENDER_CONTRACT | FRAME_FLAG_IS_COMPRESSED | 1u))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unknown browser frame flags");
  acip_render_settings_t settings;
  uint32_t effects = 0;
  bool contract = (flags & FRAME_FLAG_RENDER_CONTRACT) != 0;
  if (contract) {
    size_t consumed;
    asciichat_error_t decoded = frame_render_contract_decode(metadata, metadata_length, &settings, &effects, &consumed);
    if (decoded != ASCIICHAT_OK)
      return decoded;
    if (settings.revision && settings.revision == remote_render_settings.revision &&
        remote_render_settings.palette[0] && remote_render_settings.caps.utf8_support &&
        strcmp(settings.palette, remote_render_settings.palette) != 0)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Browser frame palette contradicts acknowledged revision");
    if (consumed != metadata_length)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Trailing browser frame contract bytes");
  } else if (metadata_length) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected browser frame contract");
  }
  if (!text || !length || length > FRAME_RENDER_MAX_BYTES || asciichat_crc32(text, length) != checksum)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Browser frame length/checksum mismatch");
  return session_frame_validate_contract(text, length, width, height, contract ? &settings : NULL, effects);
}

EMSCRIPTEN_KEEPALIVE asciichat_error_t client_decode_frame_data(const void *encoded, size_t encoded_length,
                                                                void *output, size_t output_length) {
  if (!encoded || !output || !encoded_length || encoded_length > FRAME_RENDER_MAX_BYTES || !output_length ||
      output_length > FRAME_RENDER_MAX_BYTES)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid browser frame decompression bounds");
  if (decompress_data_exact(encoded, encoded_length, output, output_length) != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Decompressed ASCII frame size mismatch");
  return ASCIICHAT_OK;
}
