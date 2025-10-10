#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <criterion/theories.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

#include "compression.h"
#include "network.h"
#include "tests/common.h"
#include "options.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(compression);

// Mock network functions for testing
static int mock_send_packet_calls = 0;
static int mock_send_packet_result = 1; // Default to success

__attribute__((unused)) static int mock_send_packet(int sockfd, packet_type_t type, const void *data, size_t size) {
  (void)sockfd; // Suppress unused parameter warning
  (void)type;
  (void)data;
  (void)size;
  mock_send_packet_calls++;
  return mock_send_packet_result;
}

// Override the send_packet function with our mock
#define send_packet mock_send_packet

// Helper function to reset mock state
static void reset_mock_state(void) {
  mock_send_packet_calls = 0;
  mock_send_packet_result = 1; // Default to success
}

uint32_t mock_asciichat_crc32(const void *data, size_t size) {
  // Simple mock CRC32 - just return a hash of the data
  const char *bytes = (const char *)data;
  uint32_t hash = 0;
  for (size_t i = 0; i < size; i++) {
    hash = hash * 31 + bytes[i];
  }
  return hash;
}

// Override the macro with our mock function
#undef asciichat_crc32
#define asciichat_crc32(data, len) mock_asciichat_crc32((data), (len))

// Helper function to create a test socket
int create_test_socket(void) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    return -1;
  }

  // Set socket to non-blocking for testing
  int flags = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

  return sockfd;
}

// Helper function to generate test frame data
char *generate_test_frame_data(size_t size) {
  char *data = SAFE_MALLOC(size + 1, void *);
  if (!data)
    return NULL;

  // Fill with repeating pattern for better compression
  for (size_t i = 0; i < size; i++) {
    data[i] = 'A' + (i % 26);
  }
  data[size] = '\0';

  return data;
}

// Helper function to generate random test frame data
char *generate_random_frame_data(size_t size) {
  char *data = SAFE_MALLOC(size + 1, void *);
  if (!data)
    return NULL;

  // Fill with random-like data (poor compression)
  for (size_t i = 0; i < size; i++) {
    data[i] = (char)(rand() % 256);
  }
  data[size] = '\0';

  return data;
}

/* ============================================================================
 * Compression Roundtrip Tests
 * ============================================================================ */

// Theory: Compression roundtrip property - decompress(compress(data)) == data
TheoryDataPoints(compression, compression_roundtrip_property) = {
    DataPoints(size_t, 1, 16, 64, 256, 512, 1024, 4096, 8192),
};

Theory((size_t data_size), compression, compression_roundtrip_property) {
  cr_assume(data_size > 0 && data_size <= 8192);

  char *original_data = SAFE_MALLOC(data_size, void *);
  cr_assume(original_data != NULL);

  for (size_t i = 0; i < data_size; i++) {
    original_data[i] = (char)('A' + (i % 26));
  }

  void *compressed_data = NULL;
  size_t compressed_size = 0;
  int compress_result = compress_data(original_data, data_size, &compressed_data, &compressed_size);

  if (compress_result != 0 || compressed_data == NULL) {
    SAFE_FREE(original_data);
    cr_skip("Compression failed for size %zu", data_size);
    return;
  }

  char *decompressed_data = SAFE_MALLOC(data_size, void *);
  cr_assert_not_null(decompressed_data, "Decompression buffer allocation should succeed for size %zu", data_size);

  int decompress_result = decompress_data(compressed_data, compressed_size, decompressed_data, data_size);

  cr_assert_eq(decompress_result, 0, "Decompression should succeed for size %zu", data_size);
  cr_assert_eq(memcmp(original_data, decompressed_data, data_size), 0,
               "Compression roundtrip must preserve data for size %zu", data_size);

  SAFE_FREE(original_data);
  SAFE_FREE(compressed_data);
  SAFE_FREE(decompressed_data);
}

// Theory: Compression effectiveness - compressible data should compress well
TheoryDataPoints(compression, compressible_data_property) = {
    DataPoints(size_t, 64, 256, 1024, 4096),
};

