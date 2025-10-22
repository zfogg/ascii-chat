#pragma once

/**
 * @file network/packet_types.h
 * @brief Packet type definitions and structures
 *
 * This header contains packet type enums, packet structures, and protocol
 * constants used by the network modules.
 */

#include "platform/abstraction.h"
#include <stdbool.h>
#include <stdint.h>

// Pack network protocol structures tightly for wire format
#ifdef _WIN32
#pragma pack(push, 1)
#endif

// =============================================================================
// Network Protocol Constants
// =============================================================================

// Packet size thresholds
#define LARGE_PACKET_THRESHOLD (100 * 1024)       // 100KB threshold for large packets
#define MAX_PACKET_SIZE ((size_t)5 * 1024 * 1024) // 5MB maximum packet size

// Timeout constants (in seconds)
#define BASE_SEND_TIMEOUT 5                   // Base send timeout
#define LARGE_PACKET_EXTRA_TIMEOUT_PER_MB 0.8 // Extra timeout per MB for large packets
#define MIN_CLIENT_TIMEOUT 10                 // Minimum client timeout (server timeout + buffer)
#define MAX_CLIENT_TIMEOUT 60                 // Maximum client timeout

// Audio batching for efficiency
#define AUDIO_BATCH_COUNT 4                                                // Number of audio chunks per batch
#define AUDIO_BATCH_SAMPLES (AUDIO_SAMPLES_PER_PACKET * AUDIO_BATCH_COUNT) // 1024 samples = 23.2ms @ 44.1kHz
#define AUDIO_BATCH_MS 23                                                  // Approximate milliseconds per batch
#define AUDIO_SAMPLES_PER_PACKET 256                                       // Smaller packets for lower latency

/* Packet-based communication protocol */
#define PACKET_MAGIC 0xDEADBEEF

typedef enum {
  // Protocol negotiation
  PACKET_TYPE_PROTOCOL_VERSION = 1, // Protocol version and capabilities negotiation

  // Unified frame packets (header + data in single packet)
  PACKET_TYPE_ASCII_FRAME = 2, // Complete ASCII frame with all metadata
  PACKET_TYPE_IMAGE_FRAME = 3, // Complete RGB image with dimensions

  // Audio and control
  PACKET_TYPE_AUDIO = 4,
  PACKET_TYPE_CLIENT_CAPABILITIES = 5, // Client reports terminal capabilities
  PACKET_TYPE_PING = 6,
  PACKET_TYPE_PONG = 7,

  // Multi-user protocol extensions
  PACKET_TYPE_CLIENT_JOIN = 8,    // Client announces capability to send media
  PACKET_TYPE_CLIENT_LEAVE = 9,   // Clean disconnect notification
  PACKET_TYPE_STREAM_START = 10,  // Client requests to start sending video/audio
  PACKET_TYPE_STREAM_STOP = 11,   // Client stops sending media
  PACKET_TYPE_CLEAR_CONSOLE = 12, // Server tells client to clear console
  PACKET_TYPE_SERVER_STATE = 13,  // Server sends current state to clients

  // Crypto handshake packets (ALWAYS SENT UNENCRYPTED)
  PACKET_TYPE_CRYPTO_CAPABILITIES = 14,       // Client -> Server: Supported crypto algorithms
  PACKET_TYPE_CRYPTO_PARAMETERS = 15,         // Server -> Client: Chosen algorithms + data sizes
  PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT = 16,  // Server -> Client: {server_pubkey[32]}
  PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP = 17,  // Client -> Server: {client_pubkey[32]}
  PACKET_TYPE_CRYPTO_AUTH_CHALLENGE = 18,     // Server -> Client: {nonce[32]}
  PACKET_TYPE_CRYPTO_AUTH_RESPONSE = 19,      // Client -> Server: {HMAC[32]}
  PACKET_TYPE_CRYPTO_AUTH_FAILED = 20,        // Server -> Client: "authentication failed"
  PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP = 21,   // Server -> Client: {HMAC[32]} server proves knowledge of shared secret
  PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE = 22, // Server -> Client: "encryption ready"
  PACKET_TYPE_CRYPTO_NO_ENCRYPTION = 23,      // Client -> Server: "I want to proceed without encryption"
  PACKET_TYPE_ENCRYPTED = 24,                 // Encrypted packet (after handshake)

  // Session rekeying packets (ALWAYS SENT UNENCRYPTED during rekey handshake)
  PACKET_TYPE_CRYPTO_REKEY_REQUEST = 25,  // Initiator -> Responder: {new_ephemeral_pk[32]}
  PACKET_TYPE_CRYPTO_REKEY_RESPONSE = 26, // Responder -> Initiator: {new_ephemeral_pk[32]}
  PACKET_TYPE_CRYPTO_REKEY_COMPLETE = 27, // Initiator -> Responder: Empty (encrypted with NEW key)

  // Audio batching (moved after crypto packets)
  PACKET_TYPE_AUDIO_BATCH = 28, // Batched audio packets for efficiency

  // Message packets
  PACKET_TYPE_SIZE_MESSAGE = 29,  // Terminal size message
  PACKET_TYPE_AUDIO_MESSAGE = 30, // Audio message
  PACKET_TYPE_TEXT_MESSAGE = 31   // Text message
} packet_type_t;

