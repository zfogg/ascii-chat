/**
 * @file network/frame_validator.h
 * @brief Frame validation utilities for IMAGE_FRAME packets
 * @ingroup network
 */

#pragma once

#include "common.h"
#include <stddef.h>
#include <stdbool.h>

/* Frame format constants */

/** @brief Legacy frame header size (width:4 + height:4) */
#define FRAME_HEADER_SIZE_LEGACY 8

/** @brief New frame header size (width:4 + height:4 + compressed:4 + size:4) */
#define FRAME_HEADER_SIZE_NEW 16

/** @brief Size of each header field (uint32_t = 4 bytes) */
#define FRAME_HEADER_FIELD_SIZE 4

/**
 * @brief Validate legacy frame format
 * @param data Packet data
 * @param len Total packet length
 * @param expected_rgb_size Expected RGB data size (width * height * 3)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup network
 */
asciichat_error_t frame_validate_legacy(void *data, size_t len, size_t expected_rgb_size);

/**
 * @brief Validate new frame format with optional compression
 * @param data Packet data
 * @param len Total packet length
 * @param expected_rgb_size Expected decompressed RGB size
 * @param out_compressed Output: whether data is compressed (optional)
 * @param out_data_size Output: size of compressed/uncompressed data (optional)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup network
 */
asciichat_error_t frame_validate_new(void *data, size_t len, size_t expected_rgb_size,
                                     bool *out_compressed, uint32_t *out_data_size);

/**
 * @brief Check for overflow when adding header to data
 * @param header_size Header size in bytes
 * @param data_size Data size in bytes
 * @return ASCIICHAT_OK if safe, ERROR_BUFFER_OVERFLOW if would overflow
 *
 * @ingroup network
 */
asciichat_error_t frame_check_size_overflow(size_t header_size, size_t data_size);

/**
 * @brief Extract width and height from frame header
 * @param data Packet data (must be at least 8 bytes)
 * @param width Output: frame width
 * @param height Output: frame height
 *
 * @ingroup network
 */
void frame_extract_dimensions(const void *data, uint32_t *width, uint32_t *height);

/**
 * @brief Extract compressed flag and data size from new format header
 * @param data Packet data (must be at least 16 bytes)
 * @param compressed Output: whether data is compressed
 * @param data_size Output: size of data field
 *
 * @ingroup network
 */
void frame_extract_new_header(const void *data, uint32_t *compressed, uint32_t *data_size);

/** @} */
