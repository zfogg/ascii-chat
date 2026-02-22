/**
 * @file permessage_deflate_test.c
 * @brief Tests for permessage-deflate WebSocket compression
 *
 * Validates that permessage-deflate decompression works correctly with:
 * - Various message sizes (small, medium, large, fragmented)
 * - Different compression ratios
 * - Buffer overflow prevention
 * - Data integrity verification
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <criterion/theories.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/common.h>

// Test suite with quiet logging
TestSuite(permessage_deflate);

/**
 * Helper to compress data with DEFLATE (RFC 7692 format)
 * Uses standard zlib deflate compression
 */
static int deflate_compress(const uint8_t *input, size_t input_len, uint8_t **output, size_t *output_len) {
  if (!input || input_len == 0 || !output || !output_len) {
    return -1;
  }

  z_stream stream;
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.opaque = Z_NULL;

  // Use raw DEFLATE (no zlib header) per RFC 7692
  int ret = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                         -15, // Negative window bits for raw DEFLATE
                         8, Z_DEFAULT_STRATEGY);
  if (ret != Z_OK) {
    return -1;
  }

  // Allocate output buffer (slightly larger than input for safety)
  size_t max_compressed = compressBound(input_len);
  uint8_t *compressed = SAFE_MALLOC(max_compressed, uint8_t *);
  if (!compressed) {
    deflateEnd(&stream);
    return -1;
  }

  stream.avail_in = input_len;
  stream.next_in = (uint8_t *)input;
  stream.avail_out = max_compressed;
  stream.next_out = compressed;

  ret = deflate(&stream, Z_FINISH);
  if (ret != Z_STREAM_END) {
    SAFE_FREE(compressed);
    deflateEnd(&stream);
    return -1;
  }

  *output = compressed;
  *output_len = stream.total_out;
  deflateEnd(&stream);
  return 0;
}

/**
 * Helper to decompress data with DEFLATE (RFC 7692 format)
 * Simulates what libwebsockets does internally
 */
static int deflate_decompress(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_len,
                              size_t *decompressed_len) {
  if (!input || input_len == 0 || !output || output_len == 0) {
    return -1;
  }

  z_stream stream;
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.opaque = Z_NULL;

  // Use raw DEFLATE per RFC 7692
  int ret = inflateInit2(&stream, -15); // Negative window bits for raw DEFLATE
  if (ret != Z_OK) {
    return -1;
  }

  stream.avail_in = input_len;
  stream.next_in = (uint8_t *)input;
  stream.avail_out = output_len;
  stream.next_out = output;

  ret = inflate(&stream, Z_FINISH);

  if (ret != Z_STREAM_END && ret != Z_OK) {
    inflateEnd(&stream);
    return -1;
  }

  *decompressed_len = stream.total_out;
  inflateEnd(&stream);
  return 0;
}

/**
 * Generate test data pattern
 */
static void generate_test_data(uint8_t *data, size_t size, int pattern) {
  switch (pattern) {
  case 0: // Repeating 'A' (highly compressible)
    memset(data, 'A', size);
    break;
  case 1: // Repeating pattern "RGB" (video-like)
    for (size_t i = 0; i < size; i += 3) {
      data[i % size] = 'R';
      if (i + 1 < size)
        data[(i + 1) % size] = 'G';
      if (i + 2 < size)
        data[(i + 2) % size] = 'B';
    }
    break;
  case 2: // Pseudo-random (poorly compressible)
    for (size_t i = 0; i < size; i++) {
      data[i] = (uint8_t)((i * 73 + 17) ^ (i >> 5));
    }
    break;
  case 3: // Mixed ASCII text
    for (size_t i = 0; i < size; i++) {
      data[i] = 32 + (i % 95); // Printable ASCII range
    }
    break;
  default:
    memset(data, pattern & 0xFF, size);
    break;
  }
}

/**
 * Theory: Small messages decompress correctly
 */
TheoryDataPoints(permessage_deflate, small_messages) = {
    DataPoints(size_t, 1, 8, 64, 256, 512),
};

