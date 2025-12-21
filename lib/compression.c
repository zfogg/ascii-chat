/**
 * @file compression.c
 * @ingroup compression
 * @brief 🗜️ Fast zstd compression/decompression utilities for network payload optimization
 */

#include <zstd.h>
#include <string.h>
#include "compression.h"
#include "common.h"
#include "asciichat_errno.h" // For asciichat_errno system

// Compress data using zstd with configurable compression level
int compress_data(const void *input, size_t input_size, void **output, size_t *output_size, int compression_level) {
  if (!input || input_size == 0 || !output || !output_size) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: input=%p, input_size=%zu, output=%p, output_size=%p", input,
              input_size, output, output_size);
    return -1;
  }

  // Validate compression level (1-9 for real-time streaming)
  if (compression_level < 1 || compression_level > 9) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid compression level %d: must be between 1 and 9", compression_level);
    return -1;
  }

  // Calculate maximum compressed size
  size_t compressed_size = ZSTD_compressBound(input_size);
  unsigned char *compressed_data = NULL;
  compressed_data = SAFE_MALLOC(compressed_size, unsigned char *);

  if (!compressed_data) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate compressed data buffer");
    return -1;
  }

  // Compress using zstd with user-specified compression level
  size_t ret = ZSTD_compress(compressed_data, compressed_size, input, input_size, compression_level);

  if (ZSTD_isError(ret)) {
    SET_ERRNO(ERROR_GENERAL, "zstd compression failed: %s", ZSTD_getErrorName(ret));
    SAFE_FREE(compressed_data);
    return -1;
  }

  *output = compressed_data;
  *output_size = ret;

  return 0;
}

// Decompress data using zstd
int decompress_data(const void *input, size_t input_size, void *output, size_t output_size) {
  if (!input || input_size == 0 || !output || output_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: input=%p, input_size=%zu, output=%p, output_size=%zu", input,
              input_size, output, output_size);
    return -1;
  }

  size_t ret = ZSTD_decompress(output, output_size, input, input_size);

  if (ZSTD_isError(ret)) {
    SET_ERRNO(ERROR_GENERAL, "zstd decompression failed: %s", ZSTD_getErrorName(ret));
    return -1;
  }

  return 0;
}

// Check if compression is worthwhile based on ratio
bool should_compress(size_t original_size, size_t compressed_size) {
  if (original_size == 0)
    return false;

  float ratio = (float)compressed_size / (float)original_size;
  return ratio < COMPRESSION_RATIO_THRESHOLD;
}