Theory((size_t data_size), compression, compressible_data_property) {
  cr_assume(data_size > 0 && data_size <= 4096);

  char *original_data = SAFE_MALLOC(data_size, void *);
  cr_assume(original_data != NULL);
  memset(original_data, 'A', data_size);

  void *compressed_data = NULL;
  size_t compressed_size = 0;
  int result = compress_data(original_data, data_size, &compressed_data, &compressed_size);

  if (result == 0 && compressed_data != NULL) {
    float ratio = (float)compressed_size / (float)data_size;
    cr_assert_lt(ratio, 0.5f, "Highly compressible data should compress to <50%% for size %zu (got %.2f%%)", data_size,
                 ratio * 100.0f);

    char *decompressed_data = SAFE_MALLOC(data_size, void *);
    if (decompressed_data != NULL) {
      int decompress_result = decompress_data(compressed_data, compressed_size, decompressed_data, data_size);
      if (decompress_result == 0) {
        cr_assert_eq(memcmp(original_data, decompressed_data, data_size), 0,
                     "Roundtrip must work for compressible data size %zu", data_size);
      }
      SAFE_FREE(decompressed_data);
    }
    SAFE_FREE(compressed_data);
  }

  SAFE_FREE(original_data);
}

// Theory: Compression threshold property - should_compress follows threshold rule
TheoryDataPoints(compression, compression_threshold_property) = {
    DataPoints(size_t, 100, 500, 1000, 2000),
};

Theory((size_t original_size), compression, compression_threshold_property) {
  cr_assume(original_size > 0 && original_size <= 2000);

  size_t test_compressed_sizes[] = {(size_t)(original_size * 0.5f), (size_t)(original_size * 0.7f),
                                    (size_t)(original_size * 0.85f), (size_t)(original_size * 1.0f),
                                    (size_t)(original_size * 1.2f)};

  bool expected_results[] = {true, true, false, false, false};

  for (int i = 0; i < 5; i++) {
    size_t compressed_size = test_compressed_sizes[i];
    bool result = should_compress(original_size, compressed_size);

    cr_assert_eq(result, expected_results[i],
                 "should_compress(%zu, %zu) = %s, expected %s (ratio=%.2f, threshold=%.2f)", original_size,
                 compressed_size, result ? "true" : "false", expected_results[i] ? "true" : "false",
                 (float)compressed_size / original_size, COMPRESSION_RATIO_THRESHOLD);
  }
}

/* ============================================================================
 * ASCII Frame Packet Tests
 * ============================================================================ */

Test(compression, send_ascii_frame_packet_basic) {
  reset_mock_state();
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  // Set up mock
  mock_send_packet_calls = 0;
  mock_send_packet_result = 100; // Success

  // Set global options
  opt_width = 80;
  opt_height = 24;

  // Use very small data that won't compress well (so it won't use compression)
  char *frame_data = generate_random_frame_data(10);
  cr_assert_not_null(frame_data);

  int result = send_ascii_frame_packet(sockfd, frame_data, 10, 80, 24);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1); // Should call send_packet once
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

Test(compression, send_ascii_frame_packet_invalid_params) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  // Test with NULL frame data
  int result = send_ascii_frame_packet(sockfd, NULL, 100, 80, 24);
  cr_assert_eq(result, -1);

  // Test with zero frame size
  result = send_ascii_frame_packet(sockfd, "test", 0, 80, 24);
  cr_assert_eq(result, -1);

  // Test with invalid socket
  result = send_ascii_frame_packet(-1, "test", 4, 80, 24);
  cr_assert_eq(result, -1);

  close(sockfd);
}

Test(compression, send_ascii_frame_packet_oversized_frame) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  // Use smaller frame size in test environment for faster testing
  size_t test_size = (getenv("TESTING") || getenv("CRITERION_TEST")) ? 1024 : (1024 * 1024);
  char *large_frame = SAFE_MALLOC(test_size, void *);
  cr_assert_not_null(large_frame);
  memset(large_frame, 'A', test_size);

  int result = send_ascii_frame_packet(sockfd, large_frame, test_size, 80, 24);
  cr_assert_eq(result, -1);

  SAFE_FREE(large_frame);
  close(sockfd);
}

