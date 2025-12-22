/**
 * @file network/packet_types.h
 * @ingroup packet
 * @ingroup network
 * @addtogroup packet
 * @{
 *
 * This module defines all packet types, packet structures, and protocol
 * constants used throughout the ascii-chat network communication system.
 * It serves as the central definition point for the network protocol.
 *
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Packet type enumeration for all protocol messages
 * 2. Packet structure definitions (headers, payloads, metadata)
 * 3. Protocol constants and configuration values
 * 4. Handshake packet type detection utilities
 * 5. Wire format compatibility (packed structures for network transmission)
 *
 * ARCHITECTURAL OVERVIEW:
 * =======================
 *
 * PACKET TYPES:
 * - Protocol negotiation: Version, capabilities, parameters
 * - Frame packets: ASCII frames, image frames
 * - Audio packets: Single and batched audio samples
 * - Control packets: Ping/pong, clear console, client lifecycle
 * - Crypto handshake: Key exchange, authentication, rekeying
 * - Multi-user protocol: Client join/leave, stream control
 *
 * PACKET STRUCTURE:
 * - Packed structures for wire format compatibility
 * - Fixed-size headers with variable-length payloads
 * - Network byte order for cross-platform compatibility
 * - Magic number validation for packet detection
 *
 * PROTOCOL VERSIONS:
 * - Major version (protocol_version field)
 * - Minor revision (protocol_revision field)
 * - Feature flags for optional capabilities
 * - Compression algorithm negotiation
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - network/packet.h: Uses these types for packet I/O
 * - network/av.h: Uses frame and audio packet types
 * - crypto/crypto.h: Uses crypto handshake packet types
 * - src/server/protocol.c: Processes these packet types
 *
 * PERFORMANCE CHARACTERISTICS:
 * ============================
 * - Packed structures minimize wire format overhead
 * - Audio batching reduces packet overhead
 * - Large packet detection for timeout adjustment
 * - Efficient packet type checking
 *
 * @note All packet structures are packed for wire format compatibility.
 *       Structures may not be naturally aligned.
 *
 * @note Packet types are organized by category: protocol negotiation,
 *       frames, audio, control, crypto, multi-user extensions.
 *
 * @warning Handshake packets (types 14-23, 25-27) must NEVER be encrypted.
 *          Use packet_is_handshake_type() to check before encryption.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 */

#pragma once

#include "platform/abstraction.h"
#include "network/logging.h" // IWYU pragma: keep (re-export remote log direction type)
#include <stdbool.h>
#include <stdint.h>

// Pack network protocol structures tightly for wire format
#ifdef _WIN32
#pragma pack(push, 1)
#endif

/**
 * @name Network Protocol Constants
 * @{
 * @ingroup packet
 */

/**
 * @brief Large packet size threshold (100KB)
 *
 * Packets larger than this threshold are considered "large" and may
 * receive extended timeouts or compression treatment.
 *
 * @ingroup packet
 */
#define LARGE_PACKET_THRESHOLD (100 * 1024)

/**
 * @brief Maximum packet size (5MB)
 *
 * Absolute maximum size for any packet. Packets larger than this
 * are rejected to prevent memory exhaustion attacks.
 *
 * @ingroup packet
 */
#define MAX_PACKET_SIZE ((size_t)5 * 1024 * 1024)

/**
 * @brief Maximum error message length (512 bytes)
 *
 * Error packets include a message payload. This defines the maximum number of
 * bytes allowed in the message portion to prevent excessive allocations and
 * potential abuse.
 */
#define MAX_ERROR_MESSAGE_LENGTH 512

/**
 * @brief Maximum remote log message length (512 bytes)
 *
 * Remote logging packets mirror the error packet structure but are intended for
 * diagnostic log forwarding between client and server. Limiting the payload
 * size keeps allocations predictable and prevents log flooding over the
 * network.
 */
#define MAX_REMOTE_LOG_MESSAGE_LENGTH 512

/** @} */

