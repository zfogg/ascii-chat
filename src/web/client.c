/**
 * @file client_wasm.c
 * @brief WASM entry point for ascii-chat client mode
 */

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Use console.error for client logging (playwright captures stderr)
#define WASM_LOG_USE_ERROR
#include "common/wasm_log.h"
#include "common/init.h"

// JavaScript callback for sending complete ACIP packets from WASM to WebSocket
// This will be called by the WASM transport to send complete packets (header + payload)
EM_JS(void, js_send_raw_packet, (const uint8_t *packet_data, size_t packet_len), {
  if (!Module.sendPacketCallback) {
    console.error('[WASM] sendPacketCallback not registered - cannot send packet');
    return;
  }

  // Copy complete packet from WASM memory to JavaScript
  const packetArray = new Uint8Array(HEAPU8.buffer, packet_data, packet_len);
  const packetCopy = new Uint8Array(packetArray);

  var pktType = packetCopy.length >= 10 ? ((packetCopy[8] << 8) | packetCopy[9]) : -1;
  console.error('[WASM->JS] Sending raw packet:', packetCopy.length, 'bytes, type=0x' + pktType.toString(16));

  // Send as raw binary packet via WebSocket
  Module.sendPacketCallback(packetCopy);
});

#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/packet_parsing.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/ansi_fast.h>
#include <ascii-chat/common.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/magic.h>
#include <opus.h>

// ============================================================================
// WASM Transport Implementation
// ============================================================================

/**
 * WASM transport that forwards complete ACIP packets to JavaScript
 */
static asciichat_error_t wasm_transport_send(acip_transport_t *transport, const void *data, size_t len) {
  WASM_LOG("wasm_transport_send called");
  WASM_LOG_INT("  packet length", (int)len);

  // Forward complete packet (header + payload) to JavaScript WebSocket bridge
  js_send_raw_packet((const uint8_t *)data, len);

  WASM_LOG("wasm_transport_send: packet sent to JS");
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
  WASM_LOG("client_init_with_args: START");

  // Parse space-separated arguments
  WASM_LOG("Parsing arguments...");
  char *args_copy = NULL;
  char *argv[64] = {NULL};
  int argc = wasm_parse_args(args_json, argv, 64, &args_copy);
  if (argc < 0) {
    WASM_ERROR("strdup FAILED");
    return -1;
  }
  WASM_LOG_INT("Parsed arguments, argc", argc);

  // Initialize options (sets up RCU, defaults, etc.)
  WASM_LOG("Calling options_init...");
  asciichat_error_t err = options_init(argc, argv);
  free(args_copy);

  if (g_initialized) {
    WASM_ERROR("Client already initialized");
    return -1;
  }

  // Initialize platform layer
  WASM_LOG("Calling platform_init...");
  err = platform_init();
  if (err != ASCIICHAT_OK) {
    WASM_ERROR("platform_init FAILED");
    return -1;
  }
  WASM_LOG("platform_init OK");

  // Initialize logging to stderr (console.error in browser)
  WASM_LOG("Calling log_init...");
  log_init(NULL, LOG_DEBUG, true, false);
  WASM_LOG("log_init OK");
  log_info("WASM client initialized via logging system");

  if (err != ASCIICHAT_OK) {
    WASM_LOG_INT("options_init FAILED", err);
    return -1;
  }
  WASM_LOG("options_init OK");

  // Initialize ANSI color code generation
  WASM_LOG("Calling ansi_fast_init...");
  ansi_fast_init();
  WASM_LOG("ansi_fast_init OK");

  g_initialized = true;
  g_connection_state = CONNECTION_STATE_DISCONNECTED;

  WASM_LOG("client_init_with_args: COMPLETE");
  return 0;
}

