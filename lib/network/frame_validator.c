/**
 * @file network/frame_validator.c
 * @brief Frame validation implementation for IMAGE_FRAME packets
 * @ingroup network
 */

#include "frame_validator.h"
#include "network.h"
#include "asciichat_errno.h"
#include "util/format.h"
#include "util/endian.h"
#include "video/image.h"
#include <string.h>

asciichat_error_t frame_check_size_overflow(size_t header_size, size_t data_size) {
  if (data_size > SIZE_MAX - header_size) {
    char size_str[32];
    format_bytes_pretty(data_size, size_str, sizeof(size_str));
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Frame size overflow: %s", size_str);
    return ERROR_BUFFER_OVERFLOW;
  }
  return ASCIICHAT_OK;
}

asciichat_error_t frame_validate_legacy(size_t len, size_t expected_rgb_size) {
  // Check minimum header size
  if (len < FRAME_HEADER_SIZE_LEGACY) {
    SET_ERRNO(ERROR_INVALID_FRAME, "Legacy frame header too small: %zu bytes", len);
    return ERROR_INVALID_FRAME;
  }

  // Check for overflow
  asciichat_error_t err = frame_check_size_overflow(FRAME_HEADER_SIZE_LEGACY, expected_rgb_size);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  size_t expected_total = FRAME_HEADER_SIZE_LEGACY + expected_rgb_size;
  if (len != expected_total) {
    SET_ERRNO(ERROR_INVALID_FRAME, "Legacy frame length mismatch: expected %zu got %zu", expected_total, len);
    return ERROR_INVALID_FRAME;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t frame_validate_new(void *data, size_t len, bool *out_compressed, uint32_t *out_data_size) {
  // Check minimum new format header size
  if (len < FRAME_HEADER_SIZE_NEW) {
    SET_ERRNO(ERROR_INVALID_FRAME, "New frame header too small: %zu bytes", len);
    return ERROR_INVALID_FRAME;
  }

  uint32_t compressed_flag, data_size;
  frame_extract_new_header(data, &compressed_flag, &data_size);

  size_t data_size_sz = (size_t)data_size;

  // Check data size against maximum
  if (data_size_sz > IMAGE_MAX_PIXELS_SIZE) {
    char size_str[32];
    format_bytes_pretty(data_size_sz, size_str, sizeof(size_str));
    SET_ERRNO(ERROR_INVALID_FRAME, "Frame data too large: %s", size_str);
    return ERROR_INVALID_FRAME;
  }

  // Check for overflow
  asciichat_error_t err = frame_check_size_overflow(FRAME_HEADER_SIZE_NEW, data_size_sz);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  size_t expected_total = FRAME_HEADER_SIZE_NEW + data_size_sz;
  if (len != expected_total) {
    SET_ERRNO(ERROR_INVALID_FRAME, "New frame length mismatch: expected %zu got %zu", expected_total, len);
    return ERROR_INVALID_FRAME;
  }

  if (out_compressed)
    *out_compressed = (compressed_flag != 0);
  if (out_data_size)
    *out_data_size = data_size;
  return ASCIICHAT_OK;
}

void frame_extract_dimensions(const void *data, uint32_t *width, uint32_t *height) {
  uint32_t width_net, height_net;
  memcpy(&width_net, data, FRAME_HEADER_FIELD_SIZE);
  memcpy(&height_net, (char *)data + FRAME_HEADER_FIELD_SIZE, FRAME_HEADER_FIELD_SIZE);
  *width = NET_TO_HOST_U32(width_net);
  *height = NET_TO_HOST_U32(height_net);
}

void frame_extract_new_header(const void *data, uint32_t *compressed, uint32_t *data_size) {
  uint32_t compressed_net, size_net;
  memcpy(&compressed_net, (char *)data + FRAME_HEADER_FIELD_SIZE * 2, FRAME_HEADER_FIELD_SIZE);
  memcpy(&size_net, (char *)data + FRAME_HEADER_FIELD_SIZE * 3, FRAME_HEADER_FIELD_SIZE);
  *compressed = NET_TO_HOST_U32(compressed_net);
  *data_size = NET_TO_HOST_U32(size_net);
}
