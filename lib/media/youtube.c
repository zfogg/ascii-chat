/**
 * @file youtube.c
 * @brief YouTube URL extraction and stream URL resolution using yt-dlp
 * @ingroup media
 *
 * This module provides YouTube URL detection and extraction of direct stream URLs
 * by calling yt-dlp as a subprocess. yt-dlp is actively maintained and handles
 * YouTube's modern cipher and n-parameter protections.
 */

#include "youtube.h"
#include "common.h"
#include "log/logging.h"
#include "asciichat_errno.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

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
asciichat_error_t youtube_extract_stream_url(const char *youtube_url, char *output_url,
                                              size_t output_size) {
  if (!youtube_url || !output_url || output_size < 256) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for YouTube URL extraction");
    return ERROR_INVALID_PARAM;
  }

  // Check if yt-dlp is available
  if (!youtube_check_ytdlp_available()) {
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED,
              "yt-dlp is not installed. Please install it with: "
              "pip install yt-dlp (or: brew install yt-dlp on macOS)");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Verify it's a YouTube URL
  if (!youtube_is_youtube_url(youtube_url)) {
    SET_ERRNO(ERROR_YOUTUBE_INVALID_URL, "URL is not a YouTube URL: %s", youtube_url);
    return ERROR_YOUTUBE_INVALID_URL;
  }

  // Build command to run yt-dlp and extract the URL directly
  // Try best available format (works with most videos)
  char command[2048];
  // Note: %(url)s is for yt-dlp's -O option, not snprintf format string
  // Using literal string to avoid format string warnings
  int cmd_ret = snprintf(command, sizeof(command), "%s%s%s",
                         "yt-dlp --quiet --no-warnings "
                         "-f 'b' "
                         "-O '%(url)s' '",
                         youtube_url,
                         "' 2>/dev/null");

  if (cmd_ret < 0 || cmd_ret >= (int)sizeof(command)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "YouTube URL too long");
    return ERROR_INVALID_PARAM;
  }

  // Execute yt-dlp and capture URL output
  FILE *pipe = popen(command, "r");
  if (!pipe) {
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "Failed to execute yt-dlp subprocess");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Read URL output from yt-dlp (should be a single line)
  char url_buffer[8192];
  size_t url_size = 0;
  int c;
  while ((c = fgetc(pipe)) != EOF && c != '\n' && url_size < sizeof(url_buffer) - 1) {
    url_buffer[url_size++] = (char)c;
  }
  url_buffer[url_size] = '\0';

  int pclose_ret = pclose(pipe);
  if (pclose_ret != 0) {
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED,
              "yt-dlp failed to extract video information. "
              "Video may be age-restricted, geo-blocked, or private.");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  if (url_size == 0 || (url_size == 2 && strncmp(url_buffer, "NA", 2) == 0)) {
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp returned empty output - no playable formats");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  // Validate the URL starts with http
  if (url_buffer[0] != 'h' || strncmp(url_buffer, "http", 4) != 0) {
    log_debug("Invalid URL from yt-dlp: %s", url_buffer);
    SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED,
              "yt-dlp returned invalid URL. Video may not be playable.");
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }

  if (url_size >= output_size) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Stream URL too long for output buffer (%zu bytes, max %zu)",
              url_size, output_size);
    return ERROR_INVALID_PARAM;
  }

  // Copy the URL directly (it's already properly formatted)
  strncpy(output_url, url_buffer, output_size - 1);
  output_url[output_size - 1] = '\0';

  log_debug("Successfully extracted YouTube stream URL (%zu bytes)", url_size);
  return ASCIICHAT_OK;
}