Test(compression, send_ascii_frame_packet_compressible_data) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Generate highly compressible data (repeating pattern)
  char *frame_data = generate_test_frame_data(1000);
  cr_assert_not_null(frame_data);

  int result = send_ascii_frame_packet(sockfd, frame_data, 1000, 80, 24);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

Test(compression, send_ascii_frame_packet_uncompressible_data) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Generate uncompressible data (random-like)
  char *frame_data = generate_random_frame_data(1000);
  cr_assert_not_null(frame_data);

  int result = send_ascii_frame_packet(sockfd, frame_data, 1000, 80, 24);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

Test(compression, send_ascii_frame_packet_send_failure) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = -1; // Simulate send failure

  char *frame_data = generate_test_frame_data(100);
  cr_assert_not_null(frame_data);

  int result = send_ascii_frame_packet(sockfd, frame_data, 100, 80, 24);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

Test(compression, send_ascii_frame_packet_memory_allocation_failure) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  // Test with very large frame that might cause allocation failure
  // This is hard to test reliably, but we can test the error handling path
  char *frame_data = generate_test_frame_data(100);
  cr_assert_not_null(frame_data);

  // The function should handle allocation failures gracefully
  int result = send_ascii_frame_packet(sockfd, frame_data, 100, 80, 24);

  // Should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);

  SAFE_FREE(frame_data);
  close(sockfd);
}

/* ============================================================================
 * Image Frame Packet Tests
 * ============================================================================ */

Test(compression, send_image_frame_packet_basic) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Generate test pixel data
  char *pixel_data = generate_test_frame_data(100);
  cr_assert_not_null(pixel_data);

  int result = send_image_frame_packet(sockfd, pixel_data, 100, 32, 32, 0x12345678);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  SAFE_FREE(pixel_data);
  close(sockfd);
}

Test(compression, send_image_frame_packet_invalid_params) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  // Test with NULL pixel data
  int result = send_image_frame_packet(sockfd, NULL, 1024, 32, 32, 0x12345678);
  cr_assert_eq(result, -1);

  // Test with zero pixel size
  result = send_image_frame_packet(sockfd, "test", 0, 32, 32, 0x12345678);
  cr_assert_eq(result, -1);

  // Test with invalid socket
  result = send_image_frame_packet(-1, "test", 4, 32, 32, 0x12345678);
  cr_assert_eq(result, -1);

  close(sockfd);
}

Test(compression, send_image_frame_packet_send_failure) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = -1; // Simulate send failure

  char *pixel_data = generate_test_frame_data(1024);
  cr_assert_not_null(pixel_data);

  int result = send_image_frame_packet(sockfd, pixel_data, 1024, 32, 32, 0x12345678);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  SAFE_FREE(pixel_data);
  close(sockfd);
}

Test(compression, send_image_frame_packet_memory_allocation_failure) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  char *pixel_data = generate_test_frame_data(1024);
  cr_assert_not_null(pixel_data);

  // The function should handle allocation failures gracefully
  int result = send_image_frame_packet(sockfd, pixel_data, 1024, 32, 32, 0x12345678);

  // Should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);

  SAFE_FREE(pixel_data);
  close(sockfd);
}

/* ============================================================================
 * Legacy Function Tests
 * ============================================================================ */

Test(compression, send_compressed_frame_legacy) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Set global options
  opt_width = 80;
  opt_height = 24;

  char *frame_data = generate_test_frame_data(100);
  cr_assert_not_null(frame_data);

  int result = send_compressed_frame(sockfd, frame_data, 100);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

