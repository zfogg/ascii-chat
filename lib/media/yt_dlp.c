
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
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/url.h>
#ifndef __EMSCRIPTEN__
#include <libavformat/avformat.h>
#include <yyjson.h>
#include <libavformat/avio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>

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
  yt_dlp_request_options_t video_request, audio_request;
  time_t extracted_time; // When URL was extracted
  bool valid;            // Whether cache entry is valid
} yt_dlp_cache_entry_t;

static yt_dlp_cache_entry_t g_yt_dlp_cache = {0};
static static_mutex_t g_yt_dlp_cache_lock = STATIC_MUTEX_INIT;

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
                             char *audio_url, size_t audio_size, yt_dlp_request_options_t *video_request,
                             yt_dlp_request_options_t *audio_request) {
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
  *video_request = g_yt_dlp_cache.video_request;
  *audio_request = g_yt_dlp_cache.audio_request;

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
static void yt_dlp_cache_set(const char *url, const char *yt_dlp_options, const char *stream_url, const char *audio_url,
                             const yt_dlp_request_options_t *video_request,
                             const yt_dlp_request_options_t *audio_request) {
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

  g_yt_dlp_cache.video_request = *video_request;
  g_yt_dlp_cache.audio_request = *audio_request;
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
  LOG_IO("yt-dlp", { ret = platform_execute_subprocess("yt-dlp", argv, NULL, 0, NULL, NULL); });
  return (ret == 0);
}

static bool yt_dlp_is_available_interruptible(bool (*cancel)(void *), void *data) {
  const char *argv[] = {"yt-dlp", "--version", NULL};
  int ret = 0;
  LOG_IO("yt-dlp", { ret = platform_execute_subprocess("yt-dlp", argv, NULL, 0, cancel, data); });
  return ret == 0;
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
#ifndef __EMSCRIPTEN__
    // RTSP is a demuxer rather than an AVIO protocol.
    if (strcmp(prefixes[i], "rtsp://") == 0 || strcmp(prefixes[i], "rtsps://") == 0)
      return av_find_input_format("rtsp") != NULL;
    const char *protocol = avio_find_protocol_name(line);
    return protocol && strcmp(protocol, "file") != 0;
#else
    return false;
#endif
  }
  return false;
}

static char *yt_dlp_next_cookie_attribute(char **cursor) {
  char *start = *cursor;
  if (!start || !*start)
    return NULL;
  bool quoted = false, escaped = false;
  for (char *p = start; *p; ++p) {
    if (escaped) {
      escaped = false;
      continue;
    }
    if (*p == '\\' && quoted) {
      escaped = true;
      continue;
    }
    if (*p == '"')
      quoted = !quoted;
    if (*p == ';' && !quoted) {
      *p = '\0';
      *cursor = p + 1;
      return start;
    }
  }
  if (quoted)
    return NULL;
  *cursor = NULL;
  return start;
}

// FFmpeg expects CRLF-terminated request headers and newline-separated cookies.
static bool yt_dlp_parse_request(yyjson_val *format, yyjson_val *root, const char *stream,
                                 yt_dlp_request_options_t *request) {
  yyjson_val *headers = yyjson_obj_get(format, "http_headers");
  if (!yyjson_is_obj(headers))
    headers = yyjson_obj_get(root, "http_headers");
  size_t index, max, used = 0;
  yyjson_val *key, *value;
  if (yyjson_is_obj(headers)) {
    yyjson_obj_foreach(headers, index, max, key, value) {
      const char *name = yyjson_get_str(key), *text = yyjson_get_str(value);
      if (!name || !text || strlen(name) != yyjson_get_len(key) || strlen(text) != yyjson_get_len(value) ||
          strpbrk(name, ":\r\n") || strpbrk(text, "\r\n"))
        return false;
      int n = snprintf(request->headers + used, sizeof(request->headers) - used, "%s: %s\r\n", name, text);
      if (n < 0 || (size_t)n >= sizeof(request->headers) - used)
        return false;
      used += (size_t)n;
    }
  }
  yyjson_val *cookie_value = yyjson_obj_get(format, "cookies");
  if (!yyjson_is_str(cookie_value))
    cookie_value = yyjson_obj_get(root, "cookies");
  const char *cookies = yyjson_get_str(cookie_value);
  if (cookies && *cookies) {
    const size_t cookie_length = strlen(cookies);
    if (cookie_length != yyjson_get_len(cookie_value) || cookie_length >= sizeof(request->cookies) ||
        strpbrk(cookies, "\r\n"))
      return false;
    // yt-dlp serializes scoped cookies as semicolon-separated attributes.
    // Start a new Set-Cookie line at each cookie name, retaining its scope.
    char copy[YT_DLP_COOKIES_CAPACITY];
    memcpy(copy, cookies, cookie_length + sizeof(char));
    url_parts_t parts = {0};
    asciichat_errno_scope_t scope = errno_scope_begin();
    int port = url_parse(stream, &parts) == ASCIICHAT_OK ? parts.port : 0;
    if (parts.scheme && ((!platform_strcasecmp(parts.scheme, "http") && port == 80) ||
                         (!platform_strcasecmp(parts.scheme, "https") && port == 443)))
      port = 0;
    url_parts_destroy(&parts);
    errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
    char *context = copy;
    char *token = yt_dlp_next_cookie_attribute(&context);
    used = 0;
    bool first = true;
    while (token) {
      while (isspace((unsigned char)*token))
        ++token;
      bool attribute = !platform_strncasecmp(token, "domain=", 7) || !platform_strncasecmp(token, "path=", 5) ||
                       !platform_strncasecmp(token, "expires=", 8) || !platform_strncasecmp(token, "version=", 8) ||
                       !platform_strcasecmp(token, "secure") || !platform_strcasecmp(token, "httponly");
      if (!attribute && !strchr(token, '='))
        return false;
      // FFmpeg compares cookie domains against its Host header, including a
      // non-default port. Preserve domain scoping when that port is present.
      bool domain = !platform_strncasecmp(token, "domain=", 7);
      int n = port && domain && !strchr(token + 7, ':')
                  ? snprintf(request->cookies + used, sizeof(request->cookies) - used, "%s%s:%d", first ? "" : "; ",
                             token, port)
                  : snprintf(request->cookies + used, sizeof(request->cookies) - used, "%s%s",
                             first       ? ""
                             : attribute ? "; "
                                         : "\n",
                             token);
      if (n < 0 || (size_t)n >= sizeof(request->cookies) - used)
        return false;
      used += (size_t)n;
      first = false;
      token = yt_dlp_next_cookie_attribute(&context);
    }
    if (context)
      return false;
    if (used + 1 >= sizeof(request->cookies))
      return false;
    request->cookies[used++] = '\n';
    request->cookies[used] = '\0';
  }
  return true;
}

