#include "client.h"

/* Storage capacities in elements; text buffers include the terminating NUL. */
#define WEB_CLIENT_CRYPTO_PASSWORD_BUFFER_SIZE 256

#include <ascii-chat/crypto/sizes.h>
#include <ascii-chat/network/compression.h>
#include "session/frame_validation.h"
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/crypto/policy.h>
#include <emscripten.h>
#include <ascii-chat/common.h>
#include <string.h>
#include <ascii-chat/network/acip/settings.h>
#include <ascii-chat/network/browser/transport.h>

typedef struct client_state client_state_t;
static client_state_t *client_state(browser_transport_t *transport);

/**
 * @file client_wasm.c
 * @brief WASM entry point for ascii-chat client mode
 */

#include <emscripten.h>
#include <emscripten/html5.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

#include <ascii-chat/common.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/magic.h>
#include <sodium.h>

// ============================================================================
// WASM Transport Implementation
// ============================================================================

static void wasm_transport_output(const void *data, size_t length, void *user_data) {
  (void)user_data;
  js_send_raw_packet((const uint8_t *)data, length);
}

// ============================================================================
// Global State
// ============================================================================

struct client_state {
  crypto_handshake_context_t crypto_handshake_ctx;
  bool initialized;
  bool handshake_complete;
  uint8_t crypto_mode;
  bool version_received;
  char crypto_password[WEB_CLIENT_CRYPTO_PASSWORD_BUFFER_SIZE];
  uint8_t client_identity_key[crypto_sign_SECRETKEYBYTES];
  uint8_t expected_server_identity_key[crypto_sign_PUBLICKEYBYTES];
  bool has_client_identity_key;
  bool has_expected_server_identity_key;
  acip_render_settings_t remote_render_settings;
};

static client_state_t *client_state(browser_transport_t *transport) {
  return transport ? browser_transport_client_state(transport) : NULL;
}

asciichat_error_t client_settings_receive(browser_transport_t *transport, const void *payload, size_t length) {
  client_state_t *state = client_state(transport);
  if (!state)
    return SET_ERRNO(ERROR_INVALID_STATE, "Browser client transport is not initialized");
  acip_settings_header_t header;
  const char *json;
  size_t json_length;
  asciichat_error_t result = acip_settings_decode(payload, length, &header, &json, &json_length);
  if (result != ASCIICHAT_OK)
    return result;
  options_t *current = SAFE_MALLOC(sizeof(*current), options_t *),
            *candidate = SAFE_MALLOC(sizeof(*candidate), options_t *);
  state->remote_render_settings.caps.utf8_support = true;
  acip_render_settings_options(&state->remote_render_settings, current);
  result = options_prepare_json(current, json, json_length, candidate, true);
  if (result == ASCIICHAT_OK)
    result = acip_render_settings_prepare(&state->remote_render_settings, candidate);
  if (result == ASCIICHAT_OK)
    state->remote_render_settings.revision = header.revision;
  SAFE_FREE(current);
  SAFE_FREE(candidate);
  return result;
}

char *client_settings_values(browser_transport_t *transport) {
  client_state_t *state = client_state(transport);
  if (!state) {
    SET_ERRNO(ERROR_INVALID_STATE, "Browser client transport is not initialized");
    return NULL;
  }
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  acip_render_settings_options(&state->remote_render_settings, values);
  char *json = options_values_json(values, true);
  SAFE_FREE(values);
  return json;
}

static int apply_client_crypto_options(browser_transport_t *transport) {
  client_state_t *state = client_state(transport);
  if (!state) {
    SET_ERRNO(ERROR_INVALID_STATE, "Client transport is not initialized");
    return -1;
  }
  crypto_handshake_context_t *ctx = &state->crypto_handshake_ctx;
  if (ctx->state != CRYPTO_HANDSHAKE_INIT) {
    SET_ERRNO(ERROR_INVALID_STATE, "Client handshake is not ready for crypto options");
    return -1;
  }

  if (state->crypto_password[0] != '\0') {
    crypto_result_t result = crypto_derive_password_key(&ctx->crypto_ctx, state->crypto_password);
    if (result != CRYPTO_OK)
      return -1;
    ctx->crypto_ctx.has_password = true;
    ctx->has_password = true;
    SAFE_STRNCPY(ctx->password, state->crypto_password, sizeof(ctx->password) - 1);
  }

  if (state->has_client_identity_key) {
    ctx->client_private_key.type = KEY_TYPE_ED25519;
    memcpy(ctx->client_private_key.key.ed25519, state->client_identity_key, sizeof(state->client_identity_key));
    memcpy(ctx->client_private_key.public_key, state->client_identity_key + crypto_sign_SEEDBYTES,
           crypto_sign_PUBLICKEYBYTES);
    memcpy(ctx->client_public_key.key, state->client_identity_key + crypto_sign_SEEDBYTES, crypto_sign_PUBLICKEYBYTES);
    ctx->client_public_key.type = KEY_TYPE_ED25519;
  }

  if (state->has_expected_server_identity_key) {
    memcpy(ctx->expected_server_key_bytes, state->expected_server_identity_key, sizeof(ctx->expected_server_key_bytes));
    ctx->expected_server_key_bytes_set = true;
    ctx->verify_server_key = true;
  }
  return 0;
}

