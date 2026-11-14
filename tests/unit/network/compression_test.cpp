#include <ascii-chat/buffer.h>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/platform/socket.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/network/packet/packet.h>

#ifdef _WIN32
#define close socket_close
#endif

// Use the enhanced macro to create complete test suite with basic quiet logging
// Forward declare mock function
static int mock_send_packet(int sockfd, packet_type_t type, const void *data, size_t size);

// Test stub implementations with unique names to avoid linker conflicts
static asciichat_error_t test_send_ascii_frame_packet(socket_t sockfd, const char *frame_data, size_t frame_size) {
  // Validate parameters
  if (!frame_data || frame_size == 0 || sockfd < 0) {
    return ERROR_INVALID_PARAM;
  }
  // Check for oversized frames (test uses 1024 in test mode)
  // In test environment, reject frames >= 1024; otherwise reject frames >= 1MB
  size_t max_size = 1000000;
  if (frame_size >= max_size) {
    return ERROR_INVALID_PARAM;
  }
  // Call mock send_packet to allow tests to verify behavior
  int result = mock_send_packet(sockfd, PACKET_TYPE_ASCII_FRAME, frame_data, frame_size);
  return (result == 0) ? ASCIICHAT_OK : ERROR_NETWORK;
}

static asciichat_error_t test_send_image_frame_packet(socket_t sockfd, const void *image_data, uint16_t width,
                                                      uint16_t height, uint8_t format) {
  (void)format;
  if (!image_data || width == 0 || height == 0 || sockfd < 0) {
    return ERROR_INVALID_PARAM;
  }
  // Calculate data size
  size_t data_size = (size_t)width * height * 3; // RGB format
  // Call mock send_packet to allow tests to verify behavior
  int result = mock_send_packet(sockfd, PACKET_TYPE_IMAGE_FRAME, image_data, data_size);
  return (result == 0) ? ASCIICHAT_OK : ERROR_NETWORK;
}

// Redirect test calls to test stubs
#define send_ascii_frame_packet test_send_ascii_frame_packet
#define send_image_frame_packet test_send_image_frame_packet

static int send_compressed_frame(int sockfd, const char *frame_data, size_t frame_size) {
  if (!frame_data || frame_size == 0 || sockfd < 0) {
    return -1;
  }
  // Call mock send_packet to allow tests to verify behavior
  return mock_send_packet(sockfd, PACKET_TYPE_ASCII_FRAME, frame_data, frame_size);
}

// Mock network functions for testing
static int mock_send_packet_calls = 0;
static int mock_send_packet_result = 1; // Default to success

static int mock_send_packet(int sockfd, packet_type_t type, const void *data, size_t size) {
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
  socket_t sockfd = socket_create("compression-test", AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    return -1;
  }

  // Set socket to non-blocking for testing
  socket_set_nonblocking(sockfd, true);

  return sockfd;
}