asciichat_error_t yt_dlp_extract_with_options(const char *url, const char *yt_dlp_options, char *output_url,
                                              size_t output_size, char *audio_url, size_t audio_size,
                                              yt_dlp_request_options_t *video_request,
                                              yt_dlp_request_options_t *audio_request, bool (*cancel)(void *),
                                              void *data) {
  if (!video_request || !audio_request)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing yt-dlp request options output");
  memset(video_request, 0, sizeof(*video_request));
  memset(audio_request, 0, sizeof(*audio_request));
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
  static_mutex_lock_named(&g_yt_dlp_cache_lock, "yt_dlp_cache");
  bool cached = yt_dlp_cache_get(url, yt_dlp_options, output_url, output_size, audio_url, audio_size, video_request,
                                 audio_request);
  static_mutex_unlock(&g_yt_dlp_cache_lock);
  if (cached) {
    ascii_otel_metric(ASCII_OTEL_METRIC_YTDLP_HIT, 1);
    ASCII_OTEL_ATTR_STRING("cache.result", *output_url ? "hit" : "negative_hit");
    if (!*output_url) {
      ascii_otel_metric(ASCII_OTEL_METRIC_YTDLP_NEGATIVE, 1);
      return ERROR_YOUTUBE_EXTRACT_FAILED;
    }
    return ASCIICHAT_OK;
  }

  if (!yt_dlp_is_available_interruptible(cancel, data)) {
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
  if (!yt_dlp_parse_options(yt_dlp_options, option_storage, sizeof(option_storage), argv, &argc, YT_DLP_MAX_ARGS - 5)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid or too long yt-dlp options");
  }
  // Pass the explicit yt-dlp proxy to the independent FFmpeg requests.
  bool skip_proxy_value = false;
  for (size_t i = 0; i < argc; ++i) {
    if (skip_proxy_value) {
      skip_proxy_value = false;
      continue;
    }
    const char *proxy = NULL;
    if (!strcmp(argv[i], "--proxy") && i + 1 < argc) {
      proxy = argv[i + 1];
      skip_proxy_value = true;
    } else if (!strncmp(argv[i], "--proxy=", sizeof("--proxy=") - 1)) {
      proxy = argv[i] + sizeof("--proxy=") - 1;
    }
    if (proxy) {
      const size_t proxy_length = strlen(proxy);
      if (proxy_length >= sizeof(video_request->proxy))
        return SET_ERRNO(ERROR_NOT_SUPPORTED, "yt-dlp proxy is too long for media requests");
      memcpy(video_request->proxy, proxy, proxy_length + sizeof(char));
      memcpy(audio_request->proxy, proxy, proxy_length + sizeof(char));
      video_request->proxy_specified = audio_request->proxy_specified = true;
    }
  }
  if (video_request->proxy_specified && video_request->proxy[0] && strncmp(video_request->proxy, "http://", 7))
    return SET_ERRNO(ERROR_NOT_SUPPORTED,
                     "FFmpeg media requests require an HTTP proxy; the yt-dlp proxy is unsupported");
  argv[argc++] = "--print";
  argv[argc++] =
      "{\"base\":%(.{url,http_headers,cookies})j,\"formats\":%(requested_formats.:.{url,http_headers,cookies}|null)j}";
  argv[argc++] = "--";
  argv[argc++] = url;
  argv[argc] = NULL;

  // Two URLs can each occupy the full stream URL capacity, plus diagnostics.
  char output[MEDIA_YT_DLP_FULL_OUTPUT_BUFFER_SIZE + MEDIA_STREAM_URL_SIZE] = {0};
  int status = 0;
  LOG_IO("yt-dlp", { status = platform_execute_subprocess("yt-dlp", argv, output, sizeof(output), cancel, data); });
  if (cancel && cancel(data)) {
    return ERROR_YOUTUBE_EXTRACT_FAILED;
  }
  if (status != 0 || strlen(output) == sizeof(output) - 1) {
    static_mutex_lock_named(&g_yt_dlp_cache_lock, "yt_dlp_cache");
    yt_dlp_cache_set(url, yt_dlp_options, NULL, NULL, video_request, audio_request);
    static_mutex_unlock(&g_yt_dlp_cache_lock);
    log_error("yt-dlp failed to extract streams (exit %d)", status);
    return SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp failed to extract streams");
  }

  size_t count = 0;
  yyjson_doc *doc = yyjson_read(output, strlen(output), 0);
  yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
  yyjson_val *base = root ? yyjson_obj_get(root, "base") : NULL;
  yyjson_val *formats = root ? yyjson_obj_get(root, "formats") : NULL;
  if (!yyjson_is_arr(formats))
    formats = NULL;
  size_t total = yyjson_is_arr(formats) ? yyjson_arr_size(formats) : 1;
  if (!yyjson_is_obj(base) || total > 2) {
    if (doc)
      yyjson_doc_free(doc);
    return SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp returned invalid stream metadata");
  }
  for (size_t i = 0; i < total; ++i) {
    yyjson_val *format = formats ? yyjson_arr_get(formats, i) : base;
    yyjson_val *stream_value = yyjson_obj_get(format, "url");
    const char *stream = yyjson_get_str(stream_value);
    char *dest = i == 0 ? output_url : audio_url;
    size_t capacity = i == 0 ? output_size : audio_size;
    yt_dlp_request_options_t *request = i == 0 ? video_request : audio_request;
    const size_t stream_length = stream ? strlen(stream) : 0;
    if (!stream || stream_length != yyjson_get_len(stream_value) || !yt_dlp_is_stream_url(stream) ||
        stream_length >= capacity || !yt_dlp_parse_request(format, base, stream, request)) {
      yyjson_doc_free(doc);
      output_url[0] = audio_url[0] = '\0';
      return SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp returned invalid stream metadata");
    }
    memcpy(dest, stream, stream_length + sizeof(char));
    ++count;
  }
  yyjson_doc_free(doc);
  if (count == 0) {
    static_mutex_lock_named(&g_yt_dlp_cache_lock, "yt_dlp_cache");
    yt_dlp_cache_set(url, yt_dlp_options, NULL, NULL, video_request, audio_request);
    static_mutex_unlock(&g_yt_dlp_cache_lock);
    log_error("yt-dlp returned no playable stream URL");
    return SET_ERRNO(ERROR_YOUTUBE_EXTRACT_FAILED, "yt-dlp returned no playable formats");
  }
  static_mutex_lock_named(&g_yt_dlp_cache_lock, "yt_dlp_cache");
  yt_dlp_cache_set(url, yt_dlp_options, output_url, audio_url, video_request, audio_request);
  static_mutex_unlock(&g_yt_dlp_cache_lock);
  log_debug("yt-dlp extracted %zu stream URL(s)", count);
  return ASCIICHAT_OK;
}

asciichat_error_t yt_dlp_extract_stream_urls_interruptible(const char *url, const char *yt_dlp_options,
                                                           char *output_url, size_t output_size, char *audio_url,
                                                           size_t audio_size, bool (*cancel)(void *), void *data) {
  yt_dlp_request_options_t video_request, audio_request;
  return yt_dlp_extract_with_options(url, yt_dlp_options, output_url, output_size, audio_url, audio_size,
                                     &video_request, &audio_request, cancel, data);
}

asciichat_error_t yt_dlp_extract_stream_urls(const char *url, const char *yt_dlp_options, char *output_url,
                                             size_t output_size, char *audio_url, size_t audio_size) {
  return yt_dlp_extract_stream_urls_interruptible(url, yt_dlp_options, output_url, output_size, audio_url, audio_size,
                                                  NULL, NULL);
}

asciichat_error_t yt_dlp_extract_stream_url(const char *url, const char *yt_dlp_options, char *output_url,
                                            size_t output_size) {
  char audio_url[MEDIA_STREAM_URL_SIZE];
  return yt_dlp_extract_stream_urls(url, yt_dlp_options, output_url, output_size, audio_url, sizeof(audio_url));
}