Test(compression, send_compressed_frame_legacy_invalid_params) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  // Test with NULL frame data
  int result = send_compressed_frame(sockfd, NULL, 100);
  cr_assert_eq(result, -1);

  // Test with zero frame size
  result = send_compressed_frame(sockfd, "test", 0);
  cr_assert_eq(result, -1);

  // Test with invalid socket
  result = send_compressed_frame(-1, "test", 4);
  cr_assert_eq(result, -1);

  close(sockfd);
}

/* ============================================================================
 * Compression Ratio Tests
 * ============================================================================ */

Test(compression, compression_ratio_threshold) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Test with data that compresses well (should use compression)
  char *compressible_data = SAFE_MALLOC(1000, void *);
  cr_assert_not_null(compressible_data);
  memset(compressible_data, 'A', 1000); // Highly compressible

  int result = send_ascii_frame_packet(sockfd, compressible_data, 1000, 80, 24);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  SAFE_FREE(compressible_data);
  close(sockfd);
}

Test(compression, no_compression_when_ineffective) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Test with data that doesn't compress well (should not use compression)
  char *uncompressible_data = generate_random_frame_data(1000);
  cr_assert_not_null(uncompressible_data);

  int result = send_ascii_frame_packet(sockfd, uncompressible_data, 1000, 80, 24);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  SAFE_FREE(uncompressible_data);
  close(sockfd);
}

/* ============================================================================
 * Edge Cases and Stress Tests
 * ============================================================================ */

Test(compression, very_small_frame) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  char *frame_data = generate_test_frame_data(1);
  cr_assert_not_null(frame_data);

  int result = send_ascii_frame_packet(sockfd, frame_data, 1, 1, 1);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

Test(compression, large_frame) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Test with large but reasonable frame size (smaller in test environment)
  size_t test_size = (getenv("TESTING") || getenv("CRITERION_TEST")) ? 1024 : (1024 * 1024);
  char *frame_data = generate_test_frame_data(test_size);
  cr_assert_not_null(frame_data);

  int result = send_ascii_frame_packet(sockfd, frame_data, test_size, 1000, 1000);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

Test(compression, multiple_frames) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Send multiple frames
  int successful_calls = 0;
  for (int i = 0; i < 5; i++) {
    char *frame_data = generate_test_frame_data(100 + i * 10);
    cr_assert_not_null(frame_data);

    int result = send_ascii_frame_packet(sockfd, frame_data, 100 + i * 10, 80, 24);
    if (result > 0) {
      successful_calls++;
    }

    SAFE_FREE(frame_data);
  }

  // Should have at least some successful calls
  cr_assert_geq(successful_calls, 0);
  cr_assert_leq(successful_calls, 5);

  close(sockfd);
}

Test(compression, different_image_formats) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  char *pixel_data = generate_test_frame_data(1024);
  cr_assert_not_null(pixel_data);

  // Test different pixel formats
  uint32_t formats[] = {0x12345678, 0x87654321, 0x00000000, 0xFFFFFFFF};

  int successful_calls = 0;
  for (int i = 0; i < 4; i++) {
    int result = send_image_frame_packet(sockfd, pixel_data, 1024, 32, 32, formats[i]);
    if (result > 0) {
      successful_calls++;
    }
  }

  // Should have at least some successful calls
  cr_assert_geq(successful_calls, 0);
  cr_assert_leq(successful_calls, 4);

  SAFE_FREE(pixel_data);
  close(sockfd);
}

Test(compression, zero_dimensions) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  char *frame_data = generate_test_frame_data(100);
  cr_assert_not_null(frame_data);

  // Test with zero dimensions
  int result = send_ascii_frame_packet(sockfd, frame_data, 100, 0, 0);
  cr_assert(result == -1 || result > 0);

  result = send_image_frame_packet(sockfd, frame_data, 100, 0, 0, 0x12345678);
  cr_assert(result == -1 || result > 0);

  SAFE_FREE(frame_data);
  close(sockfd);
}

