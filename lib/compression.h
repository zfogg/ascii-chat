#pragma once

#include <stdlib.h>
#include <stdbool.h>

// Compression settings
#define COMPRESSION_RATIO_THRESHOLD 0.8f // Only use compression if <80% original size

// Pure compression/decompression utilities
int compress_data(const void *input, size_t input_size, void **output, size_t *output_size);
int decompress_data(const void *input, size_t input_size, void *output, size_t output_size);
bool should_compress(size_t original_size, size_t compressed_size);

// Note: Frame sending functions have been moved to network.h/network.c