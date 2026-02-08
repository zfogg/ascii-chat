/**
 * @file client_wasm.c
 * @brief WASM entry point for ascii-chat client mode
 */

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

// Logging macros for debug
#define WASM_LOG(msg) EM_ASM({ console.log('[C] ' + UTF8ToString($0)); }, msg)
#define WASM_LOG_INT(msg, val) EM_ASM({ console.log('[C] ' + UTF8ToString($0) + ': ' + $1); }, msg, val)
#define WASM_ERROR(msg) EM_ASM({ console.error('[C] ' + UTF8ToString($0)); }, msg)

#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/packet_parsing.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/ansi_fast.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/magic.h>
#include <opus.h>

// Global state for client session
static crypto_context_t *g_crypto_ctx = NULL;
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

  if (g_initialized) {
    WASM_ERROR("Client already initialized");
    return -1;
  }

  // Initialize platform layer
  WASM_LOG("Calling platform_init...");
  asciichat_error_t err = platform_init();
  if (err != ASCIICHAT_OK) {
    WASM_ERROR("platform_init FAILED");
    return -1;
  }
  WASM_LOG("platform_init OK");

  // Parse arguments
  WASM_LOG("Parsing arguments...");
  char *args_copy = strdup(args_json);
  if (!args_copy) {
    WASM_ERROR("strdup FAILED");
    return -1;
  }

  // Count arguments
  int argc = 0;
  char *argv[64] = {NULL}; // Max 64 arguments
  char *token = strtok(args_copy, " ");
  while (token != NULL && argc < 63) {
    argv[argc++] = token;
    token = strtok(NULL, " ");
  }
  argv[argc] = NULL;
  WASM_LOG_INT("Parsed arguments, argc", argc);

  // Initialize options (sets up RCU, defaults, etc.)
  WASM_LOG("Calling options_init...");
  err = options_init(argc, argv);
  free(args_copy);

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
  if (g_crypto_ctx) {
    crypto_destroy(g_crypto_ctx);
    g_crypto_ctx = NULL;
  }
  g_handshake_complete = false;
  g_connection_state = CONNECTION_STATE_DISCONNECTED;
  g_initialized = false;
  options_state_destroy();
  platform_destroy();
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
    WASM_ERROR("Client not initialized");
    return -1;
  }

  // Create crypto context (this generates keypair automatically)
  if (g_crypto_ctx) {
    crypto_destroy(g_crypto_ctx);
    g_crypto_ctx = NULL;
  }

  // Allocate crypto context
  g_crypto_ctx = SAFE_CALLOC(1, sizeof(crypto_context_t), crypto_context_t *);
  if (!g_crypto_ctx) {
    WASM_ERROR("Failed to allocate crypto context");
    return -1;
  }

  // Initialize crypto context (generates keypair)
  crypto_result_t result = crypto_init(g_crypto_ctx);
  if (result != CRYPTO_OK) {
    WASM_ERROR("Failed to initialize crypto context");
    SAFE_FREE(g_crypto_ctx);
    g_crypto_ctx = NULL;
    return -1;
  }

  WASM_LOG("Keypair generated successfully");
  return 0;
}

/**
 * Get client public key as hex string
 * @return Allocated hex string (caller must free with client_free_string), or NULL on error
 */
EMSCRIPTEN_KEEPALIVE
char *client_get_public_key_hex(void) {
  if (!g_crypto_ctx) {
    WASM_ERROR("No crypto context (call client_generate_keypair first)");
    return NULL;
  }

  // Allocate buffer for public key bytes
  uint8_t pubkey[32];
  crypto_result_t result = crypto_get_public_key(g_crypto_ctx, pubkey);
  if (result != CRYPTO_OK) {
    WASM_ERROR("Failed to get public key from crypto context");
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
 * Set server public key from hex string
 * @param server_pubkey_hex Hex-encoded server public key (64 chars)
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_set_server_public_key(const char *server_pubkey_hex) {
  if (!g_crypto_ctx) {
    WASM_ERROR("No crypto context");
    return -1;
  }

  if (!server_pubkey_hex || strlen(server_pubkey_hex) != 64) {
    WASM_ERROR("Invalid server public key hex string");
    return -1;
  }

  // Convert hex to bytes
  uint8_t server_pubkey[32];
  for (int i = 0; i < 32; i++) {
    char byte_str[3] = {server_pubkey_hex[i * 2], server_pubkey_hex[i * 2 + 1], '\0'};
    server_pubkey[i] = (uint8_t)strtol(byte_str, NULL, 16);
  }

  // Set peer's public key and compute shared secret
  crypto_result_t result = crypto_set_peer_public_key(g_crypto_ctx, server_pubkey);
  if (result != CRYPTO_OK) {
    WASM_ERROR("Failed to set server public key");
    return -1;
  }

  WASM_LOG("Server public key set successfully");
  return 0;
}

/**
 * Perform client-side handshake (compute shared secret)
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int client_perform_handshake(void) {
  if (!g_crypto_ctx) {
    WASM_ERROR("No crypto context");
    return -1;
  }

  // The shared secret was computed when we called crypto_set_remote_key
  // Just verify the handshake is complete
  if (!crypto_is_ready(g_crypto_ctx)) {
    WASM_ERROR("Crypto context not ready after handshake");
    return -1;
  }

  g_handshake_complete = true;
  g_connection_state = CONNECTION_STATE_CONNECTED;
  WASM_LOG("Handshake complete, session encrypted");
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
  if (!g_handshake_complete || !g_crypto_ctx) {
    WASM_ERROR("Encryption requires completed handshake");
    return -1;
  }

  // Encrypt the packet
  size_t ciphertext_len = 0;
  crypto_result_t result =
      crypto_encrypt(g_crypto_ctx, plaintext, plaintext_len, ciphertext, ciphertext_size, &ciphertext_len);
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
  if (!g_handshake_complete || !g_crypto_ctx) {
    WASM_ERROR("Decryption requires completed handshake");
    return -1;
  }

  // Decrypt the packet
  size_t plaintext_len = 0;
  crypto_result_t result =
      crypto_decrypt(g_crypto_ctx, ciphertext, ciphertext_len, plaintext, plaintext_size, &plaintext_len);
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

  // Validate magic number
  if (header->magic != PACKET_MAGIC) {
    WASM_ERROR("Invalid packet magic number");
    return NULL;
  }

  // Build JSON response with packet metadata
  char *json = SAFE_MALLOC(1024, char *);
  if (!json) {
    WASM_ERROR("Failed to allocate JSON buffer");
    return NULL;
  }

  snprintf(json, 1024, "{\"type\":%d,\"length\":%u,\"client_id\":%u,\"crc32\":%u}", header->type, header->length,
           header->client_id, header->crc32);

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

  // Build packet header
  packet_header_t header = {.magic = PACKET_MAGIC,
                            .type = packet_type,
                            .length = (uint32_t)payload_len,
                            .crc32 = crc,
                            .client_id = client_id};

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
