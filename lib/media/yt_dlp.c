/**
 * @file yt_dlp.c
 * @brief yt-dlp stream URL extraction implementation
 * @ingroup media
 */

#include <ascii-chat/media/yt_dlp.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/process.h>
#include <ascii-chat/util/url.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/* ============================================================================
 * URL Extraction Cache
 * ============================================================================ */

/**
 * @brief Cache entry for extracted stream URLs
 *
 * Avoids calling yt-dlp multiple times for the same URL during
 * initialization (FPS detection, audio probing, playback setup).
 *
 * Cache entry is valid for 30 seconds (typical stream URL duration).
 */
typedef struct {
  char url[2048];           // Original URL
  char stream_url[8192];    // Extracted stream URL
  char yt_dlp_options[512]; // yt-dlp options used
  time_t extracted_time;    // When URL was extracted
  bool valid;               // Whether cache entry is valid
} yt_dlp_cache_entry_t;

static yt_dlp_cache_entry_t g_yt_dlp_cache = {0};

/**
 * @brief Check if cached URL is still valid
 *
 * Stream URLs expire after ~6 hours, but we use 30 seconds cache
 * duration to be safe during initialization.
 */
static bool yt_dlp_cache_is_valid(void) {
  if (!g_yt_dlp_cache.valid) {
    return false;
  }

  time_t now = time(NULL);
  time_t age = now - g_yt_dlp_cache.extracted_time;

  // Cache valid for 30 seconds
  return (age >= 0 && age < 30);
}

/**
 * @brief Get cached stream URL if available and valid
 *
 * Returns true if cache hit. Check if output_url is empty to detect
 * cached failures.
 */
static bool yt_dlp_cache_get(const char *url, const char *yt_dlp_options, char *output_url, size_t output_size) {
  if (!yt_dlp_cache_is_valid()) {
    return false;
  }

  // Check if cache matches requested URL and options
  if (strcmp(g_yt_dlp_cache.url, url) != 0) {
    return false;
  }

  // For cache key, compare options (NULL == "" for consistency)
  const char *cached_opts = g_yt_dlp_cache.yt_dlp_options;
  const char *req_opts = yt_dlp_options ? yt_dlp_options : "";
  if (strcmp(cached_opts, req_opts) != 0) {
    return false;
  }

  // Return cached URL (may be empty if cached failure)
  size_t url_len = strlen(g_yt_dlp_cache.stream_url);
  if (url_len >= output_size) {
    return false;
  }

  SAFE_STRNCPY(output_url, g_yt_dlp_cache.stream_url, output_size - 1);
  output_url[output_size - 1] = '\0';

  if (url_len == 0) {
    log_debug("Using cached yt-dlp failure for URL (failed %ld seconds ago)",
              (long)(time(NULL) - g_yt_dlp_cache.extracted_time));
  } else {
    log_debug("Using cached yt-dlp stream URL (extracted %ld seconds ago)",
              (long)(time(NULL) - g_yt_dlp_cache.extracted_time));
  }
  return true;
}

/**
 * @brief Cache extracted stream URL or failure
 *
 * If stream_url is empty/NULL, caches a failure state so we don't retry
 * the same failed URL multiple times.
 */