/** Configure browser-provided authentication material for the next handshake. */
int client_configure_crypto_options(browser_transport_t *transport, const char *password, const uint8_t *identity_secret_key,
                                    const uint8_t *expected_server_public_key) {
  client_state_t *state = client_state(transport);
  if (!state || !state->initialized) {
    SET_ERRNO(ERROR_INVALID_STATE, "Client is not initialized");
    return -1;
  }

  sodium_memzero(state->crypto_password, sizeof(state->crypto_password));
  sodium_memzero(state->client_identity_key, sizeof(state->client_identity_key));
  sodium_memzero(state->expected_server_identity_key, sizeof(state->expected_server_identity_key));
  state->has_client_identity_key = false;
  state->has_expected_server_identity_key = false;

  if (password && password[0] != '\0') {
    size_t length = strnlen(password, sizeof(state->crypto_password));
    if (length < 8 || length >= sizeof(state->crypto_password)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Client password must contain 8 to %zu bytes", sizeof(state->crypto_password) - 1);
      return -1;
    }
    SAFE_STRNCPY(state->crypto_password, password, sizeof(state->crypto_password) - 1);
  }

  if (identity_secret_key) {
    uint8_t derived_public_key[crypto_sign_PUBLICKEYBYTES];
    if (crypto_sign_ed25519_sk_to_pk(derived_public_key, identity_secret_key) != 0 ||
        sodium_memcmp(derived_public_key, identity_secret_key + crypto_sign_SEEDBYTES, crypto_sign_PUBLICKEYBYTES) !=
            0) {
      sodium_memzero(derived_public_key, sizeof(derived_public_key));
      SET_ERRNO(ERROR_INVALID_PARAM, "Client identity secret key is invalid");
      return -1;
    }
    memcpy(state->client_identity_key, identity_secret_key, sizeof(state->client_identity_key));
    sodium_memzero(derived_public_key, sizeof(derived_public_key));
    state->has_client_identity_key = true;
  }

  if (expected_server_public_key) {
    memcpy(state->expected_server_identity_key, expected_server_public_key, sizeof(state->expected_server_identity_key));
    state->has_expected_server_identity_key = true;
  }
  return 0;
}

