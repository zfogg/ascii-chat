#ifndef ASCII_CHAT_COMMON_H
#define ASCII_CHAT_COMMON_H

/* Feature test macros for POSIX functions */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "options.h"

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
} asciichat_error_t;

/* Error handling */
const char *asciichat_error_string(asciichat_error_t error);

const char *asciichat_error_string(asciichat_error_t error) {
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
  default:
    return "Unknown error";
  }
}


#define ASCIICHAT_WEBCAM_ERROR_STRING "Webcam capture failed"

/* Frame protocol header */
// typedef struct {
//   uint32_t magic;     /* Magic number: 0x41534349 ('ASCI') */
//   uint32_t version;   /* Protocol version */
//   uint32_t width;     /* Frame width */
//   uint32_t height;    /* Frame height */
//   uint32_t size;      /* Payload size in bytes */
//   uint32_t flags;     /* Frame flags (future use) */
//   uint32_t sequence;  /* Frame sequence number */
//   uint32_t timestamp; /* Unix timestamp */
// } frame_header_t;

// #define FRAME_MAGIC 0x41534349 /* 'ASCI' */
// #define FRAME_VERSION 1

/* Performance tuning */
#define MAX_FPS 120

/* Frame interval calculation */
#define FRAME_INTERVAL_MS (1000 / MAX_FPS)
static inline int get_frame_interval_ms(void) {
  return FRAME_INTERVAL_MS;
}

/* Buffer sizes */
// #define FRAME_BUFFER_SIZE 65536        /* 64KB frame buffer (monochrome) */
// #define FRAME_BUFFER_SIZE 4194304      /* 4MB frame buffer */
//  Calculate buffer size more accurately based on actual usage:
//  - Foreground ANSI: avg 16 chars (range 12-19)
//  - Background ANSI: avg 16 chars (range 12-19)
//  - ASCII char: 1 char
//  - Per row: color reset (4) + newline (1) = 5 chars
//  - At end: delimiter (1) + null terminator (1) = 2 chars
//  Dynamic calculation based on color options:
#define FRAME_BUFFER_SIZE_BASE(w, h) ((h) * (w) * (opt_background_color ? 33 : 17) + (h) * 5 + 2)
// Add 50% safety margin for ANSI sequence length variations and terminal resizing
#define FRAME_BUFFER_SIZE (FRAME_BUFFER_SIZE_BASE(opt_width, opt_height) * 3 / 2)
// Ensure minimum size for very small terminals
#define FRAME_BUFFER_SIZE_MIN (1024 * 1024) /* 1MB minimum */
// Ensure reasonable maximum to prevent excessive memory usage
#define FRAME_BUFFER_SIZE_MAX (16 * 1024 * 1024) /* 16MB maximum */
// Final calculation with bounds checking
#define FRAME_BUFFER_SIZE_FINAL                                                                                        \
  (FRAME_BUFFER_SIZE < FRAME_BUFFER_SIZE_MIN                                                                           \
       ? FRAME_BUFFER_SIZE_MIN                                                                                         \
       : (FRAME_BUFFER_SIZE > FRAME_BUFFER_SIZE_MAX ? FRAME_BUFFER_SIZE_MAX : FRAME_BUFFER_SIZE))

#define FRAME_BUFFER_CAPACITY (MAX_FPS / 4)

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

/* Safe memory reallocation */
#define SAFE_REALLOC(ptr, new_ptr, size)                                                                               \
  do {                                                                                                                 \
    void *tmp_ptr = realloc((ptr), (size));                                                                            \
    if (!(tmp_ptr)) {                                                                                                  \
      log_error("Memory reallocation failed: %zu bytes", (size_t)(size));                                              \
      /*return ASCIICHAT_ERR_MALLOC;*/                                                                                 \
      exit(ASCIICHAT_ERR_MALLOC);                                                                                      \
    }                                                                                                                  \
    (new_ptr) = tmp_ptr;                                                                                               \
    (ptr) = (new_ptr);                                                                                                 \
  } while (0)

/* Safe string copy */
#define SAFE_STRNCPY(dst, src, size)                                                                                   \
  do {                                                                                                                 \
    strncpy((dst), (src), (size) - 1);                                                                                 \
    (dst)[(size) - 1] = '\0';                                                                                          \
  } while (0)

/* Min/Max macros */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Array size */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Logging functions */
void log_init(const char *filename, log_level_t level);
void log_destroy(void);
void log_set_level(log_level_t level);
void log_truncate_if_large(void); /* Manually truncate large log files */
void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...);

/* Logging macros */
#define log_debug(...) log_msg(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_info(...) log_msg(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warn(...) log_msg(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_error(...) log_msg(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_fatal(...) log_msg(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

/* Memory debugging (only in debug builds) */
#ifdef DEBUG_MEMORY
void *debug_malloc(size_t size, const char *file, int line);
void debug_free(void *ptr, const char *file, int line);
void debug_memory_report(void);

#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#endif

#endif /* ASCII_CHAT_COMMON_H */