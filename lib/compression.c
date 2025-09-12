// compression.c - Pure compression/decompression utilities
// Network functions have been moved to network.c

#include <zlib.h>
#include <string.h>
#include "compression.h"
#include "common.h"

// Compress data using zlib deflate
int compress_data(const void *input, size_t input_size, void **output, size_t *output_size) {
  if (!input || input_size == 0 || !output || !output_size) {
    return -1;
  }

  // Calculate maximum compressed size
  uLongf compressed_size = compressBound(input_size);
  unsigned char *compressed_data;
  SAFE_MALLOC(compressed_data, compressed_size, unsigned char *);
  
  if (!compressed_data) {
    return -1;
  }

  // Compress using zlib
  int ret = compress2(compressed_data, &compressed_size, (const Bytef *)input, input_size, Z_DEFAULT_COMPRESSION);
  
  if (ret != Z_OK) {
    free(compressed_data);
    return -1;
  }

  *output = compressed_data;
  *output_size = compressed_size;
  
  return 0;
}

// Decompress data using zlib inflate
int decompress_data(const void *input, size_t input_size, void *output, size_t output_size) {
  if (!input || input_size == 0 || !output || output_size == 0) {
    return -1;
  }

  uLongf dest_len = output_size;
  int ret = uncompress((Bytef *)output, &dest_len, (const Bytef *)input, input_size);
  
  return (ret == Z_OK) ? 0 : -1;
}

// Check if compression is worthwhile based on ratio
bool should_compress(size_t original_size, size_t compressed_size) {
  if (original_size == 0) return false;
  
  float ratio = (float)compressed_size / original_size;
  return ratio < COMPRESSION_RATIO_THRESHOLD;
}