/**
 * @name Timeout Configuration Constants
 * @{
 * @ingroup packet
 *
 * Timeout constants for handling packets of different sizes.
 * Large packets require extended timeouts for successful transmission.
 */

/**
 * @brief Base send timeout in seconds (5 seconds)
 *
 * Default timeout for sending packets. Used as base value for
 * calculating timeouts for large packets.
 *
 * @ingroup packet
 */
#define BASE_SEND_TIMEOUT 5

/**
 * @brief Extra timeout per MB for large packets (0.8 seconds per MB)
 *
 * Additional timeout added per MB for large packets to prevent
 * premature timeout failures.
 *
 * @ingroup packet
 */
#define LARGE_PACKET_EXTRA_TIMEOUT_PER_MB 0.8

/**
 * @brief Minimum client timeout in seconds (10 seconds)
 *
 * Minimum timeout value for client operations. Set to server timeout
 * plus buffer for reliable operation.
 *
 * @ingroup packet
 */
#define MIN_CLIENT_TIMEOUT 10

/**
 * @brief Maximum client timeout in seconds (60 seconds)
 *
 * Maximum timeout value for client operations. Prevents excessively
 * long waits for dead connections.
 *
 * @ingroup packet
 */
#define MAX_CLIENT_TIMEOUT 60

/** @} */

/**
 * @name Audio Batching Constants
 * @{
 * @ingroup packet
 *
 * Audio batching configuration for efficient audio transmission.
 * Batched audio reduces packet overhead and improves bandwidth usage.
 */

/**
 * @brief Number of audio chunks per batch (4 chunks)
 *
 * Number of audio chunks aggregated into a single batch packet
 * for efficiency.
 *
 * @ingroup packet
 */
#define AUDIO_BATCH_COUNT 32

/**
 * @brief Total samples in audio batch (8192 samples)
 *
 * Total audio samples across all chunks in a batch. At 44.1kHz,
 * this equals ~185.8ms of audio.
 *
 * @ingroup packet
 */
#define AUDIO_BATCH_SAMPLES (AUDIO_SAMPLES_PER_PACKET * AUDIO_BATCH_COUNT)

/**
 * @brief Audio batch duration in milliseconds (~186ms)
 *
 * Approximate duration of audio batch at 44.1kHz sample rate.
 *
 * @ingroup packet
 */
#define AUDIO_BATCH_MS 186

/**
 * @brief Samples per audio packet (256 samples)
 *
 * Number of audio samples in a single audio packet. Smaller packets
 * provide lower latency at the cost of increased packet overhead.
 *
 * @ingroup packet
 */
#define AUDIO_SAMPLES_PER_PACKET 256

/** @} */

/**
 * @name Protocol Constants
 * @{
 * @ingroup packet
 */

/**
 * @brief Packet magic number (0xDEADBEEF)
 *
 * Magic number used in packet headers for packet validation.
 * Invalid magic numbers indicate corrupted or invalid packets.
 *
 * @ingroup packet
 */
#define PACKET_MAGIC 0xDEADBEEF

/** @} */

/**
 * @brief Network protocol packet type enumeration
 *
 * Defines all packet types used in the ascii-chat network protocol.
 * Packet types are organized by category: protocol negotiation, frames,
 * audio, control, crypto handshake, multi-user extensions, messages.
 *
 * PACKET CATEGORIES:
 * - 1: Protocol negotiation
 * - 2-3: Frame packets (ASCII, image)
 * - 4: Audio packets (legacy single)
 * - 5-7: Control packets (capabilities, ping/pong)
 * - 8-13: Multi-user protocol extensions
 * - 14-24: Crypto handshake packets (ALWAYS UNENCRYPTED)
 * - 25-27: Crypto rekeying packets (ALWAYS UNENCRYPTED during rekey)
 * - 28: Audio batching (efficient)
 * - 29-31: Message packets (size, audio, text)
 *
 * @note Crypto handshake packets (14-23) and rekey packets (25-27) are
 *       ALWAYS sent unencrypted. Use packet_is_handshake_type() to check.
 *
 * @note PACKET_TYPE_ENCRYPTED (24) is used for encrypted session packets
 *       after handshake completion.
 *
 * @ingroup packet
 */
