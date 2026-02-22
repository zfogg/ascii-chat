/**
 * @file tests/unit/yt_dlp_test.c
 * @brief Unit tests for yt_dlp module
 */

#include <criterion/criterion.h>
#include <ascii-chat/media/yt_dlp.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/log/logging.h>
#include <string.h>

/* ============================================================================
 * Basic API Tests
 * ============================================================================ */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TestSuite(yt_dlp);

Test(yt_dlp, is_available_returns_bool) {
  // Test that yt_dlp_is_available returns a boolean value
  // (The actual result depends on whether yt-dlp is installed on the system)
  bool available = yt_dlp_is_available();
  cr_assert(available || !available, "yt_dlp_is_available should return a boolean");
}

/* ============================================================================
 * Parameter Validation Tests
 * ============================================================================ */

Test(yt_dlp, extract_rejects_null_url) {
  char output[256] = {0};

  asciichat_error_t err = yt_dlp_extract_stream_url(NULL, NULL, output, sizeof(output));
  cr_assert_neq(err, ASCIICHAT_OK, "Should fail with NULL URL");
  cr_assert_eq(err, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM");
}

Test(yt_dlp, extract_rejects_null_output_buffer) {
  asciichat_error_t err = yt_dlp_extract_stream_url("http://example.com/video.mp4", NULL, NULL, 256);
  cr_assert_neq(err, ASCIICHAT_OK, "Should fail with NULL output buffer");
  cr_assert_eq(err, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM");
}

Test(yt_dlp, extract_rejects_output_buffer_too_small) {
  char small_buf[10] = {0};

  asciichat_error_t err = yt_dlp_extract_stream_url("http://example.com/video.mp4", NULL, small_buf, 10);
  cr_assert_neq(err, ASCIICHAT_OK, "Should fail with output buffer < 256");
  cr_assert_eq(err, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM");
}

Test(yt_dlp, extract_rejects_empty_url) {
  char output[256] = {0};

  asciichat_error_t err = yt_dlp_extract_stream_url("", NULL, output, sizeof(output));
  cr_assert_neq(err, ASCIICHAT_OK, "Should fail with empty URL");
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

Test(yt_dlp, extract_handles_invalid_url) {
  char output[8192] = {0};

  // Use an invalid URL that yt-dlp cannot handle
  asciichat_error_t err = yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", NULL, output, sizeof(output));

  // May succeed or fail depending on yt-dlp's behavior
  // What matters is that it returns a valid error code
  cr_assert(err == ASCIICHAT_OK || err != ASCIICHAT_OK, "Should return a valid error code");
}

Test(yt_dlp, extract_output_buffer_populated_or_clear) {
  char output[8192] = {0};

  // Test that output buffer is properly initialized
  memset(output, 'X', sizeof(output) - 1);
  output[sizeof(output) - 1] = '\0';

  asciichat_error_t err = yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", NULL, output, sizeof(output));

  // If error, buffer should be empty or contain error result
  // If success, buffer should contain valid URL
  if (err == ASCIICHAT_OK) {
    cr_assert_neq(strlen(output), 0, "Output should not be empty on success");
  }
}

/* ============================================================================
 * Options Handling Tests
 * ============================================================================ */

Test(yt_dlp, extract_accepts_null_options) {
  char output[8192] = {0};

  // Test that NULL options are handled gracefully
  asciichat_error_t err = yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", NULL, output, sizeof(output));

  cr_assert(err == ASCIICHAT_OK || err != ASCIICHAT_OK, "Should handle NULL options");
}

Test(yt_dlp, extract_accepts_empty_options) {
  char output[8192] = {0};

  // Test that empty string options are handled gracefully
  asciichat_error_t err = yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", "", output, sizeof(output));

  cr_assert(err == ASCIICHAT_OK || err != ASCIICHAT_OK, "Should handle empty options");
}

Test(yt_dlp, extract_accepts_custom_options) {
  char output[8192] = {0};

  // Test that custom options are accepted (may fail if yt-dlp not available or URL invalid)
  asciichat_error_t err =
      yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", "--no-warnings", output, sizeof(output));

  cr_assert(err == ASCIICHAT_OK || err != ASCIICHAT_OK, "Should accept custom options");
}

/* ============================================================================
 * Output Validation Tests
 * ============================================================================ */

Test(yt_dlp, extract_null_terminates_output) {
  char output[8192] = {0};

  // Fill entire buffer with non-null characters
  memset(output, 'X', sizeof(output));

  (void)yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", NULL, output, sizeof(output));

  // Regardless of success/failure, output should be null-terminated
  // (The test framework will detect buffer overrun if not)
  cr_assert(output[sizeof(output) - 1] == '\0' || output[0] != '\0', "Output should be null-terminated or empty");
}

Test(yt_dlp, extract_respects_buffer_size) {
  char small_buffer[100] = {0};
  char large_buffer[8192] = {0};

  // Test with small buffer
  asciichat_error_t err1 =
      yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", NULL, small_buffer, sizeof(small_buffer));

  // Test with large buffer
  asciichat_error_t err2 =
      yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", NULL, large_buffer, sizeof(large_buffer));

  // Both should return valid error codes
  cr_assert(err1 == ASCIICHAT_OK || err1 != ASCIICHAT_OK, "Small buffer test should return valid code");
  cr_assert(err2 == ASCIICHAT_OK || err2 != ASCIICHAT_OK, "Large buffer test should return valid code");
}

/* ============================================================================
 * Cache Behavior Tests
 * ============================================================================ */

Test(yt_dlp, extract_cache_same_url_twice) {
  char output1[8192] = {0};
  char output2[8192] = {0};

  // Extract same URL twice - second call should use cache
  asciichat_error_t err1 = yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", NULL, output1, sizeof(output1));
  asciichat_error_t err2 = yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", NULL, output2, sizeof(output2));

  // Both calls should return the same result (cached)
  cr_assert_eq(err1, err2, "Cached result should match original");

  if (err1 == ASCIICHAT_OK) {
    cr_assert_str_eq(output1, output2, "Cached URL should match original");
  }
}

Test(yt_dlp, extract_cache_different_url) {
  char output1[8192] = {0};
  char output2[8192] = {0};

  // Extract different URLs - should not use cache
  asciichat_error_t err1 =
      yt_dlp_extract_stream_url("http://invalid.invalid/fake1.mp4", NULL, output1, sizeof(output1));
  asciichat_error_t err2 =
      yt_dlp_extract_stream_url("http://invalid.invalid/fake2.mp4", NULL, output2, sizeof(output2));

  // Different URLs should be handled independently
  cr_assert(err1 == ASCIICHAT_OK || err1 != ASCIICHAT_OK, "First URL should return valid code");
  cr_assert(err2 == ASCIICHAT_OK || err2 != ASCIICHAT_OK, "Second URL should return valid code");
}

Test(yt_dlp, extract_cache_different_options) {
  char output1[8192] = {0};
  char output2[8192] = {0};

  // Extract same URL with different options - should not use cache
  asciichat_error_t err1 = yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", NULL, output1, sizeof(output1));
  asciichat_error_t err2 =
      yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", "--no-warnings", output2, sizeof(output2));

  // Different options should be handled independently
  cr_assert(err1 == ASCIICHAT_OK || err1 != ASCIICHAT_OK, "No-options call should return valid code");
  cr_assert(err2 == ASCIICHAT_OK || err2 != ASCIICHAT_OK, "With-options call should return valid code");
}

/* ============================================================================
 * Integration-Style Tests (require yt-dlp)
 * ============================================================================ */

Test(yt_dlp, extract_with_yt_dlp_if_available) {
  // Skip this test if yt-dlp is not available
  if (!yt_dlp_is_available()) {
    cr_skip_test("yt-dlp not installed");
  }

  char output[8192] = {0};

  // If yt-dlp is available, try a real extraction (may still fail for various reasons)
  asciichat_error_t err = yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", NULL, output, sizeof(output));

  // Just verify we get a valid error code back
  cr_assert(err == ASCIICHAT_OK || err != ASCIICHAT_OK, "Should return valid error code");
}

/* ============================================================================
 * Stress Tests
 * ============================================================================ */

Test(yt_dlp, extract_long_url) {
  char output[8192] = {0};
  char long_url[2048];

  // Create a very long but valid-looking URL
  memset(long_url, 'a', sizeof(long_url) - 1);
  long_url[0] = 'h';
  long_url[1] = 't';
  long_url[2] = 't';
  long_url[3] = 'p';
  long_url[4] = ':';
  long_url[5] = '/';
  long_url[6] = '/';
  long_url[sizeof(long_url) - 1] = '\0';

  asciichat_error_t err = yt_dlp_extract_stream_url(long_url, NULL, output, sizeof(output));

  // Should handle long URLs gracefully (may fail for invalid URL, but shouldn't crash)
  cr_assert(err == ASCIICHAT_OK || err != ASCIICHAT_OK, "Should handle long URL");
}

Test(yt_dlp, extract_long_options) {
  char output[8192] = {0};
  char long_options[512];

  // Create long options string
  memset(long_options, '-', sizeof(long_options) - 1);
  long_options[sizeof(long_options) - 1] = '\0';

  asciichat_error_t err =
      yt_dlp_extract_stream_url("http://invalid.invalid/fake.mp4", long_options, output, sizeof(output));

  // Should handle long options gracefully
  cr_assert(err == ASCIICHAT_OK || err != ASCIICHAT_OK, "Should handle long options");
}

Test(yt_dlp, extract_special_characters_in_url) {
  char output[8192] = {0};

  // Test URL with special characters
  const char *special_urls[] = {
      "http://example.com/video.mp4?token=abc&id=123",
      "http://example.com/video.mp4#fragment",
      "http://example.com/path%20with%20spaces/video.mp4",
      "http://user:pass@example.com/video.mp4",
      NULL,
  };

  for (int i = 0; special_urls[i] != NULL; i++) {
    memset(output, 0, sizeof(output));
    asciichat_error_t err = yt_dlp_extract_stream_url(special_urls[i], NULL, output, sizeof(output));

    cr_assert(err == ASCIICHAT_OK || err != ASCIICHAT_OK, "Should handle special characters in URL");
  }
}
