/**
 * @file youtube_test.c
 * @brief Unit tests for YouTube URL extraction and detection
 * @ingroup tests
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>

#include "tests/logging.h"
#include "media/youtube.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(youtube);

// =============================================================================
// YouTube URL Detection Tests
// =============================================================================

typedef struct {
  const char *url;
  bool expected_is_youtube;
  const char *description;
} url_detection_test_case_t;

static url_detection_test_case_t url_detection_cases[] = {
    // Standard YouTube URLs
    {"https://www.youtube.com/watch?v=dQw4w9WgXcQ", true, "Standard YouTube URL with www"},
    {"https://youtube.com/watch?v=dQw4w9WgXcQ", true, "YouTube URL without www"},
    {"https://m.youtube.com/watch?v=dQw4w9WgXcQ", true, "Mobile YouTube URL"},
    {"http://youtube.com/watch?v=dQw4w9WgXcQ", true, "HTTP YouTube URL"},

    // YouTube short URL
    {"https://youtu.be/dQw4w9WgXcQ", true, "YouTube short URL"},
    {"https://youtu.be/dQw4w9WgXcQ?t=10", true, "YouTube short URL with timestamp"},

    // YouTube URLs with parameters
    {"https://youtube.com/watch?v=dQw4w9WgXcQ&t=30", true, "YouTube URL with timestamp parameter"},
    {"https://youtube.com/watch?v=dQw4w9WgXcQ&list=PLAYLIST", true, "YouTube URL with playlist parameter"},

    // Non-YouTube URLs
    {"https://example.com/watch?v=dQw4w9WgXcQ", false, "Non-YouTube domain"},
    {"https://vimeo.com/123456", false, "Vimeo URL"},
    {"http://example.com/video.mp4", false, "HTTP file URL"},
    {"file:///home/user/video.mp4", false, "File URL"},
    {"", false, "Empty string"},
    {"/path/to/video.mp4", false, "Local file path"},
    {"video.mp4", false, "Local filename"},
};

Test(youtube, url_detection) {
  size_t num_cases = sizeof(url_detection_cases) / sizeof(url_detection_cases[0]);
  for (size_t i = 0; i < num_cases; i++) {
    url_detection_test_case_t *tc = &url_detection_cases[i];
    bool result = youtube_is_youtube_url(tc->url);
    cr_assert_eq(result, tc->expected_is_youtube,
                 "%s: youtube_is_youtube_url(\"%s\") should return %s",
                 tc->description, tc->url, tc->expected_is_youtube ? "true" : "false");
  }
}

// =============================================================================
// YouTube Video ID Extraction Tests
// =============================================================================

typedef struct {
  const char *url;
  const char *expected_video_id;
  asciichat_error_t expected_error;
  const char *description;
} video_id_extraction_test_case_t;

static video_id_extraction_test_case_t video_id_cases[] = {
    // Valid video IDs
    {"https://youtube.com/watch?v=dQw4w9WgXcQ", "dQw4w9WgXcQ", ASCIICHAT_OK,
     "Standard YouTube URL"},
    {"https://youtu.be/dQw4w9WgXcQ", "dQw4w9WgXcQ", ASCIICHAT_OK, "YouTube short URL"},
    {"https://m.youtube.com/watch?v=dQw4w9WgXcQ", "dQw4w9WgXcQ", ASCIICHAT_OK,
     "Mobile YouTube URL"},
    {"https://youtube.com/watch?v=dQw4w9WgXcQ&t=30", "dQw4w9WgXcQ", ASCIICHAT_OK,
     "YouTube URL with parameters"},

    // Invalid URLs
    {"https://example.com/watch?v=ID", "InvalidVideoId", ERROR_YOUTUBE_INVALID_URL,
     "Non-YouTube domain"},
    {"https://youtube.com/watch", "InvalidVideoId", ERROR_YOUTUBE_INVALID_URL,
     "YouTube URL without video ID"},
};

Test(youtube, video_id_extraction) {
  size_t num_cases = sizeof(video_id_cases) / sizeof(video_id_cases[0]);
  for (size_t i = 0; i < num_cases; i++) {
    video_id_extraction_test_case_t *tc = &video_id_cases[i];
    char extracted_id[16] = {0};
    asciichat_error_t result = youtube_extract_video_id(tc->url, extracted_id, sizeof(extracted_id));

    cr_assert_eq(result, tc->expected_error,
                 "%s: youtube_extract_video_id(\"%s\") should return %d", tc->description,
                 tc->url, tc->expected_error);

    if (result == ASCIICHAT_OK) {
      cr_assert_str_eq(extracted_id, tc->expected_video_id,
                       "%s: Extracted video ID should be '%s', got '%s'",
                       tc->description, tc->expected_video_id, extracted_id);
    }
  }
}

// =============================================================================
// Edge Case Tests
// =============================================================================

Test(youtube, null_url_detection) {
  // NULL pointer should not crash
  bool result = youtube_is_youtube_url(NULL);
  cr_assert_eq(result, false, "NULL URL should return false");
}

Test(youtube, null_url_extraction) {
  // NULL pointer should not crash and should return error
  char id[16] = {0};
  asciichat_error_t result = youtube_extract_video_id(NULL, id, sizeof(id));
  cr_assert_eq(result, ERROR_INVALID_PARAM, "NULL URL should return ERROR_INVALID_PARAM");
}

Test(youtube, small_buffer_extraction) {
  // Buffer too small should return error
  char small_id[4] = {0};
  asciichat_error_t result = youtube_extract_video_id(
      "https://youtube.com/watch?v=dQw4w9WgXcQ", small_id, sizeof(small_id));
  cr_assert_eq(result, ERROR_INVALID_PARAM,
               "Small buffer should return ERROR_INVALID_PARAM");
}

Test(youtube, video_id_length) {
  // Verify video ID is 11 characters long for valid IDs
  char id[16] = {0};
  asciichat_error_t result =
      youtube_extract_video_id("https://youtube.com/watch?v=dQw4w9WgXcQ", id, sizeof(id));
  cr_assert_eq(result, ASCIICHAT_OK, "Video ID extraction should succeed");
  cr_assert_eq(strlen(id), 11, "YouTube video IDs should be 11 characters long");
}

// =============================================================================
// URL Variation Tests
// =============================================================================

Test(youtube, youtube_url_variations) {
  // Test various YouTube URL formats
  const char *valid_urls[] = {
      "https://www.youtube.com/watch?v=dQw4w9WgXcQ",
      "https://youtube.com/watch?v=dQw4w9WgXcQ",
      "https://m.youtube.com/watch?v=dQw4w9WgXcQ",
      "https://youtu.be/dQw4w9WgXcQ",
      "http://youtube.com/watch?v=dQw4w9WgXcQ",
  };

  for (size_t i = 0; i < sizeof(valid_urls) / sizeof(valid_urls[0]); i++) {
    cr_assert(youtube_is_youtube_url(valid_urls[i]),
              "URL %zu should be recognized as YouTube URL: %s", i, valid_urls[i]);

    char id[16] = {0};
    asciichat_error_t result =
        youtube_extract_video_id(valid_urls[i], id, sizeof(id));
    cr_assert_eq(result, ASCIICHAT_OK,
                 "Video ID extraction should succeed for URL %zu: %s", i, valid_urls[i]);
    cr_assert_str_eq(id, "dQw4w9WgXcQ",
                     "All URL formats should extract same video ID, got '%s' for URL %zu", id, i);
  }
}

// =============================================================================
// Error Code Tests
// =============================================================================

Test(youtube, error_code_definitions) {
  // Verify YouTube error codes are defined and distinct
  int codes[] = {ERROR_YOUTUBE_INVALID_URL, ERROR_YOUTUBE_EXTRACT_FAILED,
                 ERROR_YOUTUBE_UNPLAYABLE, ERROR_YOUTUBE_NETWORK};

  for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
    // Each error code should have a string representation
    const char *str = asciichat_error_string(codes[i]);
    cr_assert_not_null(str, "Error code %d should have a string representation",
                       codes[i]);
    cr_assert(strlen(str) > 0,
              "Error code %d string should not be empty", codes[i]);
  }
}

Test(youtube, error_codes_distinct) {
  // Test that all YouTube error codes are distinct
  int codes[] = {ERROR_YOUTUBE_INVALID_URL, ERROR_YOUTUBE_EXTRACT_FAILED,
                 ERROR_YOUTUBE_UNPLAYABLE, ERROR_YOUTUBE_NETWORK};

  for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
    for (size_t j = i + 1; j < sizeof(codes) / sizeof(codes[0]); j++) {
      cr_assert_neq(codes[i], codes[j],
                    "YouTube error codes at index %zu and %zu should be distinct", i, j);
    }
  }
}