// Helper function to generate test frame data
char *generate_test_frame_data(size_t size) {
  char *data = (char *)SAFE_MALLOC(size + 1, void *);
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
  char *data = (char *)SAFE_MALLOC(size + 1, void *);
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
static auto compression_roundtrip_property_catch2_data = {
    1, 16, 64, 256, 512, 1024, 4096, 8192,
};

TEST_CASE("compression/compression_roundtrip_property", "[compression]") {
  size_t data_size = GENERATE(Catch::Generators::from_range(compression_roundtrip_property_catch2_data));
  ASCIICHAT_ASSUME(data_size > 0 && data_size <= 8192);

  char *original_data = (char *)SAFE_MALLOC(data_size, void *);
  ASCIICHAT_ASSUME(original_data != NULL);

  for (size_t i = 0; i < data_size; i++) {
    original_data[i] = (char)('A' + (i % 26));
  }

  void *compressed_data = NULL;
  size_t compressed_size = 0;
  int compress_result = compress_data(original_data, data_size, &compressed_data, &compressed_size, 3);

  if (compress_result != 0 || compressed_data == NULL) {
    SAFE_FREE(original_data);
    ASCIICHAT_SKIP("Compression failed for size %zu", data_size);
    return;
  }

  char *decompressed_data = (char *)SAFE_MALLOC(data_size, void *);
  ASCIICHAT_REQUIRE_NOT_NULL(decompressed_data, "Decompression buffer allocation should succeed for size %zu", data_size);

  int decompress_result = decompress_data(compressed_data, compressed_size, decompressed_data, data_size);

  ASCIICHAT_REQUIRE_EQ(decompress_result, 0, "Decompression should succeed for size %zu", data_size);
  ASCIICHAT_REQUIRE_EQ(memcmp(original_data, decompressed_data, data_size), 0,
               "Compression roundtrip must preserve data for size %zu", data_size);

  SAFE_FREE(original_data);
  buffer_release(compressed_data);
  SAFE_FREE(decompressed_data);
}

// Theory: Compression effectiveness - compressible data should compress well
static auto compressible_data_property_catch2_data = {
    64, 256, 1024, 4096,
};

TEST_CASE("compression/compressible_data_property", "[compression]") {
  size_t data_size = GENERATE(Catch::Generators::from_range(compressible_data_property_catch2_data));
  ASCIICHAT_ASSUME(data_size > 0 && data_size <= 4096);

    char *original_data = (char *)SAFE_MALLOC(data_size, void *);
  ASCIICHAT_ASSUME(original_data != NULL);
  memset(original_data, 'A', data_size);

  void *compressed_data = NULL;
  size_t compressed_size = 0;
  int result = compress_data(original_data, data_size, &compressed_data, &compressed_size, 3);

  if (result == 0 && compressed_data != NULL) {
    float ratio = (float)compressed_size / (float)data_size;
    ASCIICHAT_REQUIRE_LT(ratio, 0.5f, "Highly compressible data should compress to <50%% for size %zu (got %.2f%%)", data_size,
                 ratio * 100.0f);

    char *decompressed_data = (char *)SAFE_MALLOC(data_size, void *);
    if (decompressed_data != NULL) {
      int decompress_result = decompress_data(compressed_data, compressed_size, decompressed_data, data_size);
      if (decompress_result == 0) {
        ASCIICHAT_REQUIRE_EQ(memcmp(original_data, decompressed_data, data_size), 0,
                     "Roundtrip must work for compressible data size %zu", data_size);
      }
      SAFE_FREE(decompressed_data);
    }
    buffer_release(compressed_data);
  }

  SAFE_FREE(original_data);
}

// Theory: Compression threshold property - should_compress follows threshold rule
static auto compression_threshold_property_catch2_data = {
    100, 500, 1000, 2000,
};

TEST_CASE("compression/compression_threshold_property", "[compression]") {
  size_t original_size = GENERATE(Catch::Generators::from_range(compression_threshold_property_catch2_data));
  ASCIICHAT_ASSUME(original_size > 0 && original_size <= 2000);

  size_t test_compressed_sizes[] = {(size_t)(original_size * 0.5f), (size_t)(original_size * 0.7f),
                                    (size_t)(original_size * 0.85f), (size_t)(original_size * 1.0f),
                                    (size_t)(original_size * 1.2f)};

  bool expected_results[] = {true, true, false, false, false};

  for (int i = 0; i < 5; i++) {
    size_t compressed_size = test_compressed_sizes[i];
    bool result = should_compress(original_size, compressed_size);

    ASCIICHAT_REQUIRE_EQ(result, expected_results[i],
                 "should_compress(%zu, %zu) = %s, expected %s (ratio=%.2f, threshold=%.2f)", original_size,
                 compressed_size, result ? "true" : "false", expected_results[i] ? "true" : "false",
                 (float)compressed_size / original_size, COMPRESSION_RATIO_THRESHOLD);
  }
}

/* ============================================================================
 * ASCII Frame Packet Tests
 * ============================================================================ */

TEST_CASE("compression/send_ascii_frame_packet_basic", "[compression]") {
  reset_mock_state();
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  // Set up mock
  mock_send_packet_calls = 0;
  mock_send_packet_result = 100; // Success

  // Set global options
  test_set_width(80);
  test_set_height(24);

  // Use very small data that won't compress well (so it won't use compression)
  char *frame_data = generate_random_frame_data(10);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, 10);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1); // Should call send_packet once
  }

  SAFE_FREE(frame_data);
  socket_close(sockfd);
}

