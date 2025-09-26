#pragma once

/* Feature test macros for POSIX functions */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// This fixes clangd errors about missing types. I DID include stdint.h, but
// it's not enough.
#ifndef UINT8_MAX
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
#endif

#include <stdlib.h>

/* ============================================================================
 * Common Definitions
 * ============================================================================
 */

/* Error codes for better error handling */
typedef enum {
  ASCIICHAT_OK = 0,
  ASCIICHAT_ERR_MALLOC = -1,
  ASCIICHAT_ERR_NETWORK = -2,
  ASCIICHAT_ERR_NETWORK_SIZE = -3,
  ASCIICHAT_ERR_WEBCAM = -4,
  ASCIICHAT_ERR_INVALID_PARAM = -5,
  ASCIICHAT_ERR_TIMEOUT = -6,
  ASCIICHAT_ERR_BUFFER_FULL = -7,
  ASCIICHAT_ERR_JPEG = -8,
  ASCIICHAT_ERR_TERMINAL = -9,
  ASCIICHAT_ERR_THREAD = -10,
  ASCIICHAT_ERR_AUDIO = -11,
  ASCIICHAT_ERR_DISPLAY = -12,
  ASCIICHAT_ERR_BUFFER_ACCESS = -13,
  ASCIICHAT_ERR_BUFFER_OVERFLOW = -14,
  ASCIICHAT_ERR_INVALID_FRAME = -15,
  ASCIICHAT_ERR_WEBCAM_IN_USE = -16,
} asciichat_error_t;

/* Error handling */
static inline const char *asciichat_error_string(asciichat_error_t error) {
  switch (error) {
  case ASCIICHAT_OK:
    return "Success";
  case ASCIICHAT_ERR_MALLOC:
    return "Memory allocation failed";
  case ASCIICHAT_ERR_NETWORK:
    return "Network error";
  case ASCIICHAT_ERR_WEBCAM:
    return "Webcam error";
  case ASCIICHAT_ERR_INVALID_PARAM:
    return "Invalid parameter";
  case ASCIICHAT_ERR_TIMEOUT:
    return "Operation timed out";
  case ASCIICHAT_ERR_BUFFER_FULL:
    return "Buffer full";
  case ASCIICHAT_ERR_JPEG:
    return "JPEG processing error";
  case ASCIICHAT_ERR_TERMINAL:
    return "Terminal error";
  case ASCIICHAT_ERR_THREAD:
    return "Thread error";
  case ASCIICHAT_ERR_AUDIO:
    return "Audio error";
  case ASCIICHAT_ERR_DISPLAY:
    return "Display error";
  case ASCIICHAT_ERR_INVALID_FRAME:
    return "Frame data error";
  case ASCIICHAT_ERR_WEBCAM_IN_USE:
    return "Webcam already in use by another application";
  default:
    return "Unknown error";
  }
}

// RGB value clamping utility function
static inline uint8_t clamp_rgb(int value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return (uint8_t)value;
}

#define ASCIICHAT_WEBCAM_ERROR_STRING "Webcam capture failed"

// Frame rate configuration - Windows terminals struggle with high FPS
#ifdef _WIN32
#define DEFAULT_MAX_FPS 30 // Windows terminals can't handle more than this
#else
#define DEFAULT_MAX_FPS 60 // macOS/Linux terminals can handle higher rates
#endif

// Allow runtime override via environment variable or command line
extern int g_max_fps;
#define MAX_FPS (g_max_fps > 0 ? g_max_fps : DEFAULT_MAX_FPS)

#define FRAME_INTERVAL_MS (1000 / MAX_FPS)

#define FRAME_BUFFER_CAPACITY (MAX_FPS / 4)

// Global variables to store last known image dimensions for aspect ratio
// recalculation
extern unsigned short int last_image_width, last_image_height;

/* Logging levels */
typedef enum { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL } log_level_t;

/* ============================================================================
 * Utility Macros
 * ============================================================================
 */