static void yt_dlp_cache_set(const char *url, const char *yt_dlp_options, const char *stream_url) {
  if (strlen(url) >= sizeof(g_yt_dlp_cache.url)) {
    return; // URL too long to cache
  }

  const char *opts = yt_dlp_options ? yt_dlp_options : "";
  if (strlen(opts) >= sizeof(g_yt_dlp_cache.yt_dlp_options)) {
    return; // Options too long to cache
  }

  if (stream_url && strlen(stream_url) >= sizeof(g_yt_dlp_cache.stream_url)) {
    return; // Stream URL too long to cache
  }

  SAFE_STRNCPY(g_yt_dlp_cache.url, url, sizeof(g_yt_dlp_cache.url) - 1);
  g_yt_dlp_cache.url[sizeof(g_yt_dlp_cache.url) - 1] = '\0';

  SAFE_STRNCPY(g_yt_dlp_cache.yt_dlp_options, opts, sizeof(g_yt_dlp_cache.yt_dlp_options) - 1);
  g_yt_dlp_cache.yt_dlp_options[sizeof(g_yt_dlp_cache.yt_dlp_options) - 1] = '\0';

  if (stream_url) {
    SAFE_STRNCPY(g_yt_dlp_cache.stream_url, stream_url, sizeof(g_yt_dlp_cache.stream_url) - 1);
  } else {
    // Mark as empty (failure state)
    g_yt_dlp_cache.stream_url[0] = '\0';
  }
  g_yt_dlp_cache.stream_url[sizeof(g_yt_dlp_cache.stream_url) - 1] = '\0';

  g_yt_dlp_cache.extracted_time = time(NULL);
  g_yt_dlp_cache.valid = true;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

bool yt_dlp_is_available(void) {
  int ret = system("yt-dlp --version >/dev/null 2>&1");
  return (ret == 0);
}

asciichat_error_t yt_dlp_extract_stream_url(const char *url, const char *yt_dlp_options, char *output_url,
                                            size_t output_size) {
  if (!url || !output_url || output_size < 256) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for yt-dlp URL extraction");
    return ERROR_INVALID_PARAM;
  }

  // Check if yt-dlp is available
  if (!yt_dlp_is_available()) {
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp is not installed. Please install it with: "
                                            "pip install yt-dlp (or: brew install yt-dlp on macOS)");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Check if we have a cached extraction for this URL+options combination
  char cached_url[8192] = {0};
  if (yt_dlp_cache_get(url, yt_dlp_options, cached_url, sizeof(cached_url))) {
    if (cached_url[0] != '\0') {
      // Cached success - return the URL
      SAFE_STRNCPY(output_url, cached_url, output_size - 1);
      output_url[output_size - 1] = '\0';
      return ASCIICHAT_OK;
    } else {
      // Cached failure - return error without logging again
      return ERROR_YOUTUBE_EXTRACT_FAILED;
    }
  }

  // Build yt-dlp command
  char command[4096];
  const char *opts = yt_dlp_options ? yt_dlp_options : "";

  int cmd_ret;
  if (opts[0] != '\0') {
    // User provided custom options
    cmd_ret = safe_snprintf(command, sizeof(command),
                            "yt-dlp --quiet --no-warnings "
                            "--user-agent 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                            "AppleWebKit/537.36' "
                            "%s "
                            "-f 'b' -O '%%(url)s' '%s' 2>&1",
                            opts, url);
  } else {
    // No custom options, use default
    cmd_ret = safe_snprintf(command, sizeof(command),
                            "yt-dlp --quiet --no-warnings "
                            "--user-agent 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                            "AppleWebKit/537.36' "
                            "-f 'b' -O '%%(url)s' '%s' 2>&1",
                            url);
  }

  if (cmd_ret < 0 || cmd_ret >= (int)sizeof(command)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "URL or yt-dlp options too long");
    return ERROR_INVALID_PARAM;
  }

  log_debug("Executing yt-dlp: %s", command);

  // Execute yt-dlp and capture output
  FILE *pipe = NULL;
  if (platform_popen("yt_dlp_extract", command, "r", &pipe) != ASCIICHAT_OK || !pipe) {
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "Failed to execute yt-dlp subprocess");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Read output looking for stream URL (should be last line starting with http)
  char url_buffer[8192] = {0};
  char full_output[16384] = {0};
  size_t ytdlp_output_len = 0;
  size_t url_size = 0;
  int c;

  while ((c = fgetc(pipe)) != EOF && ytdlp_output_len < sizeof(full_output) - 1) {
    full_output[ytdlp_output_len++] = (char)c;

    // Track current line - looking for http:// or https:// stream URL
    if ((url_size == 0 && c == 'h') || (url_size > 0 && c != '\n' && url_size < sizeof(url_buffer) - 1)) {
      url_buffer[url_size++] = (char)c;
    } else if (c == '\n') {
      if (url_size > 0) {
        url_buffer[url_size] = '\0';
        if (url_is_valid(url_buffer)) {
          break; // Found valid URL
        }
      }
      url_size = 0; // Reset for next line
    }
  }
  full_output[ytdlp_output_len] = '\0';
  url_buffer[url_size] = '\0';

  int pclose_ret = (platform_pclose(&pipe) == ASCIICHAT_OK) ? 0 : -1;
  if (pclose_ret != 0) {
    yt_dlp_cache_set(url, yt_dlp_options, NULL); // Cache failure

    log_debug("yt-dlp exited with code %d", pclose_ret);
    if (ytdlp_output_len > 0) {
      log_error("yt-dlp output:\n%s", full_output);
      SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp failed to extract stream: %s", full_output);
    } else {
      SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp failed to extract stream");
    }
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  if (url_size == 0 || (url_size == 2 && strncmp(url_buffer, "NA", 2) == 0)) {
    yt_dlp_cache_set(url, yt_dlp_options, NULL);
    log_error("yt-dlp returned empty output for URL: %s", url);
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp returned no playable formats");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  if (url_buffer[0] != 'h' || strncmp(url_buffer, "http", 4) != 0) {
    yt_dlp_cache_set(url, yt_dlp_options, NULL);
    log_error("Invalid URL from yt-dlp: %s (full output: %s)", url_buffer, full_output);
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp returned invalid URL");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  if (url_size >= output_size) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Stream URL too long for output buffer (%zu bytes, max %zu)", url_size, output_size);
    return ERROR_INVALID_PARAM;
  }

  SAFE_STRNCPY(output_url, url_buffer, output_size - 1);
  output_url[output_size - 1] = '\0';

  // Cache successful result
  yt_dlp_cache_set(url, yt_dlp_options, output_url);

  log_debug("yt-dlp successfully extracted stream URL (%zu bytes)", url_size);
  return ASCIICHAT_OK;
}