typedef enum {
  /** @brief Protocol version and capabilities negotiation */
  PACKET_TYPE_PROTOCOL_VERSION = 1,

  /** @brief Complete ASCII frame with all metadata */
  PACKET_TYPE_ASCII_FRAME = 2,
  /** @brief Complete RGB image with dimensions */
  PACKET_TYPE_IMAGE_FRAME = 3,

  /** @brief Single audio packet (legacy) */
  PACKET_TYPE_AUDIO = 4,
  /** @brief Client reports terminal capabilities */
  PACKET_TYPE_CLIENT_CAPABILITIES = 5,
  /** @brief Keepalive ping packet */
  PACKET_TYPE_PING = 6,
  /** @brief Keepalive pong response */
  PACKET_TYPE_PONG = 7,

  /** @brief Client announces capability to send media */
  PACKET_TYPE_CLIENT_JOIN = 8,
  /** @brief Clean disconnect notification */
  PACKET_TYPE_CLIENT_LEAVE = 9,
  /** @brief Client requests to start sending video/audio */
  PACKET_TYPE_STREAM_START = 10,
  /** @brief Client stops sending media */
  PACKET_TYPE_STREAM_STOP = 11,
  /** @brief Server tells client to clear console */
  PACKET_TYPE_CLEAR_CONSOLE = 12,
  /** @brief Server sends current state to clients */
  PACKET_TYPE_SERVER_STATE = 13,

  /** @brief Client -> Server: Supported crypto algorithms (UNENCRYPTED) */
  PACKET_TYPE_CRYPTO_CAPABILITIES = 14,
  /** @brief Server -> Client: Chosen algorithms + data sizes (UNENCRYPTED) */
  PACKET_TYPE_CRYPTO_PARAMETERS = 15,
  /** @brief Server -> Client: {server_pubkey[32]} (UNENCRYPTED) */
  PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT = 16,
  /** @brief Client -> Server: {client_pubkey[32]} (UNENCRYPTED) */
  PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP = 17,
  /** @brief Server -> Client: {nonce[32]} (UNENCRYPTED) */
  PACKET_TYPE_CRYPTO_AUTH_CHALLENGE = 18,
  /** @brief Client -> Server: {HMAC[32]} (UNENCRYPTED) */
  PACKET_TYPE_CRYPTO_AUTH_RESPONSE = 19,
  /** @brief Server -> Client: "authentication failed" (UNENCRYPTED) */
  PACKET_TYPE_CRYPTO_AUTH_FAILED = 20,
  /** @brief Server -> Client: {HMAC[32]} server proves knowledge (UNENCRYPTED) */
  PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP = 21,
  /** @brief Server -> Client: "encryption ready" (UNENCRYPTED) */
  PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE = 22,
  /** @brief Client -> Server: "I want to proceed without encryption" (UNENCRYPTED) */
  PACKET_TYPE_CRYPTO_NO_ENCRYPTION = 23,
  /** @brief Encrypted packet (after handshake completion) */
  PACKET_TYPE_ENCRYPTED = 24,

  /** @brief Initiator -> Responder: {new_ephemeral_pk[32]} (UNENCRYPTED during rekey) */
  PACKET_TYPE_CRYPTO_REKEY_REQUEST = 25,
  /** @brief Responder -> Initiator: {new_ephemeral_pk[32]} (UNENCRYPTED during rekey) */
  PACKET_TYPE_CRYPTO_REKEY_RESPONSE = 26,
  /** @brief Initiator -> Responder: Empty (encrypted with NEW key, but still handshake) */
  PACKET_TYPE_CRYPTO_REKEY_COMPLETE = 27,

  /** @brief Batched audio packets for efficiency */
  PACKET_TYPE_AUDIO_BATCH = 28,

  /** @brief Terminal size message */
  PACKET_TYPE_SIZE_MESSAGE = 29,
  /** @brief Audio message */
  PACKET_TYPE_AUDIO_MESSAGE = 30,
  /** @brief Text message */
  PACKET_TYPE_TEXT_MESSAGE = 31,
  /** @brief Error packet with asciichat_error_t code and human-readable message */
  PACKET_TYPE_ERROR_MESSAGE = 32,
  /** @brief Bidirectional remote logging packet */
  PACKET_TYPE_REMOTE_LOG = 33
} packet_type_t;

