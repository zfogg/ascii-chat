/**
 * @file youtube.c
 * @brief YouTube URL extraction and stream URL resolution implementation
 * @ingroup media
 */

#include "youtube.h"
#include "common.h"
#include "log/logging.h"
#include "asciichat_errno.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// FFmpeg includes for HTTP support
#include <libavformat/avio.h>
#include <libavformat/avformat.h>

// libytdl includes
#include <ytdl/info.h>

/**
 * @brief YouTube URL patterns (regex for detection)
 *
 * Matches:
 * - https://www.youtube.com/watch?v=ID
 * - https://youtube.com/watch?v=ID
 * - https://m.youtube.com/watch?v=ID
 * - https://youtu.be/ID
 * - http variants
 */
#define YOUTUBE_URL_PATTERN "^https?://(www\\.|m\\.)?youtube\\.com/watch\\?v=|^https?://youtu\\.be/"

/** Maximum size for a YouTube video ID (11 characters) */
#define YOUTUBE_VIDEO_ID_MAX 16

/** Maximum size for the watch page HTML cache (1 MB) */
#define WATCH_PAGE_MAX_SIZE (1024 * 1024)

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

  // Extract up to 11 characters or until & or end of string
  size_t id_len = 0;
  while (id_len < 11 && video_id_ptr[id_len] && video_id_ptr[id_len] != '&' && video_id_ptr[id_len] != '#') {
    id_len++;
  }

  if (id_len == 0 || id_len > 11) {
    SET_ERRNO(ERROR_YOUTUBE_INVALID_URL, "Invalid video ID length: %zu", id_len);
    return ERROR_YOUTUBE_INVALID_URL;
  }

  // Copy to output buffer
  if (id_size < id_len + 1) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Output buffer too small for video ID");
    return ERROR_INVALID_PARAM;
  }

  strncpy(output_id, video_id_ptr, id_len);
  output_id[id_len] = '\0';

  log_debug("Extracted YouTube video ID: %s", output_id);
  return ASCIICHAT_OK;
}

/**
 * @brief Fetch YouTube watch page HTML via HTTP using FFmpeg AVIO
 *
 * Uses FFmpeg's AVIO (Audio/Video Input/Output) to fetch the watch page HTML
 * via HTTP/HTTPS. FFmpeg's HTTP support works cross-platform and is already
 * a dependency of ascii-chat.
 *
 * @param video_id Video ID (11 characters)
 * @param output_html Output buffer for HTML
 * @param html_size Size of output buffer
 * @return Length of HTML downloaded, or 0 on failure
 *
 * @note This is called internally only
 */
static size_t youtube_fetch_watch_page(const char *video_id, uint8_t *output_html, size_t html_size) {
  if (!video_id || !output_html || html_size == 0) {
    return 0;
  }

  // Build URL to watch page
  char watch_url[256];
  snprintf(watch_url, sizeof(watch_url), "https://www.youtube.com/watch?v=%s", video_id);

  log_debug("Fetching YouTube watch page: %s", watch_url);

  AVIOContext *io_ctx = NULL;
  size_t bytes_read = 0;

  // Open URL for reading via FFmpeg AVIO
  int ret = avio_open(&io_ctx, watch_url, AVIO_FLAG_READ);
  if (ret < 0) {
    log_error("Failed to open YouTube watch page URL: %s (error: %d)", watch_url, ret);
    SET_ERRNO(ERROR_YOUTUBE_NETWORK, "Failed to open HTTP connection to YouTube: %s", watch_url);
    return 0;
  }

  // Get file size if available
  int64_t file_size = avio_size(io_ctx);
  if (file_size > 0 && (size_t)file_size > html_size) {
    log_warn("YouTube watch page (%lld bytes) exceeds buffer size (%zu bytes), will be truncated",
             file_size, html_size);
  }

  // Read HTML content into buffer
  // avio_read returns the number of bytes read (0 for EOF, negative for errors)
  int read_size = avio_read(io_ctx, output_html, (int)html_size - 1);
  if (read_size < 0) {
    log_error("Failed to read YouTube watch page (error: %d)", read_size);
    avio_close(io_ctx);
    SET_ERRNO(ERROR_YOUTUBE_NETWORK, "Failed to read HTTP response from YouTube");
    return 0;
  }

  bytes_read = (size_t)read_size;

  // Null-terminate the HTML for safety
  if (bytes_read < html_size) {
    output_html[bytes_read] = '\0';
  } else {
    output_html[html_size - 1] = '\0';
  }

  // Close the AVIO context
  avio_close(io_ctx);

  log_debug("Successfully fetched %zu bytes of YouTube watch page HTML", bytes_read);
  return bytes_read;
}

/**
 * @brief Extract direct stream URL from YouTube video URL
 *
 * Main function for YouTube URL extraction using libytdl.
 */