/* Safe memory allocation with error checking */
#define SAFE_MALLOC(ptr, size, cast)                                                                                   \
  do {                                                                                                                 \
    (ptr) = (cast)malloc(size);                                                                                        \
    if (!(ptr)) {                                                                                                      \
      log_error("Memory allocation failed: %zu bytes", (size_t)(size));                                                \
      /*return ASCIICHAT_ERR_MALLOC;*/                                                                                 \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
  } while (0)

/* Safe zero-initialized memory allocation */
#define SAFE_CALLOC(ptr, count, size, cast)                                                                            \
  do {                                                                                                                 \
    (ptr) = (cast)calloc((count), (size));                                                                             \
    if (!(ptr)) {                                                                                                      \
      log_error("Memory allocation failed: %zu elements x %zu bytes", (size_t)(count), (size_t)(size));                \
      /*return ASCIICHAT_ERR_MALLOC;*/                                                                                 \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
  } while (0)

/* Common utility macros */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/* Safe memory reallocation */
#define SAFE_REALLOC(ptr, size, cast)                                                                                  \
  do {                                                                                                                 \
    void *tmp_ptr = realloc((ptr), (size));                                                                            \
    if (!(tmp_ptr)) {                                                                                                  \
      log_error("Memory reallocation failed: %zu bytes", (size_t)(size));                                              \
      /*return ASCIICHAT_ERR_MALLOC;*/                                                                                 \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
    (ptr) = (cast)(tmp_ptr);                                                                                           \
  } while (0)

/* SIMD-aligned memory allocation macros for optimal NEON/AVX performance */
#ifdef __APPLE__
/* macOS uses posix_memalign() for aligned allocation */
#define SAFE_MALLOC_ALIGNED(ptr, size, alignment, cast)                                                                \
  do {                                                                                                                 \
    int result = posix_memalign((void **)&(ptr), (alignment), (size));                                                 \
    if (result != 0 || !(ptr)) {                                                                                       \
      log_error("Aligned memory allocation failed: %zu bytes, %zu alignment", (size_t)(size), (size_t)(alignment));    \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
    (ptr) = (cast)(ptr);                                                                                               \
  } while (0)
#else
/* Linux/other platforms use aligned_alloc() (C11) */
#define SAFE_MALLOC_ALIGNED(ptr, size, alignment, cast)                                                                \
  do {                                                                                                                 \
    size_t aligned_size = (((size) + (alignment) - 1) / (alignment)) * (alignment);                                    \
    (ptr) = (cast)aligned_alloc((alignment), aligned_size);                                                            \
    if (!(ptr)) {                                                                                                      \
      log_error("Aligned memory allocation failed: %zu bytes, %zu alignment", aligned_size, (size_t)(alignment));      \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
  } while (0)
#endif

/* 16-byte aligned allocation for SIMD operations */
#define SAFE_MALLOC_SIMD(ptr, size, cast) SAFE_MALLOC_ALIGNED(ptr, size, 16, cast)

/* 16-byte aligned zero-initialized allocation */
#define SAFE_CALLOC_SIMD(ptr, count, size, cast)                                                                       \
  do {                                                                                                                 \
    size_t total_size = (count) * (size);                                                                              \
    SAFE_MALLOC_SIMD(ptr, total_size, cast);                                                                           \
    memset((ptr), 0, total_size);                                                                                      \
  } while (0)

/* Safe free that nulls the pointer - available in all builds */
#define SAFE_FREE(ptr)                                                                                                 \
  do {                                                                                                                 \
    if ((ptr) != NULL) {                                                                                               \
      free((ptr));                                                                                                     \
      (ptr) = NULL;                                                                                                    \
    }                                                                                                                  \
  } while (0)

/* Safe string copy */
#include "platform/system.h"
#define SAFE_STRNCPY(dst, src, size) platform_strlcpy((dst), (src), (size))

/* Safe string duplication with platform compatibility */
#ifdef _WIN32
#define SAFE_STRDUP(dst, src)                                                                                          \
  do {                                                                                                                 \
    (dst) = _strdup(src);                                                                                              \
    if (!(dst)) {                                                                                                      \
      log_error("String duplication failed for: %s", (src) ? (src) : "(null)");                                        \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
  } while (0)
#else
#define SAFE_STRDUP(dst, src)                                                                                          \
  do {                                                                                                                 \
    (dst) = strdup(src);                                                                                               \
    if (!(dst)) {                                                                                                      \
      log_error("String duplication failed for: %s", (src) ? (src) : "(null)");                                        \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
  } while (0)
#endif

/* Rate-limited debug logging - only logs every N calls */
#define LOG_DEBUG_EVERY(name, count, fmt, ...)                                                                         \
  do {                                                                                                                 \
    static int name##_counter = 0;                                                                                     \
    name##_counter++;                                                                                                  \
    if (name##_counter % (count) == 0) {                                                                               \
      log_debug(fmt, ##__VA_ARGS__);                                                                                   \
    }                                                                                                                  \
  } while (0)

/* Platform-safe environment variable access */
#include "platform/system.h"
#define SAFE_GETENV(name) ((char *)platform_getenv(name))

/* Platform-safe sscanf */
#define SAFE_SSCANF(str, format, ...) sscanf(str, format, __VA_ARGS__)

/* Platform-safe strerror */
#include "platform/internal.h"
#define SAFE_STRERROR(errnum) platform_strerror(errnum)

/* Platform-safe file open */
#include "platform/file.h"
#define SAFE_OPEN(path, flags, mode) platform_open(path, flags, mode)

/* Safe memory functions */
#define SAFE_MEMCPY(dest, dest_size, src, count) platform_memcpy((dest), (dest_size), (src), (count))
#define SAFE_MEMSET(dest, dest_size, ch, count) platform_memset((dest), (dest_size), (ch), (count))
#define SAFE_MEMMOVE(dest, dest_size, src, count) platform_memmove((dest), (dest_size), (src), (count))
#define SAFE_STRCPY(dest, dest_size, src) platform_strcpy((dest), (dest_size), (src))

/* Safe string formatting */
#define SAFE_SNPRINTF(buffer, buffer_size, ...) safe_snprintf((buffer), (buffer_size), __VA_ARGS__)

/* Logging functions */
void log_init(const char *filename, log_level_t level);
void log_destroy(void);
void log_set_level(log_level_t level);
log_level_t log_get_level(void);            /* Get current log level */
void log_set_terminal_output(bool enabled); /* Control stderr output to terminal */
bool log_get_terminal_output(void);         /* Get current terminal output setting */
void log_truncate_if_large(void);           /* Manually truncate large log files */
void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...);

/* Logging macros */
#define log_debug(...) log_msg(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_info(...) log_msg(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warn(...) log_msg(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_error(...) log_msg(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_fatal(...) log_msg(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

void format_bytes_pretty(size_t bytes, char *out, size_t out_capacity);

/* Safe parsing functions using strtoul instead of sscanf */
int safe_parse_size_message(const char *message, unsigned int *width, unsigned int *height);
int safe_parse_audio_message(const char *message, unsigned int *num_samples);

/* New functions for coverage testing */

/* Memory debugging (only in debug builds) */
#ifdef DEBUG_MEMORY
void *debug_malloc(size_t size, const char *file, int line);
void debug_free(void *ptr, const char *file, int line);
void debug_memory_report(void);
void debug_memory_set_quiet_mode(bool quiet); /* Control stderr output for memory report */
void *debug_calloc(size_t count, size_t size, const char *file, int line);
void *debug_realloc(void *ptr, size_t size, const char *file, int line);

#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#define calloc(count, size) debug_calloc((count), (size), __FILE__, __LINE__)
#define realloc(ptr, size) debug_realloc((ptr), (size), __FILE__, __LINE__)
#endif /* DEBUG_MEMORY */