/**
 * Determines if a packet type is a handshake packet that should NEVER be encrypted.
 * Handshake packets (types 14-23) and rekey packets (types 25-27) are sent in plaintext.
 * This includes all crypto negotiation packets but NOT PACKET_TYPE_ENCRYPTED (24).
 *
 * NOTE: PACKET_TYPE_CRYPTO_REKEY_COMPLETE (27) is sent ENCRYPTED with the NEW key,
 *       so it's technically encrypted but not with the current session key.
 *       The packet handling logic must account for this special case.
 *
 * @param type The packet type to check
 * @return true if this is a handshake/rekey packet, false otherwise
 */
static inline bool packet_is_handshake_type(packet_type_t type) {
  // Initial handshake packets (14-23)
  if (type >= PACKET_TYPE_CRYPTO_CAPABILITIES && type <= PACKET_TYPE_CRYPTO_NO_ENCRYPTION) {
    return true;
  }
  // Rekey packets (25-27) - Note: REKEY_COMPLETE is encrypted with new key but still considered handshake
  if (type >= PACKET_TYPE_CRYPTO_REKEY_REQUEST && type <= PACKET_TYPE_CRYPTO_REKEY_COMPLETE) {
    return true;
  }
  return false;
}

typedef struct {
  uint32_t magic;     // PACKET_MAGIC for packet validation
  uint16_t type;      // packet_type_t
  uint32_t length;    // payload length
  uint32_t crc32;     // payload checksum
  uint32_t client_id; // which client this packet is from (0 = server)
} PACKED_ATTR packet_header_t;

// Multi-user protocol structures
#define ASCIICHAT_DEFAULT_DISPLAY_NAME "AsciiChatter"
#define MAX_DISPLAY_NAME_LEN 32
#define MAX_CLIENTS 10

// Size packet for terminal size updates
typedef struct {
  uint32_t width;
  uint32_t height;
} size_packet_t;

typedef struct {
  uint32_t client_id;                      // Unique client identifier
  char display_name[MAX_DISPLAY_NAME_LEN]; // User display name
  uint32_t capabilities;                   // Bitmask: VIDEO_CAPABLE | AUDIO_CAPABLE
} PACKED_ATTR client_info_packet_t;

typedef struct {
  uint32_t client_id;   // Which client this stream is from
  uint32_t stream_type; // VIDEO_STREAM | AUDIO_STREAM
  uint32_t timestamp;   // When frame was captured
} PACKED_ATTR stream_header_t;

typedef struct {
  uint32_t client_count;                     // Number of clients in list
  client_info_packet_t clients[MAX_CLIENTS]; // Client info array
} PACKED_ATTR client_list_packet_t;

