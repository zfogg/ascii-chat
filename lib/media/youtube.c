/**
 * @file youtube.c
 * @brief YouTube URL extraction and stream URL resolution implementation
 * @ingroup media
 */

#include "youtube.h"
#include "common.h"
#include "log/logging.h"
#include "asciichat_errno.h"
#include <version.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// FFmpeg includes for HTTP support
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>

// libytdl includes
#include <ytdl/info.h>
#include <ytdl/sig.h>

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

/** Maximum size for the watch page HTML cache (5 MB - YouTube pages can be large) */
#define WATCH_PAGE_MAX_SIZE (5 * 1024 * 1024)

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
  AVDictionary *opts = NULL;

  // Set HTTP headers to match ascii-chat's HTTP client
  // Use same User-Agent as other HTTPS requests in ascii-chat
  char user_agent[128];
  snprintf(user_agent, sizeof(user_agent), "ascii-chat/%s", ASCII_CHAT_VERSION_STRING);
  av_dict_set(&opts, "user_agent", user_agent, 0);
  av_dict_set(&opts, "referer", "https://www.youtube.com/", 0);
  av_dict_set(&opts, "accept",
              "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8",
              0);
  av_dict_set(&opts, "accept-language", "en-US,en;q=0.9", 0);
  av_dict_set(&opts, "accept-encoding", "gzip, deflate", 0);
  av_dict_set(&opts, "cache-control", "no-cache", 0);

  // Open URL for reading via FFmpeg AVIO with options
  int ret = avio_open2(&io_ctx, watch_url, AVIO_FLAG_READ, NULL, &opts);
  av_dict_free(&opts);

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
 * @brief Fetch and extract signature actions from YouTube player
 *
 * Fetches the player.js file and extracts the signature decryption actions.
 * These actions are needed to decipher the encrypted format URLs.
 *
 * @param player_url Path to the player (e.g., "/s/player/abc123/player.js")
 * @param sig_actions Output structure to store extracted signature actions
 * @return 0 on success, non-zero on failure
 *
 * @note This is called internally only
 */
static int youtube_fetch_and_extract_sig_actions(const char *player_url, ytdl_sig_actions_t *sig_actions) {
  if (!player_url || !sig_actions) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for signature action extraction");
    return -1;
  }

  // Build full URL to player.js
  char full_player_url[512];
  snprintf(full_player_url, sizeof(full_player_url), "https://www.youtube.com%s", player_url);

  log_debug("Fetching YouTube player: %s", full_player_url);

  AVIOContext *io_ctx = NULL;
  AVDictionary *opts = NULL;

  // Set HTTP headers for player.js fetch
  char user_agent[128];
  snprintf(user_agent, sizeof(user_agent), "ascii-chat/%s", ASCII_CHAT_VERSION_STRING);
  av_dict_set(&opts, "user_agent", user_agent, 0);
  av_dict_set(&opts, "referer", "https://www.youtube.com/", 0);

  // Open player.js URL
  int ret = avio_open2(&io_ctx, full_player_url, AVIO_FLAG_READ, NULL, &opts);
  av_dict_free(&opts);

  if (ret < 0) {
    log_warn("Failed to fetch player.js: %s (error: %d)", full_player_url, ret);
    return -1;
  }

  // Allocate buffer for player code (typically 100KB-500KB)
  size_t player_buffer_size = 1024 * 1024; // 1MB max
  uint8_t *player_code = SAFE_MALLOC(player_buffer_size, uint8_t *);
  if (!player_code) {
    avio_close(io_ctx);
    log_error("Failed to allocate buffer for player code");
    return -1;
  }

  // Read player.js
  int read_size = avio_read(io_ctx, player_code, (int)player_buffer_size - 1);
  avio_close(io_ctx);

  if (read_size <= 0) {
    SAFE_FREE(player_code);
    log_warn("Failed to read player.js (size: %d)", read_size);
    return -1;
  }

  player_code[read_size] = '\0';
  log_debug("Fetched player.js (%d bytes)", read_size);

  // Initialize signature actions
  if (ytdl_sig_actions_init(sig_actions) != 0) {
    SAFE_FREE(player_code);
    log_error("Failed to initialize signature actions");
    return -1;
  }

  // Extract signature actions from player code
  // This parses the player.js to find the decipher algorithm
  if (ytdl_sig_actions_extract(sig_actions, (const uint8_t *)player_code, read_size) != 0) {
    ytdl_sig_actions_free(sig_actions);
    SAFE_FREE(player_code);
    log_warn("Failed to extract signature actions from player");
    return -1;
  }

  SAFE_FREE(player_code);
  log_debug("Successfully extracted signature actions from player");
  return 0;
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

  // Get player URL and fetch signature actions to decipher format URLs
  const char *player_url = ytdl_info_get_player_url(&info);
  if (player_url && player_url[0] != '\0') {
    ytdl_sig_actions_t sig_actions = {0};

    // Try to fetch and extract signature actions
    if (youtube_fetch_and_extract_sig_actions(player_url, &sig_actions) == 0) {
      // Set signature actions on the info context for format URL decryption
      ytdl_info_set_sig_actions(&info, &sig_actions);
      log_debug("Signature actions set for format URL decryption");
    } else {
      log_warn("Could not fetch signature actions, format URLs may not decrypt properly");
    }
  } else {
    log_warn("No player URL found in video info");
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
