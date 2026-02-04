/**
 * @file youtube.c
 * @brief YouTube URL extraction and stream URL resolution using yt-dlp
 * @ingroup media
 *
 * This module provides YouTube URL detection and extraction of direct stream URLs
 * by calling yt-dlp as a subprocess. yt-dlp is actively maintained and handles
 * YouTube's modern cipher and n-parameter protections.
 */

#include <ascii-chat/media/youtube.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/version.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/process.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/util/url.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>

/* ============================================================================
 * URL Extraction Cache
 * ============================================================================ */

/**
 * @brief Simple cache for extracted YouTube stream URLs
 *
 * Avoids calling yt-dlp multiple times for the same YouTube URL during
 * initialization (FPS detection, audio probing, playback setup).
 *
 * Cache entry is valid for 30 seconds (typical YouTube stream URL duration).
 */
typedef struct {
  char youtube_url[2048]; // Original YouTube URL
  char stream_url[8192];  // Extracted direct stream URL
  time_t extracted_time;  // When URL was extracted
  bool valid;             // Whether cache entry is valid
} youtube_cache_entry_t;

static youtube_cache_entry_t g_youtube_cache = {0};

/**
 * @brief Check if cached URL is still valid
 *
 * YouTube stream URLs expire after ~6 hours, but we use a shorter
 * cache duration (30 seconds) to be safe during initialization.
 */
static bool youtube_cache_is_valid(void) {
  if (!g_youtube_cache.valid) {
    return false;
  }

  time_t now = time(NULL);
  time_t age = now - g_youtube_cache.extracted_time;

  // Cache valid for 30 seconds
  return (age >= 0 && age < 30);
}

/**
 * @brief Get cached stream URL if available and valid
 *
 * Returns true for both successful and failed cached extractions.
 * Check if output_url is empty to detect cached failures.
 */
static bool youtube_cache_get(const char *youtube_url, char *output_url, size_t output_size) {
  if (!youtube_cache_is_valid()) {
    return false;
  }

  // Check if cache matches requested URL
  if (strcmp(g_youtube_cache.youtube_url, youtube_url) != 0) {
    return false;
  }

  // Return cached URL (may be empty if cached failure)
  size_t url_len = strlen(g_youtube_cache.stream_url);
  if (url_len >= output_size) {
    return false;
  }

  strncpy(output_url, g_youtube_cache.stream_url, output_size - 1);
  output_url[output_size - 1] = '\0';

  if (url_len == 0) {
    // Cached failure - don't log debug message
    log_debug("Using cached failure for YouTube URL (failed %ld seconds ago)",
              (long)(time(NULL) - g_youtube_cache.extracted_time));
  } else {
    log_debug("Using cached YouTube stream URL (extracted %ld seconds ago)",
              (long)(time(NULL) - g_youtube_cache.extracted_time));
  }
  return true;
}

/**
 * @brief Cache extracted stream URL or failure
 *
 * If stream_url is empty/NULL, caches a failure state so we don't retry
 * the same failed URL multiple times during initialization.
 */
static void youtube_cache_set(const char *youtube_url, const char *stream_url) {
  if (strlen(youtube_url) >= sizeof(g_youtube_cache.youtube_url)) {
    return; // URL too long to cache
  }

  if (stream_url && strlen(stream_url) >= sizeof(g_youtube_cache.stream_url)) {
    return; // Stream URL too long to cache
  }

  strncpy(g_youtube_cache.youtube_url, youtube_url, sizeof(g_youtube_cache.youtube_url) - 1);
  g_youtube_cache.youtube_url[sizeof(g_youtube_cache.youtube_url) - 1] = '\0';

  if (stream_url) {
    strncpy(g_youtube_cache.stream_url, stream_url, sizeof(g_youtube_cache.stream_url) - 1);
  } else {
    // Mark as empty (failure state)
    g_youtube_cache.stream_url[0] = '\0';
  }
  g_youtube_cache.stream_url[sizeof(g_youtube_cache.stream_url) - 1] = '\0';

  g_youtube_cache.extracted_time = time(NULL);
  g_youtube_cache.valid = true;
}

/**
 * @brief Check if a URL is a YouTube URL
 */