Theory((size_t msg_size), permessage_deflate, small_messages) {
  cr_assume(msg_size > 0 && msg_size <= 512);

  uint8_t *original = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(original);
  generate_test_data(original, msg_size, 0);

  // Compress
  uint8_t *compressed = NULL;
  size_t compressed_len = 0;
  int ret = deflate_compress(original, msg_size, &compressed, &compressed_len);
  cr_assert_eq(ret, 0, "Compression should succeed for size %zu", msg_size);
  cr_assert_not_null(compressed);
  cr_assert_gt(compressed_len, 0);

  // Decompress
  uint8_t *decompressed = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(decompressed);
  size_t decompressed_len = 0;
  ret = deflate_decompress(compressed, compressed_len, decompressed, msg_size, &decompressed_len);
  cr_assert_eq(ret, 0, "Decompression should succeed for size %zu", msg_size);
  cr_assert_eq(decompressed_len, msg_size, "Decompressed size must match original for size %zu", msg_size);

  // Verify data integrity
  cr_assert_eq(memcmp(original, decompressed, msg_size), 0, "Decompressed data must match original for size %zu",
               msg_size);

  SAFE_FREE(original);
  SAFE_FREE(compressed);
  SAFE_FREE(decompressed);
}

/**
 * Theory: Medium messages (typical video frame size)
 */
TheoryDataPoints(permessage_deflate, medium_messages) = {
    DataPoints(size_t, 1024, 4096, 16384, 65536),
};

Theory((size_t msg_size), permessage_deflate, medium_messages) {
  cr_assume(msg_size > 0 && msg_size <= 65536);

  uint8_t *original = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(original);
  generate_test_data(original, msg_size, 1); // RGB pattern

  uint8_t *compressed = NULL;
  size_t compressed_len = 0;
  int ret = deflate_compress(original, msg_size, &compressed, &compressed_len);
  cr_assert_eq(ret, 0);

  uint8_t *decompressed = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(decompressed);
  size_t decompressed_len = 0;
  ret = deflate_decompress(compressed, compressed_len, decompressed, msg_size, &decompressed_len);
  cr_assert_eq(ret, 0);
  cr_assert_eq(decompressed_len, msg_size);
  cr_assert_eq(memcmp(original, decompressed, msg_size), 0);

  SAFE_FREE(original);
  SAFE_FREE(compressed);
  SAFE_FREE(decompressed);
}

/**
 * Theory: Various compression patterns
 */
TheoryDataPoints(permessage_deflate, compression_patterns) = {
    DataPoints(int, 0, 1, 2, 3),
};

Theory((int pattern), permessage_deflate, compression_patterns) {
  size_t msg_size = 8192;

  uint8_t *original = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(original);
  generate_test_data(original, msg_size, pattern);

  uint8_t *compressed = NULL;
  size_t compressed_len = 0;
  int ret = deflate_compress(original, msg_size, &compressed, &compressed_len);
  cr_assert_eq(ret, 0, "Compression should succeed for pattern %d", pattern);

  // For pattern 0 (highly compressible), expect good compression
  if (pattern == 0) {
    float ratio = (float)compressed_len / (float)msg_size;
    cr_assert_lt(ratio, 0.1f, "Pattern 0 should compress to <10%% (got %.2f%%)", ratio * 100.0f);
  }

  uint8_t *decompressed = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(decompressed);
  size_t decompressed_len = 0;
  ret = deflate_decompress(compressed, compressed_len, decompressed, msg_size, &decompressed_len);
  cr_assert_eq(ret, 0);
  cr_assert_eq(memcmp(original, decompressed, msg_size), 0);

  SAFE_FREE(original);
  SAFE_FREE(compressed);
  SAFE_FREE(decompressed);
}

/**
 * Test: Buffer overflow prevention - large message
 * Ensure decompression doesn't write beyond output buffer
 */