/**
 * @brief Determine if packet type is a handshake packet (must NEVER be encrypted)
 * @param type Packet type to check
 * @return true if packet is a handshake/rekey packet, false otherwise
 *
 * Identifies handshake packets that must be sent unencrypted. This includes:
 * - Initial crypto handshake packets (types 14-23)
 * - Session rekeying packets (types 25-27)
 *
 * CRITICAL HANDLING:
 * - Handshake packets are ALWAYS sent in plaintext
 * - This includes all crypto negotiation packets
 * - PACKET_TYPE_ENCRYPTED (24) is NOT a handshake packet
 * - PACKET_TYPE_CRYPTO_REKEY_COMPLETE (27) is encrypted with NEW key,
 *   but still considered handshake for routing purposes
 *
 * @note This function should be used BEFORE encryption to prevent
 *       accidentally encrypting handshake packets.
 *
 * @note Packet handling logic must account for REKEY_COMPLETE being
 *       encrypted with NEW key rather than current session key.
 *
 * @warning Encrypting handshake packets will break the crypto handshake.
 *
 * @ingroup packet
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

/**
 * @brief Network packet header structure
 *
 * Standard header for all network packets. Contains magic number for
 * validation, packet type, payload length, CRC32 checksum, and client ID.
 *
 * PACKET HEADER FIELDS:
 * - magic: PACKET_MAGIC constant for packet detection/validation
 * - type: Packet type enumeration (packet_type_t)
 * - length: Payload data length in bytes (0 for header-only packets)
 * - crc32: CRC32 checksum of payload data (0 if length == 0)
 * - client_id: Client identifier (0 = server, >0 = client ID)
 *
 * @note Structure is packed for wire format compatibility (no padding).
 *
 * @note Magic number must be PACKET_MAGIC or packet is considered invalid.
 *
 * @note CRC32 is computed over payload data, not including header.
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Magic number (PACKET_MAGIC) for packet validation */
  uint32_t magic;
  /** @brief Packet type (packet_type_t enumeration) */
  uint16_t type;
  /** @brief Payload data length in bytes (0 for header-only packets) */
  uint32_t length;
  /** @brief CRC32 checksum of payload data (0 if length == 0) */
  uint32_t crc32;
  /** @brief Client ID (0 = server, >0 = client identifier) */
  uint32_t client_id;
} /** @cond */
PACKED_ATTR /** @endcond */ packet_header_t;

/**
 * @name Multi-User Protocol Constants
 * @{
 * @ingroup packet
 */

/**
 * @brief Default display name for clients without a custom name
 *
 * Used when a client connects without specifying a display name.
 *
 * @ingroup packet
 */
#define ASCIICHAT_DEFAULT_DISPLAY_NAME "AsciiChatter"

/**
 * @brief Maximum length of client display name in bytes (32 bytes)
 *
 * Maximum length for display_name field in client_info_packet_t.
 * Includes null terminator.
 *
 * @ingroup packet
 */
#define MAX_DISPLAY_NAME_LEN 32

/** @} */

/**
 * @brief Terminal size update packet
 *
 * Packet structure for notifying server/client of terminal dimension changes.
 * Used when terminal is resized to update rendering dimensions.
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Terminal width in characters */
  uint32_t width;
  /** @brief Terminal height in characters */
  uint32_t height;
} /** @cond */
PACKED_ATTR /** @endcond */ size_packet_t;