bool youtube_is_youtube_url(const char *url) {
  if (!url) {
    return false;
  }

  // Quick checks for common YouTube domains
  if (strstr(url, "youtube.com") != NULL || strstr(url, "youtu.be") != NULL) {
    // Verify it has watch?v= or youtu.be/ pattern
    if (strstr(url, "watch?v=") != NULL) {
      return true;
    }
    if (strstr(url, "youtu.be/") != NULL) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Extract YouTube video ID from a URL
 *
 * Handles formats:
 * - youtube.com/watch?v=VIDEOID
 * - youtu.be/VIDEOID
 */
asciichat_error_t youtube_extract_video_id(const char *url, char *output_id, size_t id_size) {
  if (!url || !output_id || id_size < 12) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for video ID extraction");
    return ERROR_INVALID_PARAM;
  }

  // Verify it's a YouTube URL
  if (!youtube_is_youtube_url(url)) {
    SET_ERRNO(ERROR_YOUTUBE_INVALID_URL, "URL is not a YouTube URL: %s", url);
    return ERROR_YOUTUBE_INVALID_URL;
  }

  const char *video_id_ptr = NULL;

  // Try watch?v= pattern
  video_id_ptr = strstr(url, "watch?v=");
  if (video_id_ptr) {
    video_id_ptr += strlen("watch?v=");
  } else {
    // Try youtu.be/ pattern
    video_id_ptr = strstr(url, "youtu.be/");
    if (video_id_ptr) {
      video_id_ptr += strlen("youtu.be/");
    }
  }

  if (!video_id_ptr) {
    SET_ERRNO(ERROR_YOUTUBE_INVALID_URL, "Could not extract video ID from URL: %s", url);
    return ERROR_YOUTUBE_INVALID_URL;
  }

  // Extract the video ID (11 characters, alphanumeric, -, _)
  size_t id_len = 0;
  for (size_t i = 0; i < 16 && video_id_ptr[i] != '\0' && video_id_ptr[i] != '&'; i++) {
    if (isalnum(video_id_ptr[i]) || video_id_ptr[i] == '-' || video_id_ptr[i] == '_') {
      id_len++;
    } else {
      break;
    }
  }

  if (id_len < 10 || id_len > 12) {
    SET_ERRNO(ERROR_YOUTUBE_INVALID_URL, "Invalid video ID length (%zu) in URL: %s", id_len, url);
    return ERROR_YOUTUBE_INVALID_URL;
  }

  if (id_len >= id_size) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Video ID buffer too small (need %zu bytes)", id_len + 1);
    return ERROR_INVALID_PARAM;
  }

  strncpy(output_id, video_id_ptr, id_len);
  output_id[id_len] = '\0';

  return ASCIICHAT_OK;
}

/**
 * @brief Check if yt-dlp is installed and accessible
 * @return true if yt-dlp is found and executable, false otherwise
 */
static bool youtube_check_ytdlp_available(void) {
  // Try to run yt-dlp --version
  int ret = system("yt-dlp --version >/dev/null 2>&1");
  return (ret == 0);
}

/**
 * @brief Extract direct stream URL from YouTube video URL using yt-dlp
 *
 * Calls yt-dlp as subprocess with --dump-json to get video information,
 * then parses the JSON to extract the direct stream URL.
 */
