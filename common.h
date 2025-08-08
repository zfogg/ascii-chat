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
  ASCIICHAT_ERR_BUFFER_ACCESS = -12,
  ASCIICHAT_ERR_BUFFER_OVERFLOW = -13,
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

#define FRAME_BUFFER_CAPACITY (MAX_FPS / 4)
#define FRAME_BUFFER_SIZE_FINAL (320 * 240 * 64 + 1024) // ~4.9MB for terminal frames with ANSI colors

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

/* Min/Max macros (with guards for macOS Foundation.h) */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

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
