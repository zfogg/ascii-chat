/**
 * @file network/packet_parsing.c
 * @ingroup packet_parsing
 * @brief 📡 Shared packet parsing utilities implementation
 *
 * Provides reusable packet parsing functions used by both server and client
 * protocol handlers to eliminate code duplication and ensure consistency.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include "packet_parsing.h"
#include "network/compression.h"
#include "util/bytes.h"
#include "util/format.h"
#include "util/endian.h"
#include "logging.h"
#include <string.h>

/**
 * @brief Maximum frame size (256MB) - prevents memory exhaustion attacks
 * @ingroup packet_parsing
 */
#define PACKET_MAX_FRAME_SIZE (256 * 1024 * 1024)

/**
 * @brief Maximum frame dimension (32768x32768) - prevents overflow
 * @ingroup packet_parsing
 */
#define PACKET_MAX_DIMENSION 32768

char *packet_decode_frame_data_malloc(const char *frame_data_ptr, size_t frame_data_len, bool is_compressed,
                                      uint32_t original_size, uint32_t compressed_size) {
  // Validate size before allocation to prevent excessive memory usage
  if (original_size > PACKET_MAX_FRAME_SIZE) {
    char size_str[32];
    format_bytes_pretty(original_size, size_str, sizeof(size_str));
    SET_ERRNO(ERROR_NETWORK_SIZE, "Frame size exceeds maximum: %s (max %d MB)", size_str,
              PACKET_MAX_FRAME_SIZE / (1024 * 1024));
    return NULL;
  }

  // Allocate buffer with extra byte for null terminator
  char *frame_data = SAFE_MALLOC(original_size + 1, char *);
  if (!frame_data) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate %u bytes for frame decode", original_size);
    return NULL;
  }

  if (is_compressed) {
    // Validate compressed frame size
    if (frame_data_len != compressed_size) {
      SET_ERRNO(ERROR_NETWORK_SIZE, "Compressed frame size mismatch: expected %u, got %zu", compressed_size,
                frame_data_len);
      SAFE_FREE(frame_data);
      return NULL;
    }

    // Decompress using compression API
    int result = decompress_data(frame_data_ptr, frame_data_len, frame_data, original_size);

    if (result != 0) {
      SET_ERRNO(ERROR_COMPRESSION, "Decompression failed for expected size %u", original_size);
      SAFE_FREE(frame_data);
      return NULL;
    }

    log_debug("Decompressed frame: %zu -> %u bytes", frame_data_len, original_size);
  } else {
    // Uncompressed frame - validate size
    if (frame_data_len != original_size) {
      log_error("Uncompressed frame size mismatch: expected %u, got %zu", original_size, frame_data_len);
      SAFE_FREE(frame_data);
      return NULL;
    }

    // Only copy the actual amount of data we received
    size_t copy_size = (frame_data_len > original_size) ? original_size : frame_data_len;
    memcpy(frame_data, frame_data_ptr, copy_size);
  }

  // Null-terminate the frame data
  frame_data[original_size] = '\0';
  return frame_data;
}