asciichat_error_t youtube_extract_stream_url(const char *youtube_url, char *output_url, size_t output_size) {
  if (!youtube_url || !output_url || output_size < 256) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for YouTube URL extraction");
    return ERROR_INVALID_PARAM;
  }

  // Check if yt-dlp is available
  if (!youtube_check_ytdlp_available()) {
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp is not installed. Please install it with: "
                                            "pip install yt-dlp (or: brew install yt-dlp on macOS)");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Verify it's a YouTube URL
  if (!youtube_is_youtube_url(youtube_url)) {
    SET_ERRNO(ERROR_YOUTUBE_INVALID_URL, "URL is not a YouTube URL: %s", youtube_url);
    return ERROR_YOUTUBE_INVALID_URL;
  }

  // Check if we have a cached extraction for this URL
  char cached_url[8192] = {0};
  if (youtube_cache_get(youtube_url, cached_url, sizeof(cached_url))) {
    if (cached_url[0] != '\0') {
      // Cached success - return the URL
      strncpy(output_url, cached_url, output_size - 1);
      output_url[output_size - 1] = '\0';
      return ASCIICHAT_OK;
    } else {
      // Cached failure - return error without logging again (already logged on first attempt)
      return ERROR_YOUTUBE_EXTRACT_FAILED;
    }
  }

  // Build command to run yt-dlp and extract the URL directly
  char command[2048];
  // Note: %(url)s is for yt-dlp's -O option, not snprintf format string

  int cmd_ret;
  const char *cookies_value = GET_OPTION(cookies_from_browser);

  // Build yt-dlp command based on cookies preference
  bool disable_cookies = GET_OPTION(no_cookies_from_browser);

  if (disable_cookies) {
    // User explicitly disabled with --no-cookies-from-browser
    cmd_ret =
        safe_snprintf(command, sizeof(command),
                      "yt-dlp --quiet --no-warnings "
                      "--user-agent 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like "
                      "Gecko) Chrome/120.0.0.0 Safari/537.36' "
                      "--no-cookies-from-browser "
                      "-f 'b' -O '%%(url)s' '%s' 2>&1",
                      youtube_url);
  } else {
    // User enabled --cookies-from-browser (with optional browser/keyring specification)
    if (cookies_value && cookies_value[0] != '\0') {
      // Specific browser/keyring provided
      cmd_ret =
          safe_snprintf(command, sizeof(command),
                        "yt-dlp --quiet --no-warnings "
                        "--user-agent 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, "
                        "like Gecko) Chrome/120.0.0.0 Safari/537.36' "
                        "--cookies-from-browser '%s' "
                        "-f 'b' -O '%%(url)s' '%s'",
                        cookies_value, youtube_url);
    } else {
      // No browser specified - use without cookies for better compatibility
      // Note: Using --cookies-from-browser can trigger YouTube's stricter n-challenge
      // signature solving which frequently breaks when YouTube updates their player code.
      // Using --no-cookies-from-browser works around this by using alternative extraction.
      cmd_ret =
          safe_snprintf(command, sizeof(command),
                        "yt-dlp --quiet --no-warnings "
                        "--user-agent 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, "
                        "like Gecko) Chrome/120.0.0.0 Safari/537.36' "
                        "--no-cookies-from-browser "
                        "-f 'b' -O '%%(url)s' '%s'",
                        youtube_url);
    }
  }

  if (cmd_ret < 0 || cmd_ret >= (int)sizeof(command)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "YouTube URL too long");
    return ERROR_INVALID_PARAM;
  }

  // Debug: log the command being executed
  log_debug("Executing: %s", command);

  // Execute yt-dlp and capture URL output (stderr is shown in debug builds for troubleshooting)
  FILE *pipe = NULL;
  if (platform_popen(command, "r", &pipe) != ASCIICHAT_OK || !pipe) {
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "Failed to execute yt-dlp subprocess");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Read all output from yt-dlp (may contain warnings, errors, and the URL)
  // The URL should be on the last line starting with "http"
  char url_buffer[8192] = {0};
  char full_output[16384] = {0}; // Capture all output for error reporting
  size_t ytdlp_output_len = 0;
  size_t url_size = 0;
  int c;

  // Read all output character by character, looking for http:// or https:// URL
  while ((c = fgetc(pipe)) != EOF && ytdlp_output_len < sizeof(full_output) - 1) {
    full_output[ytdlp_output_len++] = (char)c;

    // Track current line - looking for http:// or https:// stream URL
    if ((url_size == 0 && c == 'h') || (url_size > 0 && c != '\n' && url_size < sizeof(url_buffer) - 1)) {
      // Start of URL or continue building potential URL
      url_buffer[url_size++] = (char)c;
    } else if (c == '\n') {
      // End of line - check if what we collected is a valid URL using production-grade PCRE2 validator
      if (url_size > 0) {
        // Null-terminate the buffer for validation
        url_buffer[url_size] = '\0';
        // Validate using PCRE2-based production-grade URL regex
        if (url_is_valid(url_buffer)) {
          // Found valid HTTP(S) URL, break out of read loop
          break;
        }
      }
      // Not a valid URL or empty line, reset for next line
      url_size = 0;
    }
  }
  full_output[ytdlp_output_len] = '\0';
  url_buffer[url_size] = '\0';

  int pclose_ret = (platform_pclose(&pipe) == ASCIICHAT_OK) ? 0 : -1;
  if (pclose_ret != 0) {
    // Cache the failure so we don't retry this URL
    youtube_cache_set(youtube_url, NULL);

    log_debug("yt-dlp exited with code %d", pclose_ret);

    // Log yt-dlp stderr output if available
    if (ytdlp_output_len > 0) {
      log_error("yt-dlp stderr:\n%s", full_output);
      SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp failed to extract video. See logs for details:\n%s", full_output);
    } else {
      SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp failed to extract video information");
    }
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  if (url_size == 0 || (url_size == 2 && strncmp(url_buffer, "NA", 2) == 0)) {
    // Cache the failure
    youtube_cache_set(youtube_url, NULL);

    log_error("yt-dlp returned empty output - no playable formats found for URL: %s", youtube_url);
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp returned empty output - no playable formats");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Validate the URL starts with http
  if (url_buffer[0] != 'h' || strncmp(url_buffer, "http", 4) != 0) {
    // Cache the failure
    youtube_cache_set(youtube_url, NULL);

    log_error("Invalid URL from yt-dlp: %s (full output: %s)", url_buffer, full_output);
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp returned invalid URL. Video may not be playable.");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  if (url_size >= output_size) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Stream URL too long for output buffer (%zu bytes, max %zu)", url_size, output_size);
    return ERROR_INVALID_PARAM;
  }

  // Copy the URL directly (it's already properly formatted)
  strncpy(output_url, url_buffer, output_size - 1);
  output_url[output_size - 1] = '\0';

  // Cache the extracted URL to avoid redundant yt-dlp calls during initialization
  // (FPS detection, audio probing, playback setup)
  youtube_cache_set(youtube_url, output_url);

  log_debug("Successfully extracted YouTube stream URL (%zu bytes)", url_size);
  return ASCIICHAT_OK;
}