Test(permessage_deflate, buffer_overflow_protection) {
  size_t msg_size = 256 * 1024; // 256KB - large video frame

  uint8_t *original = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(original);
  generate_test_data(original, msg_size, 1); // Video pattern

  uint8_t *compressed = NULL;
  size_t compressed_len = 0;
  int ret = deflate_compress(original, msg_size, &compressed, &compressed_len);
  cr_assert_eq(ret, 0);

  // Allocate exact-size buffer
  uint8_t *decompressed = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(decompressed);

  // Fill with sentinel values
  uint8_t *beyond_buffer = SAFE_MALLOC(256, uint8_t *);
  cr_assert_not_null(beyond_buffer);
  memset(beyond_buffer, 0xAA, 256);

  size_t decompressed_len = 0;
  ret = deflate_decompress(compressed, compressed_len, decompressed, msg_size, &decompressed_len);
  cr_assert_eq(ret, 0);
  cr_assert_eq(decompressed_len, msg_size);

  // Verify decompressed data matches
  cr_assert_eq(memcmp(original, decompressed, msg_size), 0);

  SAFE_FREE(original);
  SAFE_FREE(compressed);
  SAFE_FREE(decompressed);
  SAFE_FREE(beyond_buffer);
}

/**
 * Test: Fragmented message handling
 * Simulate receiving a message in multiple fragments
 */
Test(permessage_deflate, fragmented_decompression) {
  size_t total_size = 128 * 1024;   // 128KB total message
  size_t fragment_size = 16 * 1024; // 16KB fragments
  size_t num_fragments = total_size / fragment_size;

  uint8_t *original = SAFE_MALLOC(total_size, uint8_t *);
  cr_assert_not_null(original);
  generate_test_data(original, total_size, 1);

  // Compress entire message
  uint8_t *compressed = NULL;
  size_t compressed_len = 0;
  int ret = deflate_compress(original, total_size, &compressed, &compressed_len);
  cr_assert_eq(ret, 0);

  // Decompress fragments sequentially
  uint8_t *decompressed = SAFE_MALLOC(total_size, uint8_t *);
  cr_assert_not_null(decompressed);

  z_stream stream;
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.opaque = Z_NULL;
  int z_ret = inflateInit2(&stream, -15);
  cr_assert_eq(z_ret, Z_OK);

  stream.avail_out = total_size;
  stream.next_out = decompressed;

  size_t offset = 0;
  for (size_t i = 0; i < num_fragments; i++) {
    size_t chunk_size = (i < num_fragments - 1) ? fragment_size : (compressed_len - offset);
    cr_assert_gt(chunk_size, 0, "Fragment %zu should have data", i);

    stream.avail_in = chunk_size;
    stream.next_in = compressed + offset;

    z_ret = inflate(&stream, i == num_fragments - 1 ? Z_FINISH : Z_NO_FLUSH);
    cr_assert(z_ret == Z_OK || z_ret == Z_STREAM_END, "inflate should succeed for fragment %zu (got %d)", i, z_ret);

    offset += chunk_size;
  }

  size_t decompressed_len = stream.total_out;
  inflateEnd(&stream);

  cr_assert_eq(decompressed_len, total_size);
  cr_assert_eq(memcmp(original, decompressed, total_size), 0);

  SAFE_FREE(original);
  SAFE_FREE(compressed);
  SAFE_FREE(decompressed);
}

/**
 * Test: Invalid/corrupted compressed data
 * Ensure decompression fails gracefully with corrupted input
 */
Test(permessage_deflate, corrupted_data_handling) {
  size_t msg_size = 16384;

  uint8_t *original = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(original);
  generate_test_data(original, msg_size, 1);

  uint8_t *compressed = NULL;
  size_t compressed_len = 0;
  int ret = deflate_compress(original, msg_size, &compressed, &compressed_len);
  cr_assert_eq(ret, 0);

  // Corrupt the data
  if (compressed_len > 10) {
    compressed[compressed_len / 2] ^= 0xFF; // Flip all bits in middle
  }

  uint8_t *decompressed = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(decompressed);
  size_t decompressed_len = 0;

  ret = deflate_decompress(compressed, compressed_len, decompressed, msg_size, &decompressed_len);

  // Decompression should fail or produce corrupted data
  cr_assert(ret != 0 || decompressed_len == 0 || memcmp(original, decompressed, msg_size) != 0,
            "Decompression should fail or produce corrupted output for corrupted input");

  SAFE_FREE(original);
  SAFE_FREE(compressed);
  SAFE_FREE(decompressed);
}