// Server state packet - sent to clients when state changes
typedef struct {
  uint32_t connected_client_count; // Number of currently connected clients
  uint32_t active_client_count;    // Number of clients actively sending video
  uint32_t reserved[6];            // Reserved for future use
} PACKED_ATTR server_state_packet_t;

// Authentication failure reasons
typedef enum {
  AUTH_FAIL_PASSWORD_REQUIRED = 0x01,   // Server requires password but client didn't provide one
  AUTH_FAIL_PASSWORD_INCORRECT = 0x02,  // Password verification failed
  AUTH_FAIL_CLIENT_KEY_REQUIRED = 0x04, // Server requires client key but client didn't provide one
  AUTH_FAIL_CLIENT_KEY_REJECTED = 0x08, // Client key not in whitelist
  AUTH_FAIL_SIGNATURE_INVALID = 0x10    // Client signature verification failed
} auth_failure_reason_t;

// Authentication failure packet - sent by server to explain why auth failed
typedef struct {
  uint8_t reason_flags; // Bitmask of auth_failure_reason_t values
  uint8_t reserved[7];  // Reserved for future use
} PACKED_ATTR auth_failure_packet_t;

// ============================================================================
// Protocol Negotiation Packets
// ============================================================================

// Compression algorithm flags
#define COMPRESS_ALGO_NONE 0x00 // No compression
#define COMPRESS_ALGO_ZLIB 0x01 // zlib deflate compression
#define COMPRESS_ALGO_LZ4 0x02  // LZ4 fast compression (future)

// Feature flags for protocol_version_packet_t
#define FEATURE_RLE_ENCODING 0x01 // Run-length encoding support
#define FEATURE_DELTA_FRAMES 0x02 // Delta frame encoding (future)

// Protocol version packet (Packet Type 1) - Initial handshake packet
// Sent by both client and server to negotiate protocol capabilities
typedef struct {
  uint16_t protocol_version;      // Major version (e.g., 1)
  uint16_t protocol_revision;     // Minor revision (e.g., 0)
  uint8_t supports_encryption;    // Boolean: 1=yes, 0=no (plaintext mode)
  uint8_t compression_algorithms; // Bitmap: COMPRESS_ALGO_ZLIB, etc.
  uint8_t compression_threshold;  // 0-100 percentage (80 = 80%)
  uint16_t feature_flags;         // FEATURE_RLE_ENCODING, etc.
  uint8_t reserved[7];            // Padding to 16 bytes
} PACKED_ATTR protocol_version_packet_t;

// ============================================================================
// Frame Packet Structures
// ============================================================================

// ASCII frame packet - contains complete ASCII frame with metadata
// Used for both client->server and server->client frame transmission
typedef struct {
  uint32_t width;           // Terminal width in characters
  uint32_t height;          // Terminal height in characters
  uint32_t original_size;   // Size of original ASCII data
  uint32_t compressed_size; // Size of compressed data (0 = not compressed)
  uint32_t checksum;        // CRC32 of original ASCII data
  uint32_t flags;           // Bit flags: HAS_COLOR, IS_COMPRESSED, etc.

  // The actual ASCII frame data follows this header in the packet payload
  // If compressed_size > 0, data is zlib compressed
  // Format: char data[original_size] or compressed_data[compressed_size]
} PACKED_ATTR ascii_frame_packet_t;

// Image frame packet - contains raw RGB image with dimensions
// Used when client sends camera frames to server
typedef struct {
  uint32_t width;           // Image width in pixels
  uint32_t height;          // Image height in pixels
  uint32_t pixel_format;    // Format: 0=RGB, 1=RGBA, 2=BGR, etc.
  uint32_t compressed_size; // If >0, image is compressed
  uint32_t checksum;        // CRC32 of pixel data
  uint32_t timestamp;       // When frame was captured

  // The actual pixel data follows this header in the packet payload
  // Format: rgb_t pixels[width * height] or compressed data
} PACKED_ATTR image_frame_packet_t;

// Pixel formats are now defined in common.h