/** Parse an OpenSSH Ed25519 private-key file through the shared SSH parser. */
int client_parse_ssh_private_key(const char *key_path, uint8_t *secret_key_out) {
  if (!key_path || !secret_key_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid SSH private key input or output");
    return -1;
  }
#ifdef EMSCRIPTEN_BUILD
  static const char wasm_key_prefix[] = "/tmp/ascii-chat-key-";
  if (strncmp(key_path, wasm_key_prefix, sizeof(wasm_key_prefix) - 1) != 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser SSH private key path");
    return -1;
  }
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
int client_parse_ssh_public_key(const char *key_line, uint8_t *public_key_out) {
  if (!key_line || !public_key_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid SSH public key input or output");
    return -1;
  }
  return parse_ssh_ed25519_line(key_line, public_key_out) == ASCIICHAT_OK ? 0 : -1;
}

/** Parse an armored OpenPGP Ed25519 public key through the shared C parser. */
int client_parse_gpg_public_key(const char *armored_key, uint8_t *public_key_out) {
  if (!armored_key || !public_key_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid OpenPGP public key input or output");
    return -1;
  }
  return openpgp_parse_armored_pubkey(armored_key, public_key_out) == ASCIICHAT_OK ? 0 : -1;
}

/** Parse a binary OpenPGP Ed25519 public-key packet stream. */
int client_parse_gpg_public_key_binary(const uint8_t *key_data, size_t key_data_len, uint8_t *public_key_out) {
  if (!key_data || key_data_len == 0 || !public_key_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid binary OpenPGP public key input or output");
    return -1;
  }
  return openpgp_parse_binary_pubkey(key_data, key_data_len, public_key_out) == ASCIICHAT_OK ? 0 : -1;
}

/** Parse an unencrypted armored OpenPGP Ed25519 private key through the shared C parser. */
int client_parse_gpg_private_key(const char *armored_key, uint8_t *secret_key_out) {
  if (!armored_key || !secret_key_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid OpenPGP private key input or output");
    return -1;
  }
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
int client_parse_gpg_private_key_binary(const uint8_t *key_data, size_t key_data_len, uint8_t *secret_key_out) {
  if (!key_data || key_data_len == 0 || !secret_key_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid binary OpenPGP private key input or output");
    return -1;
  }
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
int client_get_crypto_error_message(char *output, size_t output_size) {
  if (!output || output_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid crypto error output buffer");
    return -1;
  }
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
 * Initialize client mode with native defaults.
 * @return 0 on success, -1 on error
 */
int client_init(browser_transport_t *transport) {
  if (!transport)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Cannot initialize client without a browser transport");
  client_state_t *state = client_state(transport);
  if (!state) {
    state = SAFE_CALLOC(1, sizeof(*state), client_state_t *);
    if (!state)
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate browser client state");
    state->crypto_mode = ACIP_CRYPTO_ENCRYPT;
    browser_transport_set_client_state(transport, state);
  }
  // The module, renderers, and allocator outlive individual connections.
  if (state->initialized) {
    browser_transport_set_output_callback(transport, wasm_transport_output, NULL);
    browser_transport_set_state(transport, BROWSER_TRANSPORT_DISCONNECTED);
    return 0;
  }
  char *argv[] = {"client", NULL};
  asciichat_error_t err = options_init(1, argv);
  if (err != ASCIICHAT_OK)
    return -1;

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

  browser_transport_set_output_callback(transport, wasm_transport_output, NULL);
  browser_transport_set_state(transport, BROWSER_TRANSPORT_DISCONNECTED);

  state->initialized = true;

  return 0;
}

void client_cleanup(browser_transport_t *transport) {
  client_state_t *state = client_state(transport);
  if (!state)
    return;
  errno_destroy();

  // Clean up crypto handshake context
  crypto_handshake_destroy(&state->crypto_handshake_ctx);
  memset(&state->crypto_handshake_ctx, 0, sizeof(state->crypto_handshake_ctx));

  state->handshake_complete = false;
  browser_transport_set_state(transport, BROWSER_TRANSPORT_DISCONNECTED);
  sodium_memzero(state->crypto_password, sizeof(state->crypto_password));
  sodium_memzero(state->client_identity_key, sizeof(state->client_identity_key));
  sodium_memzero(state->expected_server_identity_key, sizeof(state->expected_server_identity_key));
  state->has_client_identity_key = false;
  state->has_expected_server_identity_key = false;
  state->initialized = false;
  // Keep module-wide options and buffer domains alive while browser renderers
  // still reference them. Connection cleanup only resets authentication state.
  SAFE_FREE(state);
  browser_transport_set_client_state(transport, NULL);
}

// ============================================================================
// Cryptography API
// ============================================================================

/**
 * Generate client keypair for handshake
 * @return 0 on success, -1 on error
 */
int client_generate_keypair(browser_transport_t *transport) {
  client_state_t *state = client_state(transport);
  if (!state || !state->initialized) {
    SET_ERRNO(ERROR_INVALID_STATE, "Client is not initialized");
    return -1;
  }

  // A replacement handshake must not retain the previous connection's sequence state.
  // Reset handshake context if previously used (reconnection / React Strict Mode remount)
  if (state->crypto_handshake_ctx.state != CRYPTO_HANDSHAKE_DISABLED) {
    crypto_handshake_destroy(&state->crypto_handshake_ctx);
    memset(&state->crypto_handshake_ctx, 0, sizeof(state->crypto_handshake_ctx));
    state->handshake_complete = false;
  }

  // Initialize crypto handshake context
  asciichat_error_t result = crypto_handshake_init("wasm-client", &state->crypto_handshake_ctx, false /* is_server */);
  if (result != ASCIICHAT_OK) {
    return -1;
  }
  if (apply_client_crypto_options(transport) != 0) {
    crypto_handshake_destroy(&state->crypto_handshake_ctx);
    memset(&state->crypto_handshake_ctx, 0, sizeof(state->crypto_handshake_ctx));
    return -1;
  }

  browser_transport_set_state(transport, BROWSER_TRANSPORT_DISCONNECTED);
  return 0;
}

/**
 * Set server address for known_hosts verification
 * @param server_host Server hostname or IP address
 * @param server_port Server port number
 * @return 0 on success, -1 on error
 */
int client_set_server_address(browser_transport_t *transport, const char *server_host, int server_port) {
  client_state_t *state = client_state(transport);
  if (!state || !state->initialized) {
    SET_ERRNO(ERROR_INVALID_STATE, "Client is not initialized");
    return -1;
  }

  if (!server_host || !server_host[0] || server_port <= 0 || server_port > 65535) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client server address or port");
    return -1;
  }

  // Set server IP and port in handshake context
  SAFE_STRNCPY(state->crypto_handshake_ctx.server_ip, server_host, sizeof(state->crypto_handshake_ctx.server_ip));
  state->crypto_handshake_ctx.server_port = server_port;

  return 0;
}

const uint8_t *client_public_key_bytes(browser_transport_t *transport) {
  client_state_t *state = client_state(transport);
  if (!state || state->crypto_handshake_ctx.state == CRYPTO_HANDSHAKE_DISABLED) {
    SET_ERRNO(ERROR_INVALID_STATE, "Client handshake is not initialized");
    return NULL;
  }
  return state->crypto_handshake_ctx.crypto_ctx.public_key;
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
int client_encrypt_packet(browser_transport_t *transport, const uint8_t *plaintext, size_t plaintext_len, uint8_t *ciphertext, size_t ciphertext_size,
                          size_t *out_len) {
  client_state_t *state = client_state(transport);
  if (!state || !state->handshake_complete) {
    SET_ERRNO(ERROR_INVALID_STATE, "Client handshake is not complete");
    return -1;
  }

  const size_t overhead = 32 + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
  if (!plaintext || !ciphertext || !out_len || plaintext_len > SIZE_MAX - overhead ||
      ciphertext_size < plaintext_len + overhead) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client encryption buffers or lengths");
    return -1;
  }
  uint8_t *wire = NULL;
  size_t length = 0;
  packet_header_t header;
  if (plaintext_len < sizeof(header)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Client plaintext packet is shorter than its header");
    return -1;
  }
  memcpy(&header, plaintext, sizeof(header));
  acip_traffic_class_t channel = acip_packet_traffic_class(NET_TO_HOST_U16(header.type));
  asciichat_error_t result = acip_security_seal(&state->crypto_handshake_ctx.crypto_ctx, channel, ACIP_DELIVERY_RELIABLE,
                                                plaintext, plaintext_len, &wire, &length);
  if (result == ASCIICHAT_OK) {
    *out_len = length - sizeof(packet_header_t);
    memcpy(ciphertext, wire + sizeof(packet_header_t), *out_len);
  }
  buffer_release(wire);
  return result == ASCIICHAT_OK ? 0 : -1;
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
int client_decrypt_packet(browser_transport_t *transport, const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *plaintext, size_t plaintext_size,
                          size_t *out_len) {
  client_state_t *state = client_state(transport);
  if (!state || !state->handshake_complete) {
    SET_ERRNO(ERROR_INVALID_STATE, "Client handshake is not complete");
    return -1;
  }

  if (!ciphertext || !plaintext || !out_len || plaintext_size < ciphertext_len) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client decryption buffers or lengths");
    return -1;
  }
  *out_len = 0;
  uint8_t *plain = NULL;
  size_t length = 0;
  uint64_t sequence;
  bool consumed = false;
  asciichat_error_t result =
      acip_security_decrypt(&state->crypto_handshake_ctx.crypto_ctx, CRYPTO_RECORD_ANY_CHANNEL, ACIP_DELIVERY_RELIABLE,
                            ciphertext, ciphertext_len, &plain, &length, &sequence, &consumed);
  if (result == ASCIICHAT_OK && !consumed) {
    memcpy(plaintext, plain, length);
    *out_len = length;
  }
  buffer_release(plain);
  return result == ASCIICHAT_OK ? 0 : -1;
}

// ============================================================================
// Connection State API
// ============================================================================

/**
 * Get current connection state
 * @return Connection state enum value
 */
int client_get_connection_state(const browser_transport_t *transport) {
  return transport ? (int)browser_transport_state(transport) : BROWSER_TRANSPORT_DISCONNECTED;
}

asciichat_error_t client_acds_handshake_begin(browser_transport_t *transport, uint8_t mode) {
  client_state_t *state = client_state(transport);
  if (!state)
    return SET_ERRNO(ERROR_INVALID_STATE, "Client transport is not initialized");
  state->crypto_handshake_ctx.crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(mode);
  return acds_handshake_begin(browser_transport_base(transport), mode);
}
asciichat_error_t client_acds_handshake_receive(browser_transport_t *transport, uint32_t type, const void *payload, size_t length) {
  client_state_t *state = client_state(transport);
  if (!state)
    return SET_ERRNO(ERROR_INVALID_STATE, "Client transport is not initialized");
  asciichat_error_t result = acds_handshake_receive(&state->crypto_handshake_ctx, browser_transport_base(transport), type, payload, length);
  if (result != ASCIICHAT_OK) {
    browser_transport_set_state(transport, BROWSER_TRANSPORT_ERROR);
    return result;
  }
  if (state->crypto_handshake_ctx.state == CRYPTO_HANDSHAKE_READY) {
    state->handshake_complete = true;
    browser_transport_base(transport)->crypto_ctx = &state->crypto_handshake_ctx.crypto_ctx;
    browser_transport_set_state(transport, BROWSER_TRANSPORT_CONNECTED);
  } else {
    browser_transport_set_state(transport, BROWSER_TRANSPORT_HANDSHAKE);
  }
  return ASCIICHAT_OK;
}

/** Resolve browser inputs through the same policy as native and mobile clients. */
int client_resolve_crypto_policy(browser_transport_t *transport, bool secure, int encryption) {
  client_state_t *state = client_state(transport);
  if (!state)
    return SET_ERRNO(ERROR_INVALID_STATE, "Client transport is not initialized");
  crypto_policy_t policy = {
      .transport_secure = secure,
      .encrypt = encryption == 1,
      .no_encrypt = encryption == 0,
      .has_password = state->crypto_password[0] != '\0',
      .has_identity = state->has_client_identity_key,
      .has_verification_key = state->has_expected_server_identity_key,
  };
  uint8_t mode;
  if (crypto_policy_resolve(&policy, &mode) != ASCIICHAT_OK)
    return -1;
  state->crypto_mode = mode;
  return mode;
}

asciichat_error_t client_handshake_begin(browser_transport_t *transport) {
  client_state_t *state = client_state(transport);
  if (!state)
    return SET_ERRNO(ERROR_INVALID_STATE, "Client transport is not initialized");
  browser_transport_set_state(transport, BROWSER_TRANSPORT_CONNECTING);
  state->version_received = false;
  state->crypto_handshake_ctx.crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(state->crypto_mode);
  return acds_handshake_begin(browser_transport_base(transport), state->crypto_mode);
}

asciichat_error_t client_handshake_receive(browser_transport_t *transport, uint32_t type, const void *payload, size_t length) {
  client_state_t *state = client_state(transport);
  if (!state)
    return SET_ERRNO(ERROR_INVALID_STATE, "Client transport is not initialized");
  if (!state->version_received && type != PACKET_TYPE_PROTOCOL_VERSION)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected server protocol version first");
  if (type == PACKET_TYPE_PROTOCOL_VERSION) {
    if (state->version_received)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Duplicate server protocol version");
    if (!payload || length != sizeof(protocol_version_packet_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid server protocol version");
    const protocol_version_packet_t *version = payload;
    if (NET_TO_HOST_U16(version->protocol_version) != 1 || version->supports_encryption != state->crypto_mode)
      return SET_ERRNO(ERROR_CRYPTO, "Server rejected requested security policy");
    state->version_received = true;
    if (state->crypto_mode == ACIP_CRYPTO_NONE) {
      browser_transport_set_state(transport, BROWSER_TRANSPORT_CONNECTED);
      return ASCIICHAT_OK;
    }
    crypto_capabilities_packet_t caps = {0};
    caps.supported_kex_algorithms = HOST_TO_NET_U16(KEX_ALGO_X25519);
    caps.supported_auth_algorithms = HOST_TO_NET_U16(AUTH_ALGO_ED25519 | AUTH_ALGO_NONE);
    caps.supported_cipher_algorithms = HOST_TO_NET_U16(CIPHER_ALGO_XSALSA20_POLY1305 | CIPHER_ALGO_NONE);
    caps.preferred_kex = KEX_ALGO_X25519;
    caps.preferred_auth = ACIP_CRYPTO_HAS_AUTH(state->crypto_mode) ? AUTH_ALGO_ED25519 : AUTH_ALGO_NONE;
    caps.preferred_cipher = ACIP_CRYPTO_HAS_ENCRYPT(state->crypto_mode) ? CIPHER_ALGO_XSALSA20_POLY1305 : CIPHER_ALGO_NONE;
    caps.requires_verification = state->has_expected_server_identity_key;
    asciichat_error_t result =
        packet_send_via_transport(browser_transport_base(transport), PACKET_TYPE_CRYPTO_CAPABILITIES, &caps, sizeof(caps), 0);
    if (result != ASCIICHAT_OK || !state->has_expected_server_identity_key)
      return result;
    return packet_send_via_transport(browser_transport_base(transport), PACKET_TYPE_CRYPTO_CLIENT_HELLO, state->expected_server_identity_key,
                                     sizeof(state->expected_server_identity_key), 0);
  }
  if (type == PACKET_TYPE_CRYPTO_PARAMETERS) {
    if (!payload || length != sizeof(crypto_parameters_packet_t) ||
        (((const crypto_parameters_packet_t *)payload)->selected_cipher != CIPHER_ALGO_NONE) !=
            ACIP_CRYPTO_HAS_ENCRYPT(state->crypto_mode))
      return SET_ERRNO(ERROR_CRYPTO, "Server changed requested payload encryption");
  }
  return client_acds_handshake_receive(transport, type, payload, length);
}

/* Browser transport marshals framing, then invokes the SAME network content
 * contract as native before any terminal parser or recording receives bytes. */
asciichat_error_t client_validate_frame(browser_transport_t *transport, const char *text, size_t length, uint32_t width, uint32_t height,
                                        uint32_t checksum, uint32_t flags, const void *metadata,
                                        size_t metadata_length) {
  client_state_t *state = client_state(transport);
  if (!state)
    return SET_ERRNO(ERROR_INVALID_STATE, "Browser client transport is not initialized");
  if (flags & ~(FRAME_FLAG_RENDER_CONTRACT | FRAME_FLAG_IS_COMPRESSED | 1u))
    return SET_ERRNO(ERROR_INVALID_FRAME, "Unknown browser frame flags");
  acip_render_settings_t settings;
  uint32_t effects = 0;
  bool contract = (flags & FRAME_FLAG_RENDER_CONTRACT) != 0;
  if (contract) {
    size_t consumed;
    asciichat_error_t decoded = frame_render_contract_decode(metadata, metadata_length, &settings, &effects, &consumed);
    if (decoded != ASCIICHAT_OK)
      return decoded;
    if (settings.revision && settings.revision == state->remote_render_settings.revision &&
        state->remote_render_settings.palette[0] && state->remote_render_settings.caps.utf8_support &&
        strcmp(settings.palette, state->remote_render_settings.palette) != 0)
      return SET_ERRNO(ERROR_INVALID_FRAME, "Browser frame palette contradicts acknowledged revision");
    if (consumed != metadata_length)
      return SET_ERRNO(ERROR_INVALID_FRAME, "Trailing browser frame contract bytes");
  } else if (metadata_length) {
    return SET_ERRNO(ERROR_INVALID_FRAME, "Unexpected browser frame contract");
  }
  if (!text || !length || length > FRAME_RENDER_MAX_BYTES || asciichat_crc32(text, length) != checksum)
    return SET_ERRNO(ERROR_INVALID_FRAME, "Browser frame length/checksum mismatch");
  return session_frame_validate_contract(text, length, width, height, contract ? &settings : NULL, effects);
}

asciichat_error_t client_decode_frame_data(const void *encoded, size_t encoded_length, void *output,
                                           size_t output_length) {
  if (!encoded || !output || !encoded_length || encoded_length > FRAME_RENDER_MAX_BYTES || !output_length ||
      output_length > FRAME_RENDER_MAX_BYTES)
    return SET_ERRNO(ERROR_INVALID_FRAME, "Invalid browser frame decompression bounds");
  return decompress_data_exact(encoded, encoded_length, output, output_length);
}

