/**
 * @defgroup packet_parsing Shared Packet Parsing Utilities
 * @ingroup module_network
 * @brief 📡 Shared protocol parsing utilities for server and client packet handlers
 *
 * @file network/packet_parsing.h
 * @brief Shared packet parsing utilities to eliminate duplication between server and client handlers
 * @ingroup packet_parsing
 * @addtogroup packet_parsing
 * @{
 *
 * This module provides reusable utilities for parsing and validating protocol packets,
 * used by both server (src/server/protocol.c) and client (src/client/protocol.c) handlers.
 *
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Frame data decoding (handles compressed and uncompressed formats)
 * 2. Network byte order conversions and validation
 * 3. Audio batch header parsing and validation
 * 4. Frame dimension validation with overflow checking
 * 5. Generic packet payload validation helpers
 *
 * DESIGN PRINCIPLES:
 * ==================
 * - Minimal dependencies on protocol-specific types
 * - Clear error codes and messages
 * - No assumptions about error handling (caller decides)
 * - Support for both server and client usage patterns
 * - Overflow-safe integer arithmetic throughout
 *
 * USAGE PATTERNS:
 * ===============
 * Server handlers use these for incoming client packets:
 * @code
 *   // Decode compressed/uncompressed frame data
 *   frame_buffer_t *decoded = packet_decode_frame_data(
 *       frame_data_ptr, frame_data_len, is_compressed,
 *       expected_size, compressed_size);
 *
 *   // Validate audio batch header
 *   audio_batch_info_t batch;
 *   result = packet_parse_audio_batch_header(data, len, &batch);
 * @endcode
 *
 * Client handlers use these for incoming server packets:
 * @code
 *   // Same frame decoding for server->client frames
 *   char *decoded = packet_decode_frame_data_malloc(
 *       frame_data, frame_data_len, is_compressed,
 *       original_size, compressed_size);
 * @endcode
 *
 * INTEGRATION POINTS:
 * ===================
 * - src/server/protocol.c: IMAGE_FRAME, AUDIO_BATCH, AUDIO_OPUS_BATCH packet handling
 * - src/client/protocol.c: ASCII_FRAME, AUDIO_BATCH, AUDIO_OPUS_BATCH packet handling
 * - lib/network/packet.h: Low-level packet structure definitions
 * - lib/network/av.h: Audio/video packet serialization
 * - lib/util/validation.h: High-level packet validation macros
 *
 * OVERFLOW SAFETY:
 * ================
 * All integer calculations use safe_size_mul() and overflow checking
 * to prevent buffer overflows from malicious or malformed packets.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "common.h"
#include "util/endian.h"

/** @name Frame Decoding Functions
 * @{
 * @ingroup packet_parsing
 * @brief Unified frame data decoding for both compressed and uncompressed formats
 */

/**
 * @brief Decode frame data (malloc version for client handlers)
 *
 * Handles both compressed (zstd) and uncompressed frame formats.
 * Allocates buffer using SAFE_MALLOC that caller must free.
 * Validates sizes to prevent memory exhaustion attacks.
 *
 * Frame formats supported:
 * - Uncompressed: Raw pixel/text data at expected_size bytes
 * - Compressed: zstd-compressed data, decompresses to expected_size bytes
 *
 * SIZE VALIDATION:
 * - Validates original_size <= 100MB (prevents excessive allocation)
 * - Validates expected_size == uncompressed size
 * - Validates compressed_size <= frame_data_len (if compressed)
 *
 * ERROR HANDLING:
 * - Returns NULL on allocation failure
 * - Returns NULL on decompression failure
 * - Returns NULL on size validation failure
 * - Sets asciichat_errno for detailed error reporting
 *
 * @param frame_data_ptr Pointer to frame data (compressed or uncompressed)
 * @param frame_data_len Actual size of frame_data_ptr buffer
 * @param is_compressed True if data is zstd-compressed, false if raw
 * @param original_size Expected decompressed/uncompressed size in bytes
 * @param compressed_size Expected compressed size (validated only if is_compressed=true)
 *
 * @return Allocated buffer with decoded data (caller must SAFE_FREE), or NULL on error
 *
 * @note Caller is responsible for freeing returned buffer with SAFE_FREE()
 * @note Added null terminator to decoded data (buffer is original_size + 1)
 * @note Used by client handlers for server->client frame packets
 *
 * @see packet_decode_frame_data_buffer() For fixed-size buffer variant
 * @ingroup packet_parsing
 */
char *packet_decode_frame_data_malloc(const char *frame_data_ptr, size_t frame_data_len, bool is_compressed,
                                      uint32_t original_size, uint32_t compressed_size);

/**
 * @brief Decode frame data (fixed buffer version for server handlers)
 *
 * Decodes frame data into a pre-allocated fixed-size buffer.
 * Used when buffer allocation is managed separately (e.g., ring buffers).
 *
 * @param frame_data_ptr Pointer to frame data (compressed or uncompressed)
 * @param frame_data_len Actual size of frame_data_ptr buffer
 * @param is_compressed True if data is zstd-compressed
 * @param output_buffer Pre-allocated output buffer
 * @param output_size Size of output_buffer in bytes
 * @param original_size Expected decompressed/uncompressed size
 * @param compressed_size Expected compressed size (if is_compressed=true)
 *
 * @return ASCIICHAT_OK on success, error code on failure
 *         Errors set asciichat_errno with context message
 *
 * @note Used by server handlers for client->server IMAGE_FRAME packets
 * @see packet_decode_frame_data_malloc() For malloc version
 * @ingroup packet_parsing
 */