/**
 * @brief Client information packet structure
 *
 * Contains client identification and capability information.
 * Used in multi-user protocol for client join/leave notifications.
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Unique client identifier (1-9, 0 = server) */
  uint32_t client_id;
  /** @brief User display name (null-terminated, max MAX_DISPLAY_NAME_LEN bytes) */
  char display_name[MAX_DISPLAY_NAME_LEN];
  /** @brief Client capabilities bitmask (CLIENT_CAP_VIDEO | CLIENT_CAP_AUDIO | CLIENT_CAP_COLOR | CLIENT_CAP_STRETCH)
   */
  uint32_t capabilities;
} /** @cond */
PACKED_ATTR /** @endcond */ client_info_packet_t;

/**
 * @brief Stream header packet structure
 *
 * Header prepended to media streams (video/audio) to identify source client
 * and stream characteristics. Used when multiple clients are streaming.
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Client ID that this stream originates from (1-9) */
  uint32_t client_id;
  /** @brief Stream type bitmask (STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO) */
  uint32_t stream_type;
  /** @brief Timestamp when frame/audio was captured (milliseconds since epoch) */
  uint32_t timestamp;
} /** @cond */
PACKED_ATTR /** @endcond */ stream_header_t;

/**
 * @brief Client list packet structure
 *
 * Contains list of all connected clients with their information.
 * Broadcast by server to all clients when client list changes.
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Number of clients in the list (0 to MAX_CLIENTS) */
  uint32_t client_count;
  /** @brief Array of client information structures */
  client_info_packet_t clients[MAX_CLIENTS];
} /** @cond */
PACKED_ATTR /** @endcond */ client_list_packet_t;

/**
 * @brief Server state packet structure
 *
 * Server broadcasts current connection state to all clients.
 * Sent when client count or active stream count changes.
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Total number of currently connected clients */
  uint32_t connected_client_count;
  /** @brief Number of clients actively sending video/audio streams */
  uint32_t active_client_count;
  /** @brief Reserved fields for future use (must be zero) */
  uint32_t reserved[6];
} /** @cond */
PACKED_ATTR /** @endcond */ server_state_packet_t;

/**
 * @brief Error packet structure carrying error code and textual description
 *
 * Error packets allow either side of the connection to report a specific
 * `asciichat_error_t` along with a human-readable message describing the
 * failure. The message payload follows the structure in the packet and is not
 * guaranteed to be null-terminated on the wire. Consumers should rely on the
 * `message_length` field when copying. Error packets may be transmitted in
 * plaintext before the crypto handshake completes (and whenever encryption is
 * disabled), but MUST be encrypted once a session key is active.
 */
typedef struct {
  /** @brief Error code from asciichat_error_t enumeration */
  uint32_t error_code;
  /** @brief Length of message payload in bytes (0-512) */
  uint32_t message_length;
} /** @cond */
PACKED_ATTR /** @endcond */ error_packet_t;

/** @brief Remote log packet flag definitions */
#define REMOTE_LOG_FLAG_TRUNCATED 0x0001U /**< @brief Message payload was truncated to fit the maximum length */

/**
 * @brief Remote log packet structure carrying log level and message text
 */
typedef struct {
  /** @brief Log level associated with the message (log_level_t cast to uint8_t) */
  uint8_t log_level;
  /** @brief Direction hint so receivers can annotate origin */
  uint8_t direction;
  /** @brief Additional flags (REMOTE_LOG_FLAG_*) */
  uint16_t flags;
  /** @brief Message payload length in bytes (0-512) */
  uint32_t message_length;
} /** @cond */
PACKED_ATTR /** @endcond */ remote_log_packet_t;

/**
 * @brief Authentication failure reason flags
 *
 * Bitmask enumeration for authentication failure reasons.
 * Multiple flags can be combined to indicate multiple failure causes.
 *
 * @ingroup packet
 */