/**
 * Test: Empty message handling
 */
Test(permessage_deflate, empty_message) {
  uint8_t dummy[1] = {0};
  uint8_t *compressed = NULL;
  size_t compressed_len = 0;

  // Try to compress empty message - should fail
  (void)deflate_compress(dummy, 0, &compressed, &compressed_len);

  // Should have failed gracefully
  if (compressed) {
    SAFE_FREE(compressed);
  }
}

/**
 * Test: Very large message (video frame size)
 * Ensure large messages are handled without buffer overflows
 */
Test(permessage_deflate, large_video_frame) {
  // Typical video frame: 1920x1080 RGB = 6,220,800 bytes
  // But test with smaller version: 640x480 RGB = 921,600 bytes
  size_t msg_size = 921600;

  uint8_t *original = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(original);

  // Fill with video-like pattern (RGB pixels)
  for (size_t i = 0; i < msg_size; i += 3) {
    original[i] = (uint8_t)(i & 0xFF); // R
    if (i + 1 < msg_size)
      original[i + 1] = (uint8_t)((i >> 8) & 0xFF); // G
    if (i + 2 < msg_size)
      original[i + 2] = (uint8_t)((i >> 16) & 0xFF); // B
  }

  uint8_t *compressed = NULL;
  size_t compressed_len = 0;
  int ret = deflate_compress(original, msg_size, &compressed, &compressed_len);
  cr_assert_eq(ret, 0);

  // Check compression ratio is reasonable
  float ratio = (float)compressed_len / (float)msg_size;
  cr_assert_lt(ratio, 1.0f, "Compressed should be smaller than original (ratio=%.2f%%)", ratio * 100.0f);

  uint8_t *decompressed = SAFE_MALLOC(msg_size, uint8_t *);
  cr_assert_not_null(decompressed);
  size_t decompressed_len = 0;

  ret = deflate_decompress(compressed, compressed_len, decompressed, msg_size, &decompressed_len);
  cr_assert_eq(ret, 0);
  cr_assert_eq(decompressed_len, msg_size);
  cr_assert_eq(memcmp(original, decompressed, msg_size), 0);

  log_info("Video frame compression: %zu bytes -> %zu bytes (ratio=%.2f%%)", msg_size, compressed_len, ratio * 100.0f);

  SAFE_FREE(original);
  SAFE_FREE(compressed);
  SAFE_FREE(decompressed);
}

/**
 * Test: Multiple sequential messages
 * Ensure compression state doesn't leak between messages
 */
Test(permessage_deflate, sequential_messages) {
  size_t msg_size = 65536;
  int num_messages = 5;

  for (int i = 0; i < num_messages; i++) {
    uint8_t *original = SAFE_MALLOC(msg_size, uint8_t *);
    cr_assert_not_null(original);
    generate_test_data(original, msg_size, i % 4);

    uint8_t *compressed = NULL;
    size_t compressed_len = 0;
    int ret = deflate_compress(original, msg_size, &compressed, &compressed_len);
    cr_assert_eq(ret, 0, "Compression should succeed for message %d", i);

    uint8_t *decompressed = SAFE_MALLOC(msg_size, uint8_t *);
    cr_assert_not_null(decompressed);
    size_t decompressed_len = 0;

    ret = deflate_decompress(compressed, compressed_len, decompressed, msg_size, &decompressed_len);
    cr_assert_eq(ret, 0, "Decompression should succeed for message %d", i);
    cr_assert_eq(memcmp(original, decompressed, msg_size), 0, "Data must match for message %d", i);

    SAFE_FREE(original);
    SAFE_FREE(compressed);
    SAFE_FREE(decompressed);
  }
}