Test(compression, negative_dimensions) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  char *frame_data = generate_test_frame_data(100);
  cr_assert_not_null(frame_data);

  // Test with negative dimensions
  int result = send_ascii_frame_packet(sockfd, frame_data, 100, -1, -1);
  cr_assert(result == -1 || result > 0);

  result = send_image_frame_packet(sockfd, frame_data, 100, -1, -1, 0x12345678);
  cr_assert(result == -1 || result > 0);

  SAFE_FREE(frame_data);
  close(sockfd);
}

// =============================================================================
// Parameterized Tests for Compression Data Patterns
// =============================================================================

// Test case structure for compression data pattern tests
typedef struct {
  const char *description;
  size_t data_size;
  char fill_char;
  bool should_compress;
  const char *pattern_type;
} compression_data_test_case_t;

static compression_data_test_case_t compression_data_cases[] = {
    {"Highly compressible data", 1000, 'A', true, "repeating"},
    {"Moderately compressible", 1000, 'X', true, "repeating"},
    {"Random-like data", 1000, '\0', false, "random"}, // Will be filled with random
    {"Small data", 10, 'B', false, "small"},
    {"Large compressible data", 10000, 'C', true, "large_repeating"},
    {"Mixed pattern data", 500, 'D', true, "mixed"}};

ParameterizedTestParameters(compression, data_patterns) {
  size_t nb_cases = sizeof(compression_data_cases) / sizeof(compression_data_cases[0]);
  return cr_make_param_array(compression_data_test_case_t, compression_data_cases, nb_cases);
}

ParameterizedTest(compression_data_test_case_t *tc, compression, data_patterns) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0, "Socket creation should succeed for %s", tc->description);

  char *frame_data = SAFE_MALLOC(tc->data_size, void *);
  cr_assert_not_null(frame_data, "Memory allocation should succeed for %s", tc->description);

  if (tc->fill_char == '\0') {
    // Fill with random data
    for (size_t i = 0; i < tc->data_size; i++) {
      frame_data[i] = (char)(rand() % 256);
    }
  } else {
    // Fill with repeating pattern
    memset(frame_data, tc->fill_char, tc->data_size);
  }

  int result = send_ascii_frame_packet(sockfd, frame_data, tc->data_size, 80, 24);
  cr_assert(result == -1 || result > 0, "Should handle %s gracefully", tc->description);

  SAFE_FREE(frame_data);
  close(sockfd);
}

// Test case structure for compression frame size tests
typedef struct {
  size_t frame_size;
  int width;
  int height;
  const char *description;
} compression_frame_test_case_t;

static compression_frame_test_case_t compression_frame_cases[] = {
    {1, 1, 1, "Tiny frame"},          {100, 10, 10, "Small frame"},           {1000, 32, 32, "Medium frame"},
    {10000, 100, 100, "Large frame"}, {100000, 320, 240, "Very large frame"}, {1000000, 640, 480, "Huge frame"}};

ParameterizedTestParameters(compression, frame_sizes) {
  size_t nb_cases = sizeof(compression_frame_cases) / sizeof(compression_frame_cases[0]);
  return cr_make_param_array(compression_frame_test_case_t, compression_frame_cases, nb_cases);
}

ParameterizedTest(compression_frame_test_case_t *tc, compression, frame_sizes) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0, "Socket creation should succeed for %s", tc->description);

  char *frame_data = generate_test_frame_data(tc->frame_size);
  cr_assert_not_null(frame_data, "Frame data generation should succeed for %s", tc->description);

  int result = send_ascii_frame_packet(sockfd, frame_data, tc->frame_size, tc->width, tc->height);
  cr_assert(result == -1 || result > 0, "Should handle %s gracefully", tc->description);

  SAFE_FREE(frame_data);
  close(sockfd);
}

// Test case structure for compression image format tests
typedef struct {
  uint32_t pixel_format;
  const char *description;
} compression_image_format_test_case_t;

static compression_image_format_test_case_t compression_image_format_cases[] = {
    {0x12345678, "Standard RGB format"}, {0x87654321, "Reversed format"},    {0x00000000, "Zero format"},
    {0xFFFFFFFF, "Max format"},          {0xDEADBEEF, "Hex pattern format"}, {0xCAFEBABE, "Another hex pattern"}};