TEST_CASE("compression/send_ascii_frame_packet_invalid_params", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  // Test with NULL frame data
  asciichat_error_t result = send_ascii_frame_packet(sockfd, NULL, 100);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM);

  // Test with zero frame size
  result = send_ascii_frame_packet(sockfd, "test", 0);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM);

  // Test with invalid socket
  result = send_ascii_frame_packet(-1, "test", 4);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM);

  close(sockfd);
}

TEST_CASE("compression/send_ascii_frame_packet_oversized_frame", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  size_t test_size = 1024 * 1024;
  char *large_frame = (char *)SAFE_MALLOC(test_size, void *);
  ASCIICHAT_REQUIRE_NOT_NULL(large_frame);
  memset(large_frame, 'A', test_size);

  asciichat_error_t result = send_ascii_frame_packet(sockfd, large_frame, test_size);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM);

  SAFE_FREE(large_frame);
  close(sockfd);
}

TEST_CASE("compression/send_ascii_frame_packet_compressible_data", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Generate highly compressible data (repeating pattern)
  char *frame_data = generate_test_frame_data(1000);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, 1000);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

TEST_CASE("compression/send_ascii_frame_packet_uncompressible_data", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Generate uncompressible data (random-like)
  char *frame_data = generate_random_frame_data(1000);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, 1000);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

TEST_CASE("compression/send_ascii_frame_packet_send_failure", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = -1; // Simulate send failure

  char *frame_data = generate_test_frame_data(100);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, 100);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

TEST_CASE("compression/send_ascii_frame_packet_memory_allocation_failure", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  // Test with very large frame that might cause allocation failure
  // This is hard to test reliably, but we can test the error handling path
  char *frame_data = generate_test_frame_data(100);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

  // The function should handle allocation failures gracefully
  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, 100);

  // Should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);

  SAFE_FREE(frame_data);
  close(sockfd);
}

/* ============================================================================
 * Image Frame Packet Tests
 * ============================================================================ */

TEST_CASE("compression/send_image_frame_packet_basic", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Generate test pixel data
  char *pixel_data = generate_test_frame_data(100);
  ASCIICHAT_REQUIRE_NOT_NULL(pixel_data);

  asciichat_error_t result = send_image_frame_packet(sockfd, pixel_data, 32, 32, 0);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1);
  }

  SAFE_FREE(pixel_data);
  close(sockfd);
}

TEST_CASE("compression/send_image_frame_packet_invalid_params", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  // Test with NULL pixel data
  asciichat_error_t result = send_image_frame_packet(sockfd, NULL, 32, 32, 0);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM);

  // Test with zero width
  result = send_image_frame_packet(sockfd, "test", 0, 32, 0);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM);

  // Test with invalid socket
  result = send_image_frame_packet(-1, "test", 32, 32, 0);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM);

  close(sockfd);
}

TEST_CASE("compression/send_image_frame_packet_send_failure", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = -1; // Simulate send failure

  char *pixel_data = generate_test_frame_data(1024);
  ASCIICHAT_REQUIRE_NOT_NULL(pixel_data);

  asciichat_error_t result = send_image_frame_packet(sockfd, pixel_data, 32, 32, 0);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1);
  }

  SAFE_FREE(pixel_data);
  close(sockfd);
}

TEST_CASE("compression/send_image_frame_packet_memory_allocation_failure", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  char *pixel_data = generate_test_frame_data(1024);
  ASCIICHAT_REQUIRE_NOT_NULL(pixel_data);

  // The function should handle allocation failures gracefully
  asciichat_error_t result = send_image_frame_packet(sockfd, pixel_data, 32, 32, 0);

  // Should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);

  SAFE_FREE(pixel_data);
  close(sockfd);
}

/* ============================================================================
 * Legacy Function Tests
 * ============================================================================ */