asciichat_error_t youtube_extract_stream_url(const char *youtube_url, char *output_url, size_t output_size) {
  if (!youtube_url || !output_url || output_size < 256) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "Invalid parameters for stream URL extraction (output_size must be >= 256)");
    return ERROR_INVALID_PARAM;
  }

  // Verify it's a YouTube URL
  if (!youtube_is_youtube_url(youtube_url)) {
    SET_ERRNO(ERROR_YOUTUBE_INVALID_URL, "Not a valid YouTube URL: %s", youtube_url);
    return ERROR_YOUTUBE_INVALID_URL;
  }

  // Extract video ID
  char video_id[YOUTUBE_VIDEO_ID_MAX];
  asciichat_error_t err = youtube_extract_video_id(youtube_url, video_id, sizeof(video_id));
  if (err != ASCIICHAT_OK) {
    return err;
  }

  log_info("Extracting stream URL for YouTube video: %s", video_id);

  // Fetch watch page HTML
  uint8_t *watch_html = SAFE_MALLOC(WATCH_PAGE_MAX_SIZE, uint8_t *);
  if (!watch_html) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate watch page buffer");
    return ERROR_MEMORY;
  }

  size_t html_len = youtube_fetch_watch_page(video_id, watch_html, WATCH_PAGE_MAX_SIZE);
  if (html_len == 0) {
    SAFE_FREE(watch_html);
    SET_ERRNO(ERROR_YOUTUBE_NETWORK, "Failed to fetch YouTube watch page for video: %s", video_id);
    return ERROR_YOUTUBE_NETWORK;
  }

  log_debug("Fetched %zu bytes of watch page HTML", html_len);

  // Initialize libytdl info context
  ytdl_info_ctx_t info = {0};
  ytdl_info_ctx_init(&info);

  // Extract video info from HTML
  int extract_result = ytdl_info_extract_watch_html(&info, watch_html, html_len);
  if (extract_result != 0) {
    log_error("Failed to extract video info from HTML (result: %d)", extract_result);
    ytdl_info_ctx_free(&info);
    SAFE_FREE(watch_html);
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "Failed to parse YouTube watch page HTML");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Check if video is playable
  int playability_status = ytdl_info_get_playability_status(&info);
  if (playability_status != YTDL_PLAYABILITY_OK) {
    const char *status_msg = ytdl_info_get_playability_status_message(&info);
    if (!status_msg) {
      status_msg = "Unknown";
    }
    log_error("Video is not playable: %s", status_msg);
    ytdl_info_ctx_free(&info);
    SAFE_FREE(watch_html);
    SET_ERRNO(ERROR_YOUTUBE_UNPLAYABLE, "Video is not playable: %s", status_msg);
    return ERROR_YOUTUBE_UNPLAYABLE;
  }

  // Extract formats
  int format_result = ytdl_info_extract_formats(&info);
  if (format_result != 0) {
    log_error("Failed to extract formats from video info (result: %d)", format_result);
    ytdl_info_ctx_free(&info);
    SAFE_FREE(watch_html);
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "Failed to extract video formats");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Get best video format (prefers video+audio, falls back to audio-only)
  size_t best_format_idx = ytdl_info_get_best_video_format(&info);
  if (best_format_idx == (size_t)-1) {
    // Try audio-only
    log_warn("No video format found, trying audio-only");
    best_format_idx = ytdl_info_get_best_audio_format(&info);
    if (best_format_idx == (size_t)-1) {
      log_error("No playable formats found");
      ytdl_info_ctx_free(&info);
      SAFE_FREE(watch_html);
      SET_ERRNO(ERROR_YOUTUBE_UNPLAYABLE, "No playable video or audio formats found");
      return ERROR_YOUTUBE_UNPLAYABLE;
    }
  }

  // Get stream URL
  const char *stream_url = ytdl_info_get_format_url(&info, best_format_idx);
  if (!stream_url || stream_url[0] == '\0') {
    log_error("Failed to get stream URL for format %zu", best_format_idx);
    ytdl_info_ctx_free(&info);
    SAFE_FREE(watch_html);
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "Failed to extract stream URL from format");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Copy to output
  if (strlen(stream_url) >= output_size) {
    log_error("Stream URL too long for output buffer: %zu >= %zu", strlen(stream_url), output_size);
    ytdl_info_ctx_free(&info);
    SAFE_FREE(watch_html);
    SET_ERRNO(ERROR_INVALID_PARAM, "Output buffer too small for stream URL");
    return ERROR_INVALID_PARAM;
  }

  strcpy(output_url, stream_url);
  log_info("Successfully extracted YouTube stream URL (length: %zu)", strlen(stream_url));

  // Cleanup
  ytdl_info_ctx_free(&info);
  SAFE_FREE(watch_html);

  return ASCIICHAT_OK;
}