typedef enum {
  /** @brief Server requires password but client didn't provide one */
  AUTH_FAIL_PASSWORD_REQUIRED = 0x01,
  /** @brief Password verification failed (incorrect password) */
  AUTH_FAIL_PASSWORD_INCORRECT = 0x02,
  /** @brief Server requires client key but client didn't provide one */
  AUTH_FAIL_CLIENT_KEY_REQUIRED = 0x04,
  /** @brief Client key not in whitelist (access denied) */
  AUTH_FAIL_CLIENT_KEY_REJECTED = 0x08,
  /** @brief Client signature verification failed (invalid signature) */
  AUTH_FAIL_SIGNATURE_INVALID = 0x10
} auth_failure_reason_t;

/**
 * @brief Authentication failure packet structure
 *
 * Sent by server to client when authentication fails during crypto handshake.
 * Contains reason flags explaining why authentication was rejected.
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Bitmask of auth_failure_reason_t values indicating failure causes */
  uint8_t reason_flags;
  /** @brief Reserved bytes for future use (must be zero) */
  uint8_t reserved[7];
} /** @cond */
PACKED_ATTR /** @endcond */ auth_failure_packet_t;

/**
 * @name Protocol Negotiation Constants
 * @{
 * @ingroup packet
 */

/**
 * @brief Feature flags for protocol_version_packet_t
 *
 * Bitmask values for optional protocol features.
 * Used in protocol negotiation to enable advanced capabilities.
 */
#define FEATURE_RLE_ENCODING 0x01 /**< @brief Run-length encoding support */
#define FEATURE_DELTA_FRAMES 0x02 /**< @brief Delta frame encoding (future) */

/** @} */

/**
 * @brief Protocol version negotiation packet structure (Packet Type 1)
 *
 * Initial handshake packet sent by both client and server to negotiate
 * protocol capabilities, version, compression, and optional features.
 *
 * @note This is the first packet exchanged in every connection.
 * @note Structure is 16 bytes total (packed).
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Major protocol version (must match for compatibility) */
  uint16_t protocol_version;
  /** @brief Minor protocol revision (server can be newer) */
  uint16_t protocol_revision;
  /** @brief Encryption support flag (1=support encryption, 0=plaintext only) */
  uint8_t supports_encryption;
  /** @brief Supported compression algorithms bitmask (COMPRESS_ALGO_*) */
  uint8_t compression_algorithms;
  /** @brief Compression threshold percentage (0-100, e.g., 80 = compress if >80% size reduction) */
  uint8_t compression_threshold;
  /** @brief Feature flags bitmask (FEATURE_RLE_ENCODING, etc.) */
  uint16_t feature_flags;
  /** @brief Reserved bytes for future expansion (must be zero) */
  uint8_t reserved[7];
} /** @cond */
PACKED_ATTR /** @endcond */ protocol_version_packet_t;

/**
 * @brief ASCII frame packet structure (Packet Type 2)
 *
 * Contains complete ASCII art frame with metadata for terminal rendering.
 * Used for both client->server (camera input) and server->client (mixed output) transmission.
 *
 * @note ASCII frame data follows this header in the packet payload.
 * @note If compressed_size > 0, data is zstd compressed.
 * @note Format: char data[original_size] or compressed_data[compressed_size]
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Terminal width in characters */
  uint32_t width;
  /** @brief Terminal height in characters */
  uint32_t height;
  /** @brief Size of original uncompressed ASCII data in bytes */
  uint32_t original_size;
  /** @brief Size of compressed data (0 = not compressed) */
  uint32_t compressed_size;
  /** @brief CRC32 checksum of original ASCII data */
  uint32_t checksum;
  /** @brief Frame flags bitmask (HAS_COLOR, IS_COMPRESSED, etc.) */
  uint32_t flags;
} /** @cond */
PACKED_ATTR /** @endcond */ ascii_frame_packet_t;