asciichat_error_t packet_decode_frame_data_buffer(const char *frame_data_ptr, size_t frame_data_len, bool is_compressed,
                                                  void *output_buffer, size_t output_size, uint32_t original_size,
                                                  uint32_t compressed_size) {
  if (!frame_data_ptr || !output_buffer) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL pointer in frame decode");
  }

  if (output_size < original_size) {
    return SET_ERRNO(ERROR_BUFFER_FULL, "Output buffer too small: %zu < %u", output_size, original_size);
  }

  if (is_compressed) {
    // Validate compressed frame size
    if (frame_data_len != compressed_size) {
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Compressed frame size mismatch: expected %u, got %zu", compressed_size,
                       frame_data_len);
    }

    // Decompress using compression API
    int result = decompress_data(frame_data_ptr, frame_data_len, output_buffer, original_size);

    if (result != 0) {
      return SET_ERRNO(ERROR_COMPRESSION, "Decompression failed for expected size %u", original_size);
    }

    log_debug("Decompressed frame to buffer: %zu -> %u bytes", frame_data_len, original_size);
  } else {
    // Uncompressed frame - validate size
    if (frame_data_len != original_size) {
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Uncompressed frame size mismatch: expected %u, got %zu", original_size,
                       frame_data_len);
    }

    // Copy data to output buffer
    memcpy(output_buffer, frame_data_ptr, original_size);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t packet_validate_frame_dimensions(uint32_t width, uint32_t height, size_t *out_rgb_size) {
  if (!out_rgb_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "out_rgb_size pointer is NULL");
  }

  // Check dimensions are non-zero
  if (width == 0 || height == 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Frame dimensions cannot be zero: %ux%u", width, height);
  }

  // Check dimensions are within reasonable bounds
  if (width > PACKET_MAX_DIMENSION || height > PACKET_MAX_DIMENSION) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Frame dimensions exceed maximum: %ux%u (max %u)", width, height,
                     PACKET_MAX_DIMENSION);
  }

  // Calculate RGB buffer size with overflow checking (width * height * 3 bytes per pixel)
  size_t pixel_count = 0;
  if (safe_size_mul(width, height, &pixel_count) != 0) {
    return SET_ERRNO(ERROR_MEMORY, "Frame dimension multiplication overflow: %u * %u", width, height);
  }

  size_t rgb_size = 0;
  if (safe_size_mul(pixel_count, 3, &rgb_size) != 0) {
    return SET_ERRNO(ERROR_MEMORY, "RGB buffer size overflow: %zu * 3", pixel_count);
  }

  // Validate final buffer size against maximum
  if (rgb_size > PACKET_MAX_FRAME_SIZE) {
    char size_str[32];
    format_bytes_pretty(rgb_size, size_str, sizeof(size_str));
    return SET_ERRNO(ERROR_MEMORY, "Frame buffer size exceeds maximum: %s (max %d MB)", size_str,
                     PACKET_MAX_FRAME_SIZE / (1024 * 1024));
  }

  *out_rgb_size = rgb_size;
  return ASCIICHAT_OK;
}

asciichat_error_t packet_parse_audio_batch_header(const void *data, size_t len, audio_batch_info_t *out_batch) {
  if (!data || !out_batch) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL pointer in audio batch header parse");
  }

  // Forward declare audio_batch_packet_t structure
  // We can't include audio headers here due to circular dependencies
  // Instead we define the layout directly for parsing
  const size_t AUDIO_BATCH_HEADER_SIZE = 16; // sizeof(audio_batch_packet_t)

  if (len < AUDIO_BATCH_HEADER_SIZE) {
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Audio batch header too small: %zu (need %zu)", len, AUDIO_BATCH_HEADER_SIZE);
  }

  // Parse header fields (all in network byte order)
  const uint8_t *header = (const uint8_t *)data;

  uint32_t batch_count_net, total_samples_net, sample_rate_net, channels_net;

  // Read with memcpy to handle unaligned access
  memcpy(&batch_count_net, header + 0, sizeof(uint32_t));
  memcpy(&total_samples_net, header + 4, sizeof(uint32_t));
  memcpy(&sample_rate_net, header + 8, sizeof(uint32_t));
  memcpy(&channels_net, header + 12, sizeof(uint32_t));

  // Convert from network byte order
  out_batch->batch_count = NET_TO_HOST_U32(batch_count_net);
  out_batch->total_samples = NET_TO_HOST_U32(total_samples_net);
  out_batch->sample_rate = NET_TO_HOST_U32(sample_rate_net);
  out_batch->channels = NET_TO_HOST_U32(channels_net);

  // Validate header values
  if (out_batch->batch_count == 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Audio batch count cannot be zero");
  }

  if (out_batch->total_samples == 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Audio batch total_samples cannot be zero");
  }

  if (out_batch->sample_rate < 8000 || out_batch->sample_rate > 192000) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Audio batch sample_rate out of range: %u (valid: 8000-192000)",
                     out_batch->sample_rate);
  }

  if (out_batch->channels < 1 || out_batch->channels > 8) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Audio batch channels out of range: %u (valid: 1-8)", out_batch->channels);
  }

  return ASCIICHAT_OK;
}