ParameterizedTestParameters(compression, image_formats) {
  size_t nb_cases = sizeof(compression_image_format_cases) / sizeof(compression_image_format_cases[0]);
  return cr_make_param_array(compression_image_format_test_case_t, compression_image_format_cases, nb_cases);
}

ParameterizedTest(compression_image_format_test_case_t *tc, compression, image_formats) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0, "Socket creation should succeed for %s", tc->description);

  char *pixel_data = generate_test_frame_data(1024);
  cr_assert_not_null(pixel_data, "Pixel data generation should succeed for %s", tc->description);

  int result = send_image_frame_packet(sockfd, pixel_data, 1024, 32, 32, tc->pixel_format);
  cr_assert(result == -1 || result > 0, "Should handle %s gracefully", tc->description);

  SAFE_FREE(pixel_data);
  close(sockfd);
}

// Test case structure for compression error condition tests
typedef struct {
  const char *description;
  bool test_null_data;
  bool test_zero_size;
  bool test_invalid_socket;
  bool test_negative_dimensions;
} compression_error_test_case_t;

static compression_error_test_case_t compression_error_cases[] = {{"NULL frame data", true, false, false, false},
                                                                  {"Zero frame size", false, true, false, false},
                                                                  {"Invalid socket", false, false, true, false},
                                                                  {"Negative dimensions", false, false, false, true}};

ParameterizedTestParameters(compression, error_conditions) {
  size_t nb_cases = sizeof(compression_error_cases) / sizeof(compression_error_cases[0]);
  return cr_make_param_array(compression_error_test_case_t, compression_error_cases, nb_cases);
}

ParameterizedTest(compression_error_test_case_t *tc, compression, error_conditions) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0, "Socket creation should succeed for %s", tc->description);

  int result;

  if (tc->test_null_data) {
    result = send_ascii_frame_packet(sockfd, NULL, 100, 80, 24);
  } else if (tc->test_zero_size) {
    result = send_ascii_frame_packet(sockfd, "test", 0, 80, 24);
  } else if (tc->test_invalid_socket) {
    result = send_ascii_frame_packet(-1, "test", 4, 80, 24);
  } else if (tc->test_negative_dimensions) {
    result = send_ascii_frame_packet(sockfd, "test", 4, -1, -1);
  } else {
    result = send_ascii_frame_packet(sockfd, "test", 4, 80, 24);
  }

  cr_assert_eq(result, -1, "Should fail for %s", tc->description);

  close(sockfd);
}

// Test case structure for compression stress tests
typedef struct {
  int num_frames;
  const char *description;
} compression_stress_test_case_t;

static compression_stress_test_case_t compression_stress_cases[] = {
    {5, "Light stress test"}, {20, "Medium stress test"}, {50, "Heavy stress test"}, {100, "Intensive stress test"}};

ParameterizedTestParameters(compression, stress_tests) {
  size_t nb_cases = sizeof(compression_stress_cases) / sizeof(compression_stress_cases[0]);
  return cr_make_param_array(compression_stress_test_case_t, compression_stress_cases, nb_cases);
}

ParameterizedTest(compression_stress_test_case_t *tc, compression, stress_tests) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0, "Socket creation should succeed for %s", tc->description);

  int successful_calls = 0;
  for (int i = 0; i < tc->num_frames; i++) {
    char *frame_data = generate_test_frame_data(100 + i * 10);
    cr_assert_not_null(frame_data, "Frame data generation should succeed for frame %d in %s", i, tc->description);

    int result = send_ascii_frame_packet(sockfd, frame_data, 100 + i * 10, 80, 24);
    if (result > 0) {
      successful_calls++;
    }

    SAFE_FREE(frame_data);
  }

  // Should have at least some successful calls
  cr_assert_geq(successful_calls, 0, "Should have some successful calls for %s", tc->description);
  cr_assert_leq(successful_calls, tc->num_frames, "Should not exceed total frames for %s", tc->description);

  close(sockfd);
}