/**
 * @brief Image frame packet structure (Packet Type 3)
 *
 * Contains raw RGB image with dimensions from camera/webcam.
 * Used when client sends camera frames to server for ASCII conversion.
 *
 * @note Pixel data follows this header in the packet payload.
 * @note Format: rgb_t pixels[width * height] or compressed data
 * @note Pixel formats are defined in common.h
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Image width in pixels */
  uint32_t width;
  /** @brief Image height in pixels */
  uint32_t height;
  /** @brief Pixel format enum (0=RGB24, 1=RGBA32, 2=BGR24, etc.) */
  uint32_t pixel_format;
  /** @brief Compressed data size (0 = not compressed, >0 = compressed) */
  uint32_t compressed_size;
  /** @brief CRC32 checksum of pixel data */
  uint32_t checksum;
  /** @brief Timestamp when frame was captured (milliseconds since epoch) */
  uint32_t timestamp;
} /** @cond */
PACKED_ATTR /** @endcond */ image_frame_packet_t;

/**
 * @brief Audio batch packet structure (Packet Type 28)
 *
 * Contains multiple audio chunks batched together for efficiency.
 * Reduces packet overhead by aggregating 32 chunks (8192 samples) per packet.
 *
 * @note Audio samples follow this header in the packet payload.
 * @note Format: float samples[total_samples] (interleaved if stereo)
 * @note At 44.1kHz, this represents ~186ms of audio per batch.
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Sequence number for detecting packet loss (increments per batch) */
  uint32_t sequence;
  /** @brief Number of audio chunks in this batch (usually AUDIO_BATCH_COUNT = 32) */
  uint32_t batch_count;
  /** @brief Total audio samples across all chunks (typically 8192) */
  uint32_t total_samples;
  /** @brief Sample rate in Hz (e.g., 44100, 48000) */
  uint32_t sample_rate;
  /** @brief Number of audio channels (1=mono, 2=stereo) */
  uint32_t channels;
} /** @cond */
PACKED_ATTR /** @endcond */ audio_batch_packet_t;

/**
 * @name Client Capability Flags
 * @{
 * @ingroup packet
 *
 * Bitmask flags for client capabilities in multi-user protocol.
 */
#define CLIENT_CAP_VIDEO 0x01   /**< @brief Client can send/receive video */
#define CLIENT_CAP_AUDIO 0x02   /**< @brief Client can send/receive audio */
#define CLIENT_CAP_COLOR 0x04   /**< @brief Client supports color rendering */
#define CLIENT_CAP_STRETCH 0x08 /**< @brief Client can stretch frames to fill terminal */

/** @} */

/**
 * @name Stream Type Flags
 * @{
 * @ingroup packet
 *
 * Bitmask flags for stream types in multi-user protocol.
 */
#define STREAM_TYPE_VIDEO 0x01 /**< @brief Video stream */
#define STREAM_TYPE_AUDIO 0x02 /**< @brief Audio stream */

/** @} */

/**
 * @brief Crypto capabilities packet structure (Packet Type 14)
 *
 * Sent by client to server during crypto handshake to advertise
 * supported cryptographic algorithms and preferences.
 *
 * @note This packet must be sent UNENCRYPTED (handshake packet).
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Supported key exchange algorithms bitmask (KEX_ALGO_*) */
  uint16_t supported_kex_algorithms;
  /** @brief Supported authentication algorithms bitmask (AUTH_ALGO_*) */
  uint16_t supported_auth_algorithms;
  /** @brief Supported cipher algorithms bitmask (CIPHER_ALGO_*) */
  uint16_t supported_cipher_algorithms;
  /** @brief Server verification requirement flag (1=required, 0=optional) */
  uint8_t requires_verification;
  /** @brief Preferred key exchange algorithm (KEX_ALGO_*) */
  uint8_t preferred_kex;
  /** @brief Preferred authentication algorithm (AUTH_ALGO_*) */
  uint8_t preferred_auth;
  /** @brief Preferred cipher algorithm (CIPHER_ALGO_*) */
  uint8_t preferred_cipher;
} /** @cond */
PACKED_ATTR /** @endcond */ crypto_capabilities_packet_t;