// Audio batch packet - contains multiple audio chunks for efficiency
typedef struct {
  uint32_t batch_count;   // Number of audio chunks in this batch (usually AUDIO_BATCH_COUNT)
  uint32_t total_samples; // Total samples across all chunks
  uint32_t sample_rate;   // Sample rate (e.g., 44100)
  uint32_t channels;      // Number of channels (1=mono, 2=stereo)
  // The actual audio data follows: float samples[total_samples]
} PACKED_ATTR audio_batch_packet_t;

// Capability flags
#define CLIENT_CAP_VIDEO 0x01
#define CLIENT_CAP_AUDIO 0x02
#define CLIENT_CAP_COLOR 0x04
#define CLIENT_CAP_STRETCH 0x08

// Stream type flags
#define STREAM_TYPE_VIDEO 0x01
#define STREAM_TYPE_AUDIO 0x02

// Crypto capabilities packet
typedef struct {
  uint16_t supported_kex_algorithms;    // Supported key exchange algorithms
  uint16_t supported_auth_algorithms;   // Supported authentication algorithms
  uint16_t supported_cipher_algorithms; // Supported cipher algorithms
  uint8_t requires_verification;        // Whether server verification is required
  uint8_t preferred_kex;                // Preferred key exchange algorithm
  uint8_t preferred_auth;               // Preferred authentication algorithm
  uint8_t preferred_cipher;             // Preferred cipher algorithm
} PACKED_ATTR crypto_capabilities_packet_t;

// Crypto parameters packet (Packet Type 16) - Server → Client
// Server replies with chosen algorithms and handshake data sizes
typedef struct {
  uint8_t selected_kex;          // Which KEX algorithm (KEX_ALGO_*)
  uint8_t selected_auth;         // Which auth algorithm (AUTH_ALGO_*)
  uint8_t selected_cipher;       // Which cipher algorithm (CIPHER_ALGO_*)
  uint8_t verification_enabled;  // Boolean: server requires verification
  uint16_t kex_public_key_size;  // e.g., 32 for X25519, 1568 for Kyber1024
  uint16_t auth_public_key_size; // e.g., 32 for Ed25519, 1952 for Dilithium3
  uint16_t signature_size;       // e.g., 64 for Ed25519, 3309 for Dilithium3
  uint16_t shared_secret_size;   // e.g., 32 for X25519
  uint8_t nonce_size;            // e.g., 24 for XSalsa20 nonce
  uint8_t mac_size;              // e.g., 16 for Poly1305 MAC
  uint8_t hmac_size;             // e.g., 32 for HMAC-SHA256
  uint8_t reserved[3];           // Padding to 24 bytes
} PACKED_ATTR crypto_parameters_packet_t;

// Terminal capabilities packet - sent by client to inform server of capabilities
typedef struct {
  uint32_t capabilities;      // Bitmask of TERM_CAP_* flags
  uint32_t color_level;       // terminal_color_level_t enum value
  uint32_t color_count;       // Actual color count (16, 256, 16777216)
  uint32_t render_mode;       // render_mode_t enum value (foreground/background/half-block)
  uint16_t width, height;     // Terminal dimensions
  char term_type[32];         // $TERM value for debugging
  char colorterm[32];         // $COLORTERM value for debugging
  uint8_t detection_reliable; // True if detection methods were reliable
  uint32_t utf8_support;      // 0=no UTF-8, 1=UTF-8 supported
  uint32_t palette_type;      // palette_type_t enum value
  char palette_custom[64];    // Custom palette chars (if palette_type == PALETTE_CUSTOM)
  uint8_t desired_fps;        // Client's desired frame rate (1-144 FPS)
  uint8_t reserved[2];        // Padding for alignment
} PACKED_ATTR terminal_capabilities_packet_t;

// Crypto algorithm constants
#define KEX_ALGO_X25519 0x01
#define AUTH_ALGO_ED25519 0x01
#define AUTH_ALGO_NONE 0x00
#define CIPHER_ALGO_XSALSA20_POLY1305 0x01

#ifdef _WIN32
#pragma pack(pop)
#endif
