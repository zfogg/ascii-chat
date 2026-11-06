
/* Storage capacities in elements; text buffers include the terminating NUL. */
#define MEDIA_YT_DLP_YT_DLP_OPTIONS_BUFFER_SIZE 512
#define MEDIA_YT_DLP_FULL_OUTPUT_BUFFER_SIZE 16384

#include <ascii-chat/common/limits.h>
#include <ascii-chat/otel.h>
/**
 * @file yt_dlp.c
 * @brief yt-dlp stream URL extraction implementation
 * @ingroup media
 */

#include <ascii-chat/media/yt_dlp.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/io.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/process.h>
#include <ascii-chat/util/url.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
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
  char url[MEDIA_URL_CACHE_SIZE];                               // Original URL
  char stream_url[MEDIA_STREAM_URL_SIZE];                       // Video or combined stream URL
  char audio_url[MEDIA_STREAM_URL_SIZE];                        // Separate audio stream, if needed
  char yt_dlp_options[MEDIA_YT_DLP_YT_DLP_OPTIONS_BUFFER_SIZE]; // yt-dlp options used
  time_t extracted_time;                                        // When URL was extracted
  bool valid;                                                   // Whether cache entry is valid
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
static bool yt_dlp_cache_get(const char *url, const char *yt_dlp_options, char *output_url, size_t output_size,
                             char *audio_url, size_t audio_size) {
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
  if (url_len >= output_size || strlen(g_yt_dlp_cache.audio_url) >= audio_size) {
    return false;
  }

  SAFE_STRNCPY(output_url, g_yt_dlp_cache.stream_url, output_size - 1);
  output_url[output_size - 1] = '\0';
  SAFE_STRNCPY(audio_url, g_yt_dlp_cache.audio_url, audio_size - 1);
  audio_url[audio_size - 1] = '\0';

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
static void yt_dlp_cache_set(const char *url, const char *yt_dlp_options, const char *stream_url,
                             const char *audio_url) {
  if (strlen(url) >= sizeof(g_yt_dlp_cache.url)) {
    return; // URL too long to cache
  }

  const char *opts = yt_dlp_options ? yt_dlp_options : "";
  if (strlen(opts) >= sizeof(g_yt_dlp_cache.yt_dlp_options)) {
    return; // Options too long to cache
  }

  if ((stream_url && strlen(stream_url) >= sizeof(g_yt_dlp_cache.stream_url)) ||
      (audio_url && strlen(audio_url) >= sizeof(g_yt_dlp_cache.audio_url))) {
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
  SAFE_STRNCPY(g_yt_dlp_cache.audio_url, audio_url ? audio_url : "", sizeof(g_yt_dlp_cache.audio_url) - 1);
  g_yt_dlp_cache.audio_url[sizeof(g_yt_dlp_cache.audio_url) - 1] = '\0';

  g_yt_dlp_cache.extracted_time = time(NULL);
  g_yt_dlp_cache.valid = true;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

bool yt_dlp_is_available(void) {
  ASCII_OTEL_SCOPE(YTDLP_AVAILABLE);
  ASCII_OTEL_ATTR_STRING("media.extractor", "yt-dlp");
  const char *argv[] = {"yt-dlp", "--version", NULL};
  int ret = 0;
  LOG_IO("yt-dlp", { ret = platform_execute_subprocess("yt-dlp", argv, NULL, 0); });
  return (ret == 0);
}

// Parse the documented options string into arguments without invoking a shell.
// Quotes group whitespace; backslashes escape quotes and whitespace.
static bool yt_dlp_parse_options(const char *options, char *storage, size_t storage_size, const char **argv,
                                 size_t *argc, size_t argv_size) {
  size_t used = 0;
  const char *p = options ? options : "";
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
      ++p;
    if (!*p)
      break;
    if (*argc >= argv_size || used >= storage_size)
      return false;
    argv[(*argc)++] = storage + used;
    char quote = 0;
    while (*p && (quote || (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n'))) {
      if ((*p == '\'' || *p == '"') && (!quote || quote == *p)) {
        quote = quote ? 0 : *p;
        ++p;
        continue;
      }
      if (quote != '\'' && *p == '\\' && p[1] &&
          (p[1] == quote || (!quote && (p[1] == ' ' || p[1] == '\t' || p[1] == '\'' || p[1] == '"'))))
        ++p;
      if (used + 1 >= storage_size)
        return false;
      storage[used++] = *p++;
    }
    if (quote || used >= storage_size)
      return false;
    storage[used++] = '\0';
  }
  return true;
}

// FFmpeg accepts network streams beyond the schemes supported by url_parse().
static bool yt_dlp_is_stream_url(const char *line) {
  static const char *const prefixes[] = {"http://",   "https://",  "rtmp://", "rtmps://", "rtmpe://",  "rtmpt://",
                                         "rtmpte://", "rtmpts://", "rtsp://", "rtsps://", "rtp://",    "srtp://",
                                         "mms://",    "mmsh://",   "mmst://", "ftp://",   "ftps://",   "sftp://",
                                         "srt://",    "rist://",   "tcp://",  "udp://",   "udplite://"};
  for (size_t i = 0; i < ARRAY_SIZE(prefixes); ++i) {
    size_t length = strlen(prefixes[i]);
    if (platform_strncasecmp(line, prefixes[i], length) != 0)
      continue;
    const char *authority = line + length;
    if (!*authority || *authority == '/' || strpbrk(authority, " \t\r\n"))
      return false;
    if (i < 2)
      return url_is_valid(line);
    // RTSP is a demuxer rather than an AVIO protocol.
    if (strcmp(prefixes[i], "rtsp://") == 0 || strcmp(prefixes[i], "rtsps://") == 0)
      return av_find_input_format("rtsp") != NULL;
    const char *protocol = avio_find_protocol_name(line);
    return protocol && strcmp(protocol, "file") != 0;
  }
  return false;
}

asciichat_error_t yt_dlp_extract_stream_urls(const char *url, const char *yt_dlp_options, char *output_url,
                                             size_t output_size, char *audio_url, size_t audio_size) {
  ASCII_OTEL_SCOPE(YTDLP_EXTRACT);
  ASCII_OTEL_ATTR_STRING("media.extractor", "yt-dlp");
  if (output_url && output_size) {
    output_url[0] = '\0';
    output_url[output_size - 1] = '\0';
  }
  if (audio_url && audio_size) {
    audio_url[0] = '\0';
    audio_url[audio_size - 1] = '\0';
  }
  if (!url || !output_url || output_size < 256 || !audio_url || audio_size < 256) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for yt-dlp URL extraction");
  }
  if (yt_dlp_cache_get(url, yt_dlp_options, output_url, output_size, audio_url, audio_size)) {
    ascii_otel_metric(ASCII_OTEL_METRIC_YTDLP_HIT, 1);
    ASCII_OTEL_ATTR_STRING("cache.result", *output_url ? "hit" : "negative_hit");
    if (!*output_url) {
      ascii_otel_metric(ASCII_OTEL_METRIC_YTDLP_NEGATIVE, 1);
      return ERROR_YOUTUBE_EXTRACT_FAILED;
    }
    return ASCIICHAT_OK;
  }

  if (!yt_dlp_is_available()) {
    return SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp is not installed. Please install it with: "
                                                   "pip install yt-dlp (or: brew install yt-dlp on macOS)");
  }
  ascii_otel_metric(ASCII_OTEL_METRIC_YTDLP_MISS, 1);
  ASCII_OTEL_ATTR_STRING("cache.result", "miss");

  enum { YT_DLP_MAX_ARGS = 64 };
  const char *argv[YT_DLP_MAX_ARGS];
  char option_storage[MEDIA_YT_DLP_YT_DLP_OPTIONS_BUFFER_SIZE];
  size_t argc = 0;
  argv[argc++] = "yt-dlp";
  argv[argc++] = "--quiet";
  argv[argc++] = "--no-warnings";
  argv[argc++] = "--no-playlist";
  argv[argc++] = "--user-agent";
  argv[argc++] = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36";
  argv[argc++] = "-f";
  // Only select separate streams when no combined format is available.
  // Prefer H.264 for fallback: some FFmpeg builds cannot decode AV1.
  argv[argc++] = "b/bv[vcodec^=avc1]+ba/bv+ba";
  // A user-provided -f or --format overrides the default selector.
  if (!yt_dlp_parse_options(yt_dlp_options, option_storage, sizeof(option_storage), argv, &argc, YT_DLP_MAX_ARGS - 4)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid or too long yt-dlp options");
  }
  argv[argc++] = "-g";
  argv[argc++] = "--";
  argv[argc++] = url;
  argv[argc] = NULL;

  // Two URLs can each occupy the full stream URL capacity, plus diagnostics.
  char output[MEDIA_YT_DLP_FULL_OUTPUT_BUFFER_SIZE + MEDIA_STREAM_URL_SIZE] = {0};
  int status = 0;
  LOG_IO("yt-dlp", { status = platform_execute_subprocess("yt-dlp", argv, output, sizeof(output)); });
  if (status != 0 || strlen(output) == sizeof(output) - 1) {
    yt_dlp_cache_set(url, yt_dlp_options, NULL, NULL);
    log_error("yt-dlp failed to extract streams (exit %d): %s", status, output);
    return SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp failed to extract streams");
  }

  size_t count = 0;
  char *line = output;
  while (*line) {
    char *end = strpbrk(line, "\r\n");
    if (end)
      *end = '\0';
    if (yt_dlp_is_stream_url(line)) {
      size_t length = strlen(line);
      char *dest = count == 0 ? output_url : audio_url;
      size_t capacity = count == 0 ? output_size : audio_size;
      if (count >= 2 || length >= capacity) {
        return SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp returned invalid or oversized stream URLs");
      }
      memcpy(dest, line, length + 1);
      ++count;
    }
    if (!end)
      break;
    line = end + 1;
    while (*line == '\r' || *line == '\n')
      ++line;
  }
  if (count == 0) {
    yt_dlp_cache_set(url, yt_dlp_options, NULL, NULL);
    log_error("yt-dlp returned no playable stream URL: %s", output);
    return SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp returned no playable formats");
  }
  yt_dlp_cache_set(url, yt_dlp_options, output_url, audio_url);
  log_debug("yt-dlp extracted %zu stream URL(s)", count);
  return ASCIICHAT_OK;
}

asciichat_error_t yt_dlp_extract_stream_url(const char *url, const char *yt_dlp_options, char *output_url,
                                            size_t output_size) {
  char audio_url[MEDIA_STREAM_URL_SIZE];
  return yt_dlp_extract_stream_urls(url, yt_dlp_options, output_url, output_size, audio_url, sizeof(audio_url));
}