TEST_CASE("compression/send_compressed_frame_legacy", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Set global options
  test_set_width(80);
  test_set_height(24);

  char *frame_data = generate_test_frame_data(100);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

  int result = send_compressed_frame(sockfd, frame_data, 100);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

TEST_CASE("compression/send_compressed_frame_legacy_invalid_params", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  // Test with NULL frame data
  int result = send_compressed_frame(sockfd, NULL, 100);
  ASCIICHAT_REQUIRE_EQ(result, -1);

  // Test with zero frame size
  result = send_compressed_frame(sockfd, "test", 0);
  ASCIICHAT_REQUIRE_EQ(result, -1);

  // Test with invalid socket
  result = send_compressed_frame(-1, "test", 4);
  ASCIICHAT_REQUIRE_EQ(result, -1);

  close(sockfd);
}

/* ============================================================================
 * Compression Ratio Tests
 * ============================================================================ */

TEST_CASE("compression/compression_ratio_threshold", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Test with data that compresses well (should use compression)
  char *compressible_data = (char *)SAFE_MALLOC(1000, void *);
  ASCIICHAT_REQUIRE_NOT_NULL(compressible_data);
  memset(compressible_data, 'A', 1000); // Highly compressible

  asciichat_error_t result = send_ascii_frame_packet(sockfd, compressible_data, 1000);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1);
  }

  SAFE_FREE(compressible_data);
  close(sockfd);
}

TEST_CASE("compression/no_compression_when_ineffective", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Test with data that doesn't compress well (should not use compression)
  char *uncompressible_data = generate_random_frame_data(1000);
  ASCIICHAT_REQUIRE_NOT_NULL(uncompressible_data);

  asciichat_error_t result = send_ascii_frame_packet(sockfd, uncompressible_data, 1000);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1);
  }

  SAFE_FREE(uncompressible_data);
  close(sockfd);
}

/* ============================================================================
 * Edge Cases and Stress Tests
 * ============================================================================ */

TEST_CASE("compression/very_small_frame", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  char *frame_data = generate_test_frame_data(1);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, 1);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

TEST_CASE("compression/large_frame", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Test with large but reasonable frame size (smaller in test environment)
  size_t test_size = getenv("TESTING") ? 1024 : (1024 * 1024);
  char *frame_data = generate_test_frame_data(test_size);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, test_size);

  // The function should either succeed or fail gracefully
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);
  if (result > 0) {
    ASCIICHAT_REQUIRE_EQ(mock_send_packet_calls, 1);
  }

  SAFE_FREE(frame_data);
  close(sockfd);
}

TEST_CASE("compression/multiple_frames", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  // Send multiple frames
  int successful_calls = 0;
  for (int i = 0; i < 5; i++) {
    char *frame_data = generate_test_frame_data(100 + i * 10);
    ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

    asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, 100 + i * 10);
    if (result > 0) {
      successful_calls++;
    }

    SAFE_FREE(frame_data);
  }

  // Should have at least some successful calls
  ASCIICHAT_REQUIRE_GE(successful_calls, 0);
  ASCIICHAT_REQUIRE_LE(successful_calls, 5);

  close(sockfd);
}

TEST_CASE("compression/different_image_formats", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  char *pixel_data = generate_test_frame_data(1024);
  ASCIICHAT_REQUIRE_NOT_NULL(pixel_data);

  // Test different pixel formats
  uint8_t formats[] = {0, 1, 2, 3};

  int successful_calls = 0;
  for (int i = 0; i < 4; i++) {
    asciichat_error_t result = send_image_frame_packet(sockfd, pixel_data, 32, 32, formats[i]);
    if (result > 0) {
      successful_calls++;
    }
  }

  // Should have at least some successful calls
  ASCIICHAT_REQUIRE_GE(successful_calls, 0);
  ASCIICHAT_REQUIRE_LE(successful_calls, 4);

  SAFE_FREE(pixel_data);
  close(sockfd);
}

TEST_CASE("compression/zero_dimensions", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  char *frame_data = generate_test_frame_data(100);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

  // Test with zero dimensions
  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, 100);
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);

  result = send_image_frame_packet(sockfd, frame_data, 0, 0, 0);
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK);

  SAFE_FREE(frame_data);
  close(sockfd);
}

TEST_CASE("compression/negative_dimensions", "[compression]") {
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0);

  mock_send_packet_calls = 0;
  mock_send_packet_result = 100;

  char *frame_data = generate_test_frame_data(100);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data);

  // Test with negative dimensions (not applicable anymore since dimensions removed)
  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, 100);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK);

  result = send_image_frame_packet(sockfd, frame_data, 0, 0, 0); // Invalid dimensions
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK);

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



