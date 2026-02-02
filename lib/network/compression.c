/**
 * @file compression.c
 * @ingroup compression
 * @brief 🗜️ Fast zstd compression/decompression utilities for network payload optimization
 */

#include <zstd.h>
#include <string.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h> // For asciichat_errno system

// Compress data using zstd with configurable compression level
asciichat_error_t compress_data(const void *input, size_t input_size, void **output, size_t *output_size,
                                int compression_level) {
  if (!input || input_size == 0 || !output || !output_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: input=%p, input_size=%zu, output=%p, output_size=%p",
                     input, input_size, output, output_size);
  }

  // Validate compression level (1-9 for real-time streaming)
  if (compression_level < 1 || compression_level > 9) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid compression level %d: must be between 1 and 9", compression_level);
  }

  // Calculate maximum compressed size
  size_t compressed_size = ZSTD_compressBound(input_size);

  // Sanity check ZSTD_compressBound result
  // ZSTD_compressBound returns 0 for errors or very large values for huge inputs
  if (compressed_size == 0 || compressed_size > 256 * 1024 * 1024) { // Max 256MB compressed buffer
    return SET_ERRNO(ERROR_INVALID_PARAM, "ZSTD_compressBound returned unreasonable size: %zu for input %zu",
                     compressed_size, input_size);
  }

  unsigned char *compressed_data = NULL;
  compressed_data = SAFE_MALLOC(compressed_size, unsigned char *);

  if (!compressed_data) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate compressed data buffer");
  }

  // Compress using zstd with user-specified compression level
  size_t ret = ZSTD_compress(compressed_data, compressed_size, input, input_size, compression_level);

  if (ZSTD_isError(ret)) {
    SAFE_FREE(compressed_data);
    return SET_ERRNO(ERROR_GENERAL, "zstd compression failed: %s", ZSTD_getErrorName(ret));
  }

  *output = compressed_data;
  *output_size = ret;

  return ASCIICHAT_OK;
}

// Decompress data using zstd
asciichat_error_t decompress_data(const void *input, size_t input_size, void *output, size_t output_size) {
  if (!input || input_size == 0 || !output || output_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: input=%p, input_size=%zu, output=%p, output_size=%zu",
                     input, input_size, output, output_size);
  }

  size_t ret = ZSTD_decompress(output, output_size, input, input_size);

  if (ZSTD_isError(ret)) {
    return SET_ERRNO(ERROR_GENERAL, "zstd decompression failed: %s", ZSTD_getErrorName(ret));
  }

  return ASCIICHAT_OK;
}

// Check if compression is worthwhile based on ratio
bool should_compress(size_t original_size, size_t compressed_size) {
  if (original_size == 0)
    return false;

  float ratio = (float)compressed_size / (float)original_size;
  return ratio < COMPRESSION_RATIO_THRESHOLD;
}