EMSCRIPTEN_KEEPALIVE
void client_cleanup(void) {
  WASM_LOG("=== client_cleanup CALLED ===");
  WASM_LOG_INT("  g_initialized", g_initialized);
  WASM_LOG_INT("  g_connection_state", g_connection_state);
  WASM_LOG_INT("  g_crypto_handshake_ctx.state", g_crypto_handshake_ctx.state);

  // Clean up crypto handshake context
  crypto_handshake_destroy(&g_crypto_handshake_ctx);
  memset(&g_crypto_handshake_ctx, 0, sizeof(g_crypto_handshake_ctx));

  g_handshake_complete = false;
  g_connection_state = CONNECTION_STATE_DISCONNECTED;
  g_initialized = false;
  options_state_destroy();
  platform_destroy();

  WASM_LOG("=== client_cleanup COMPLETE ===");
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
  WASM_LOG("=== client_generate_keypair CALLED ===");
  WASM_LOG_INT("  g_initialized", g_initialized);
  WASM_LOG_INT("  g_crypto_handshake_ctx.state BEFORE", g_crypto_handshake_ctx.state);

  if (!g_initialized) {
    WASM_ERROR("Client not initialized");
    return -1;
  }

  // Reset handshake context if previously used (reconnection / React Strict Mode remount)
  if (g_crypto_handshake_ctx.state != CRYPTO_HANDSHAKE_DISABLED) {
    WASM_LOG("Destroying previous handshake context before re-init");
    WASM_LOG_INT("  State before destroy", g_crypto_handshake_ctx.state);
    crypto_handshake_destroy(&g_crypto_handshake_ctx);
    WASM_LOG_INT("  State after destroy", g_crypto_handshake_ctx.state);
    memset(&g_crypto_handshake_ctx, 0, sizeof(g_crypto_handshake_ctx));
    WASM_LOG_INT("  State after memset", g_crypto_handshake_ctx.state);
    g_handshake_complete = false;
  }

  // Initialize crypto handshake context
  WASM_LOG("Calling crypto_handshake_init...");
  WASM_LOG_INT("  State before init", g_crypto_handshake_ctx.state);
  asciichat_error_t result = crypto_handshake_init(&g_crypto_handshake_ctx, false /* is_server */);
  if (result != ASCIICHAT_OK) {
    WASM_LOG_INT("crypto_handshake_init FAILED, result", result);
    WASM_LOG_INT("  State after failed init", g_crypto_handshake_ctx.state);
    return -1;
  }

  WASM_LOG("Keypair generated successfully");
  WASM_LOG_INT("  g_crypto_handshake_ctx.state AFTER init", g_crypto_handshake_ctx.state);
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
    WASM_ERROR("Client not initialized");
    return -1;
  }

  if (!server_host || server_port <= 0 || server_port > 65535) {
    WASM_ERROR("Invalid server address parameters");
    return -1;
  }

  // Set server IP and port in handshake context
  SAFE_STRNCPY(g_crypto_handshake_ctx.server_ip, server_host, sizeof(g_crypto_handshake_ctx.server_ip));
  g_crypto_handshake_ctx.server_port = server_port;

  WASM_LOG("Server address set");
  return 0;
}

/**
 * Get client public key as hex string
 * @return Allocated hex string (caller must free with client_free_string), or NULL on error
 */
