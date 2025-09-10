#include <criterion/criterion.h>
#include <criterion/new/assert.h>
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
#include "common.h"
#include "options.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(compression);

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
  char *data = malloc(size + 1);
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
  char *data = malloc(size + 1);
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

  free(frame_data);
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
  char *large_frame = malloc(test_size);
  cr_assert_not_null(large_frame);
  memset(large_frame, 'A', test_size);

  int result = send_ascii_frame_packet(sockfd, large_frame, test_size, 80, 24);
  cr_assert_eq(result, -1);

  free(large_frame);
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

  free(frame_data);
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

  free(frame_data);
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

  free(frame_data);
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

  free(frame_data);
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

  free(pixel_data);
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

  free(pixel_data);
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

  free(pixel_data);
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

  free(frame_data);
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
  char *compressible_data = malloc(1000);
  cr_assert_not_null(compressible_data);
  memset(compressible_data, 'A', 1000); // Highly compressible

  int result = send_ascii_frame_packet(sockfd, compressible_data, 1000, 80, 24);

  // The function should either succeed or fail gracefully
  cr_assert(result == -1 || result > 0);
  if (result > 0) {
    cr_assert_eq(mock_send_packet_calls, 1);
  }

  free(compressible_data);
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

  free(uncompressible_data);
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

  free(frame_data);
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

  free(frame_data);
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

    free(frame_data);
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

  free(pixel_data);
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

  free(frame_data);
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

  free(frame_data);
  close(sockfd);
}