/**
 * @brief Crypto parameters packet structure (Packet Type 15)
 *
 * Sent by server to client during crypto handshake to communicate
 * chosen algorithms and algorithm-specific data sizes for handshake.
 *
 * @note This packet must be sent UNENCRYPTED (handshake packet).
 * @note Structure is 24 bytes total (packed).
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Selected key exchange algorithm (KEX_ALGO_*) */
  uint8_t selected_kex;
  /** @brief Selected authentication algorithm (AUTH_ALGO_*) */
  uint8_t selected_auth;
  /** @brief Selected cipher algorithm (CIPHER_ALGO_*) */
  uint8_t selected_cipher;
  /** @brief Server verification enabled flag (1=enabled, 0=disabled) */
  uint8_t verification_enabled;
  /** @brief Key exchange public key size in bytes (e.g., 32 for X25519, 1568 for Kyber1024) */
  uint16_t kex_public_key_size;
  /** @brief Authentication public key size in bytes (e.g., 32 for Ed25519, 1952 for Dilithium3) */
  uint16_t auth_public_key_size;
  /** @brief Signature size in bytes (e.g., 64 for Ed25519, 3309 for Dilithium3) */
  uint16_t signature_size;
  /** @brief Shared secret size in bytes (e.g., 32 for X25519) */
  uint16_t shared_secret_size;
  /** @brief Nonce size in bytes (e.g., 24 for XSalsa20) */
  uint8_t nonce_size;
  /** @brief MAC size in bytes (e.g., 16 for Poly1305) */
  uint8_t mac_size;
  /** @brief HMAC size in bytes (e.g., 32 for HMAC-SHA256) */
  uint8_t hmac_size;
  /** @brief Reserved bytes for future use (must be zero) */
  uint8_t reserved[3];
} /** @cond */
PACKED_ATTR /** @endcond */ crypto_parameters_packet_t;

/**
 * @brief Terminal capabilities packet structure (Packet Type 5)
 *
 * Sent by client to server to report terminal capabilities, dimensions,
 * color support, and rendering preferences for optimal frame delivery.
 *
 * @ingroup packet
 */
typedef struct {
  /** @brief Terminal capabilities bitmask (TERM_CAP_* flags) */
  uint32_t capabilities;
  /** @brief Color level enum value (terminal_color_level_t) */
  uint32_t color_level;
  /** @brief Actual color count (16, 256, or 16777216) */
  uint32_t color_count;
  /** @brief Render mode enum value (foreground/background/half-block) */
  uint32_t render_mode;
  /** @brief Terminal width in characters */
  uint16_t width;
  /** @brief Terminal height in characters */
  uint16_t height;
  /** @brief $TERM environment variable value (for debugging) */
  char term_type[32];
  /** @brief $COLORTERM environment variable value (for debugging) */
  char colorterm[32];
  /** @brief Detection reliability flag (1=reliable detection, 0=best guess) */
  uint8_t detection_reliable;
  /** @brief UTF-8 support flag (0=no UTF-8, 1=UTF-8 supported) */
  uint32_t utf8_support;
  /** @brief Palette type enum value (palette_type_t) */
  uint32_t palette_type;
  /** @brief Custom palette characters (if palette_type == PALETTE_CUSTOM) */
  char palette_custom[64];
  /** @brief Client's desired frame rate (1-144 FPS) */
  uint8_t desired_fps;
  /** @brief Reserved bytes for alignment (must be zero) */
  uint8_t reserved[2];
} /** @cond */
PACKED_ATTR /** @endcond */ terminal_capabilities_packet_t;

/**
 * @name Crypto Algorithm Constants
 * @{
 * @ingroup packet
 *
 * Algorithm identifiers for key exchange, authentication, and encryption.
 * Used in crypto handshake packet negotiation.
 */
#define KEX_ALGO_X25519 0x01               /**< @brief X25519 key exchange (Curve25519) */
#define AUTH_ALGO_ED25519 0x01             /**< @brief Ed25519 authentication (Edwards-curve signatures) */
#define AUTH_ALGO_NONE 0x00                /**< @brief No authentication (plaintext mode) */
#define CIPHER_ALGO_XSALSA20_POLY1305 0x01 /**< @brief XSalsa20-Poly1305 authenticated encryption */

/** @} */

#ifdef _WIN32
#pragma pack(pop)
#endif