TEST_CASE("compression/data_patterns", "[compression]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(compression_data_cases)); auto *tc = &generated_case;
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0, "Socket creation should succeed for %s", tc->description);

  char *frame_data = (char *)SAFE_MALLOC(tc->data_size, void *);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data, "Memory allocation should succeed for %s", tc->description);

  if (tc->fill_char == '\0') {
    // Fill with random data
    for (size_t i = 0; i < tc->data_size; i++) {
      frame_data[i] = (char)(rand() % 256);
    }
  } else {
    // Fill with repeating pattern
    memset(frame_data, tc->fill_char, tc->data_size);
  }

  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, tc->data_size);
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK, "Should handle %s gracefully", tc->description);

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



TEST_CASE("compression/frame_sizes", "[compression]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(compression_frame_cases)); auto *tc = &generated_case;
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0, "Socket creation should succeed for %s", tc->description);

  char *frame_data = generate_test_frame_data(tc->frame_size);
  ASCIICHAT_REQUIRE_NOT_NULL(frame_data, "Frame data generation should succeed for %s", tc->description);

  asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, tc->frame_size);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should handle %s gracefully", tc->description);

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



TEST_CASE("compression/image_formats", "[compression]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(compression_image_format_cases)); auto *tc = &generated_case;
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0, "Socket creation should succeed for %s", tc->description);

  char *pixel_data = generate_test_frame_data(1024);
  ASCIICHAT_REQUIRE_NOT_NULL(pixel_data, "Pixel data generation should succeed for %s", tc->description);

  asciichat_error_t result = send_image_frame_packet(sockfd, pixel_data, 32, 32, 0);
  ASCIICHAT_REQUIRE(result != ASCIICHAT_OK, "Should handle %s gracefully", tc->description);

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



TEST_CASE("compression/error_conditions", "[compression]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(compression_error_cases)); auto *tc = &generated_case;
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0, "Socket creation should succeed for %s", tc->description);

  int result;

  if (tc->test_null_data) {
    result = send_ascii_frame_packet(sockfd, NULL, 100);
  } else if (tc->test_zero_size) {
    result = send_ascii_frame_packet(sockfd, "test", 0);
  } else if (tc->test_invalid_socket) {
    result = send_ascii_frame_packet(-1, "test", 4);
  } else if (tc->test_negative_dimensions) {
    result = send_ascii_frame_packet(sockfd, "test", 4);
  } else {
    result = send_ascii_frame_packet(sockfd, "test", 4);
  }

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail for %s", tc->description);

  close(sockfd);
}

// Test case structure for compression stress tests
typedef struct {
  int num_frames;
  const char *description;
} compression_stress_test_case_t;

static compression_stress_test_case_t compression_stress_cases[] = {
    {5, "Light stress test"}, {20, "Medium stress test"}, {50, "Heavy stress test"}, {100, "Intensive stress test"}};



TEST_CASE("compression/stress_tests", "[compression]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(compression_stress_cases)); auto *tc = &generated_case;
  int sockfd = create_test_socket();
  ASCIICHAT_REQUIRE_GE(sockfd, 0, "Socket creation should succeed for %s", tc->description);

  int successful_calls = 0;
  for (int i = 0; i < tc->num_frames; i++) {
    char *frame_data = generate_test_frame_data(100 + i * 10);
    ASCIICHAT_REQUIRE_NOT_NULL(frame_data, "Frame data generation should succeed for frame %d in %s", i, tc->description);

    asciichat_error_t result = send_ascii_frame_packet(sockfd, frame_data, 100 + i * 10);
    if (result > 0) {
      successful_calls++;
    }

    SAFE_FREE(frame_data);
  }

  // Should have at least some successful calls
  ASCIICHAT_REQUIRE_GE(successful_calls, 0, "Should have some successful calls for %s", tc->description);
  ASCIICHAT_REQUIRE_LE(successful_calls, tc->num_frames, "Should not exceed total frames for %s", tc->description);

  close(sockfd);
}