asciichat_error_t packet_decode_frame_data_buffer(const char *frame_data_ptr, size_t frame_data_len, bool is_compressed,
                                                   void *output_buffer, size_t output_size, uint32_t original_size,
                                                   uint32_t compressed_size);

/** @} */

/** @name Frame Dimension Validation
 * @{
 * @ingroup packet_parsing
 * @brief Validation helpers for frame dimensions with overflow checking
 */

/**
 * @brief Validate frame dimensions and calculate RGB buffer size
 *
 * Performs comprehensive validation of frame dimensions:
 * 1. Checks width and height are non-zero and reasonable (< 32768)
 * 2. Calculates RGB buffer size (width * height * 3) with overflow checking
 * 3. Validates result size doesn't exceed max image size (256MB)
 *
 * OVERFLOW PROTECTION:
 * - Uses safe_size_mul() for dimension multiplication
 * - Checks for integer overflow at each step
 * - Returns error instead of wrapping around
 *
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param out_rgb_size Output parameter: calculated RGB size (width*height*3)
 *
 * @return ASCIICHAT_OK on success, error code on failure:
 *         - ERROR_INVALID_STATE: width or height is zero
 *         - ERROR_INVALID_STATE: dimensions exceed max (32768)
 *         - ERROR_MEMORY: Calculated size exceeds max (256MB)
 *         - ERROR_MEMORY: Integer overflow in multiplication
 *
 * @note Critical for preventing integer overflow attacks
 * @note Used by both server and client frame handlers
 * @ingroup packet_parsing
 */
asciichat_error_t packet_validate_frame_dimensions(uint32_t width, uint32_t height, size_t *out_rgb_size);

/** @} */

/** @name Audio Batch Header Parsing
 * @{
 * @ingroup packet_parsing
 * @brief Helpers for parsing audio batch packet headers
 */

/**
 * @brief Audio batch header information extracted from packet
 *
 * Results from parsing an audio batch packet header.
 * Used by both server and client handlers.
 *
 * @ingroup packet_parsing
 */
typedef struct {
  /** @brief Number of sample chunks in this batch */
  uint32_t batch_count;
  /** @brief Total number of float samples in batch */
  uint32_t total_samples;
  /** @brief Sample rate (e.g., 44100, 48000) */
  uint32_t sample_rate;
  /** @brief Number of audio channels (usually 1 for mono) */
  uint32_t channels;
} audio_batch_info_t;

/**
 * @brief Parse audio batch packet header
 *
 * Extracts and validates audio batch header from packet payload.
 * Converts from network byte order to host byte order.
 *
 * PACKET FORMAT:
 * - audio_batch_packet_t header (16 bytes)
 * - Float samples[total_samples] (4 bytes each)
 *
 * VALIDATION PERFORMED:
 * - Packet size >= sizeof(audio_batch_packet_t)
 * - batch_count > 0
 * - total_samples > 0
 * - sample_rate is reasonable (8000-192000 Hz)
 * - channels is 1-8
 *
 * @param data Packet payload starting with audio_batch_packet_t
 * @param len Total packet length in bytes
 * @param out_batch Output parameter: parsed batch info
 *
 * @return ASCIICHAT_OK on success, error code on failure
 *         Errors set asciichat_errno with context
 *
 * @note Used by both server and client batch handlers
 * @ingroup packet_parsing
 */
asciichat_error_t packet_parse_audio_batch_header(const void *data, size_t len, audio_batch_info_t *out_batch);

/** @} */

/** @name Generic Payload Validation
 * @{
 * @ingroup packet_parsing
 * @brief Generic helpers for common payload validation patterns
 */

/**
 * @brief Convert network byte order uint32_t to host order with validation
 *
 * Simple wrapper that combines memcpy (for unaligned access) and
 * NET_TO_HOST_U32 conversion in one step.
 *
 * @param src Source pointer (can be unaligned)
 * @param out_value Output parameter for host byte order value
 *
 * @note Handles unaligned memory access safely
 * @ingroup packet_parsing
 */
static inline void packet_read_u32_net(const void *src, uint32_t *out_value) {
  uint32_t net_value;
  memcpy(&net_value, src, sizeof(uint32_t));
  *out_value = NET_TO_HOST_U32(net_value);
}

/**
 * @brief Convert network byte order uint16_t to host order with validation
 *
 * Simple wrapper that combines memcpy (for unaligned access) and
 * NET_TO_HOST_U16 conversion in one step.
 *
 * @param src Source pointer (can be unaligned)
 * @param out_value Output parameter for host byte order value
 *
 * @note Handles unaligned memory access safely
 * @ingroup packet_parsing
 */
static inline void packet_read_u16_net(const void *src, uint16_t *out_value) {
  uint16_t net_value;
  memcpy(&net_value, src, sizeof(uint16_t));
  *out_value = NET_TO_HOST_U16(net_value);
}

/** @} */

/** @} */