EMSCRIPTEN_KEEPALIVE
char *client_get_public_key_hex(void) {
  if (g_crypto_handshake_ctx.state == CRYPTO_HANDSHAKE_DISABLED) {
    WASM_ERROR("No crypto context (call client_generate_keypair first)");
    return NULL;
  }

  // Get public key from handshake context
  const uint8_t *pubkey = g_crypto_handshake_ctx.crypto_ctx.public_key;
  if (!pubkey) {
    WASM_ERROR("Public key not available in crypto context");
    return NULL;
  }

  // Allocate buffer for hex string (32 bytes = 64 hex chars + null terminator)
  char *hex_buffer = SAFE_MALLOC(65, char *);
  if (!hex_buffer) {
    WASM_ERROR("Failed to allocate hex buffer");
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
  WASM_LOG("=== client_handle_key_exchange_init CALLED ===");
  WASM_LOG_INT("  packet_len", (int)packet_len);
  WASM_LOG_INT("  g_crypto_handshake_ctx.state BEFORE", g_crypto_handshake_ctx.state);

  // Safety check: if handshake context is not in INIT state, reinitialize it
  // This handles cases where previous handshakes weren't properly cleaned up
  if (g_crypto_handshake_ctx.state != CRYPTO_HANDSHAKE_INIT) {
    WASM_LOG("Handshake context not in INIT state, reinitializing...");
    WASM_LOG_INT("  Previous state", g_crypto_handshake_ctx.state);

    // Destroy and reset
    if (g_crypto_handshake_ctx.state != CRYPTO_HANDSHAKE_DISABLED) {
      crypto_handshake_destroy(&g_crypto_handshake_ctx);
    }
    memset(&g_crypto_handshake_ctx, 0, sizeof(g_crypto_handshake_ctx));

    // Reinitialize to INIT state
    asciichat_error_t init_result = crypto_handshake_init(&g_crypto_handshake_ctx, false);
    if (init_result != ASCIICHAT_OK) {
      WASM_ERROR("Failed to reinitialize crypto handshake context");
      WASM_LOG_INT("  init result", init_result);
      return -1;
    }
    WASM_LOG("Crypto handshake context reinitialized");
  }

  if (!packet || packet_len == 0) {
    WASM_ERROR("Invalid packet data");
    return -1;
  }

  // Extract packet type and payload
  if (packet_len < sizeof(packet_header_t)) {
    WASM_ERROR("Packet too small for header");
    return -1;
  }

  const packet_header_t *header = (const packet_header_t *)packet;
  packet_type_t packet_type = ntohs(header->type);
  const uint8_t *payload_src = packet + sizeof(packet_header_t);
  size_t payload_len = packet_len - sizeof(packet_header_t);

  WASM_LOG_INT("  packet_type", packet_type);
  WASM_LOG_INT("  payload_len", (int)payload_len);

  // Allocate payload copy from buffer pool (crypto function takes ownership and frees it)
  // The raw packet pointer from JS cannot be passed directly because the crypto
  // handshake function calls buffer_pool_free() on the payload when done.
  uint8_t *payload = NULL;
  if (payload_len > 0) {
    payload = buffer_pool_alloc(NULL, payload_len);
    if (!payload) {
      WASM_ERROR("Failed to allocate payload buffer");
      g_connection_state = CONNECTION_STATE_ERROR;
      return -1;
    }
    memcpy(payload, payload_src, payload_len);
  }

  // Process key exchange using transport-abstracted handshake
  WASM_LOG("Calling crypto_handshake_client_key_exchange...");

  asciichat_error_t result = crypto_handshake_client_key_exchange(&g_crypto_handshake_ctx, &g_wasm_transport,
                                                                  packet_type, payload, payload_len);

  WASM_LOG_INT("  handshake result", result);
  WASM_LOG_INT("  g_crypto_handshake_ctx.state AFTER", g_crypto_handshake_ctx.state);

  if (result != ASCIICHAT_OK) {
    WASM_ERROR("Failed to process KEY_EXCHANGE_INIT");
    g_connection_state = CONNECTION_STATE_ERROR;
    return -1;
  }

  g_connection_state = CONNECTION_STATE_HANDSHAKE;
  WASM_LOG("=== KEY_EXCHANGE_INIT processed successfully ===");
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
  WASM_LOG("=== client_handle_auth_challenge CALLED ===");
  WASM_LOG_INT("  packet_len", (int)packet_len);
  WASM_LOG_INT("  g_crypto_handshake_ctx.state", g_crypto_handshake_ctx.state);

  if (!packet || packet_len == 0) {
    WASM_ERROR("Invalid packet data");
    return -1;
  }

  // Extract packet type and payload
  if (packet_len < sizeof(packet_header_t)) {
    WASM_ERROR("Packet too small for header");
    return -1;
  }

  const packet_header_t *header = (const packet_header_t *)packet;
  packet_type_t packet_type = ntohs(header->type);
  const uint8_t *payload_src = packet + sizeof(packet_header_t);
  size_t payload_len = packet_len - sizeof(packet_header_t);

  WASM_LOG_INT("  packet_type", packet_type);
  WASM_LOG_INT("  payload_len", (int)payload_len);

  // Allocate payload copy from buffer pool (crypto function takes ownership and frees it)
  uint8_t *payload = NULL;
  if (payload_len > 0) {
    payload = buffer_pool_alloc(NULL, payload_len);
    if (!payload) {
      WASM_ERROR("Failed to allocate payload buffer");
      g_connection_state = CONNECTION_STATE_ERROR;
      return -1;
    }
    memcpy(payload, payload_src, payload_len);
  }

  // Process auth challenge
  asciichat_error_t result = crypto_handshake_client_auth_response(&g_crypto_handshake_ctx, &g_wasm_transport,
                                                                   packet_type, payload, payload_len);

  WASM_LOG_INT("  auth_response result", result);

  if (result != ASCIICHAT_OK) {
    WASM_ERROR("Failed to process AUTH_CHALLENGE");
    g_connection_state = CONNECTION_STATE_ERROR;
    return -1;
  }

  WASM_LOG("=== AUTH_CHALLENGE processed successfully ===");
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
  WASM_LOG("=== client_handle_handshake_complete CALLED ===");
  WASM_LOG_INT("  packet_len", (int)packet_len);
  WASM_LOG_INT("  g_crypto_handshake_ctx.state", g_crypto_handshake_ctx.state);

  if (!packet || packet_len == 0) {
    WASM_ERROR("Invalid packet data");
    return -1;
  }

  // Extract packet type and payload
  if (packet_len < sizeof(packet_header_t)) {
    WASM_ERROR("Packet too small for header");
    return -1;
  }

  const packet_header_t *header = (const packet_header_t *)packet;
  packet_type_t packet_type = ntohs(header->type);
  const uint8_t *payload_src = packet + sizeof(packet_header_t);
  size_t payload_len = packet_len - sizeof(packet_header_t);

  WASM_LOG_INT("  packet_type", packet_type);

  // Allocate payload copy from buffer pool (crypto function takes ownership and frees it)
  uint8_t *payload = NULL;
  if (payload_len > 0) {
    payload = buffer_pool_alloc(NULL, payload_len);
    if (!payload) {
      WASM_ERROR("Failed to allocate payload buffer");
      g_connection_state = CONNECTION_STATE_ERROR;
      return -1;
    }
    memcpy(payload, payload_src, payload_len);
  }

  // Complete handshake (takes ownership of payload and will free it)
  asciichat_error_t result =
      crypto_handshake_client_complete(&g_crypto_handshake_ctx, &g_wasm_transport, packet_type, payload, payload_len);

  WASM_LOG_INT("  handshake_complete result", result);

  if (result != ASCIICHAT_OK) {
    WASM_ERROR("Failed to complete handshake");
    g_connection_state = CONNECTION_STATE_ERROR;
    return -1;
  }

  g_handshake_complete = true;
  g_connection_state = CONNECTION_STATE_CONNECTED;
  WASM_LOG("=== HANDSHAKE COMPLETE - session encrypted ===");
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
    WASM_ERROR("Encryption requires completed handshake");
    return -1;
  }

  // Encrypt the packet using handshake context's crypto context
  size_t ciphertext_len = 0;
  crypto_result_t result = crypto_encrypt(&g_crypto_handshake_ctx.crypto_ctx, plaintext, plaintext_len, ciphertext,
                                          ciphertext_size, &ciphertext_len);
  if (result != CRYPTO_OK) {
    WASM_ERROR("Encryption failed");
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
    WASM_ERROR("Decryption requires completed handshake");
    return -1;
  }

  // Decrypt the packet using handshake context's crypto context
  size_t plaintext_len = 0;
  crypto_result_t result = crypto_decrypt(&g_crypto_handshake_ctx.crypto_ctx, ciphertext, ciphertext_len, plaintext,
                                          plaintext_size, &plaintext_len);
  if (result != CRYPTO_OK) {
    WASM_ERROR("Decryption failed");
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
    WASM_ERROR("Invalid packet data");
    return NULL;
  }

  // Parse packet header directly from bytes
  const packet_header_t *header = (const packet_header_t *)raw_packet;

  // Validate magic number (convert from network byte order)
  uint64_t magic = NET_TO_HOST_U64(header->magic);
  if (magic != PACKET_MAGIC) {
    WASM_ERROR("Invalid packet magic number");
    return NULL;
  }

  // Convert header fields from network byte order
  uint16_t type = NET_TO_HOST_U16(header->type);
  uint32_t length = NET_TO_HOST_U32(header->length);
  const char *client_id = NET_TO_HOST_U32(header->client_id);
  uint32_t crc32 = NET_TO_HOST_U32(header->crc32);

  // Build JSON response with packet metadata
  char *json = SAFE_MALLOC(1024, char *);
  if (!json) {
    WASM_ERROR("Failed to allocate JSON buffer");
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
    WASM_ERROR("Invalid output parameters");
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
    WASM_ERROR("Client not initialized");
    return -1;
  }

  // This is a placeholder - actual implementation would:
  // 1. Convert RGBA to target format (e.g., compress with libjpeg-turbo)
  // 2. Build frame packet
  // 3. Encrypt if handshake complete
  // 4. Return serialized packet via callback to JS

  WASM_LOG("Video frame processing not yet implemented");
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
    WASM_ERROR("Failed to create Opus encoder");
    return -1;
  }

  // Set bitrate
  opus_encoder_ctl(g_opus_encoder, OPUS_SET_BITRATE(bitrate));

  WASM_LOG("Opus encoder initialized");
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
    WASM_ERROR("Failed to create Opus decoder");
    return -1;
  }

  WASM_LOG("Opus decoder initialized");
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
    WASM_ERROR("Opus encoder not initialized");
    return -1;
  }

  int encoded_bytes = opus_encode(g_opus_encoder, pcm_data, frame_size, opus_data, max_opus_bytes);
  if (encoded_bytes < 0) {
    WASM_ERROR("Opus encoding failed");
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
    WASM_ERROR("Opus decoder not initialized");
    return -1;
  }

  int decoded_samples = opus_decode(g_opus_decoder, opus_data, opus_bytes, pcm_data, frame_size, decode_fec);
  if (decoded_samples < 0) {
    WASM_ERROR("Opus decoding failed");
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
    WASM_LOG("Opus encoder cleaned up");
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
    WASM_LOG("Opus decoder cleaned up");
  }
}
