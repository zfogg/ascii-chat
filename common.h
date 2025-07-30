#ifndef ASCII_CHAT_COMMON_H
#define ASCII_CHAT_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Common Definitions
 * ============================================================================
 */

/* Error codes for better error handling */
typedef enum {
  ASCIICHAT_OK = 0,
  ASCIICHAT_ERR_MALLOC = -1,
  ASCIICHAT_ERR_NETWORK = -2,
  ASCIICHAT_ERR_WEBCAM = -3,
  ASCIICHAT_ERR_INVALID_PARAM = -4,
  ASCIICHAT_ERR_TIMEOUT = -5,
  ASCIICHAT_ERR_BUFFER_FULL = -6,
  ASCIICHAT_ERR_JPEG = -7,
  ASCIICHAT_ERR_TERMINAL = -8
} asciichat_error_t;

/* Frame protocol header */
typedef struct {
  uint32_t magic;     /* Magic number: 0x41534349 ('ASCI') */
  uint32_t version;   /* Protocol version */
  uint32_t width;     /* Frame width */
  uint32_t height;    /* Frame height */
  uint32_t size;      /* Payload size in bytes */
  uint32_t flags;     /* Frame flags (future use) */
  uint32_t sequence;  /* Frame sequence number */
  uint32_t timestamp; /* Unix timestamp */
} frame_header_t;

#define FRAME_MAGIC 0x41534349 /* 'ASCI' */
#define FRAME_VERSION 1

/* Buffer sizes */
#define FRAME_BUFFER_SIZE 65536         /* 64KB frame buffer (monochrome) */
#define FRAME_BUFFER_SIZE_COLOR 8388608 /* 8MB frame buffer (colored) */
#define RECV_BUFFER_SIZE 8388608 /* 8MB receive buffer (for colored frames) */
#define SEND_BUFFER_SIZE 8388608 /* 8MB send buffer (for colored frames) */

/* Frame buffer size calculation */
size_t get_frame_buffer_size(void);

/* Performance tuning */
#define MAX_FPS 30
#define MAX_FPS_COLOR 15 /* Reduced FPS for colored mode */
#define FRAME_INTERVAL_MS (1000 / MAX_FPS)
#define FRAME_INTERVAL_MS_COLOR (1000 / MAX_FPS_COLOR)

/* Frame interval calculation */
static inline int get_frame_interval_ms(void) {
  extern unsigned short int opt_color_output;
  return opt_color_output ? FRAME_INTERVAL_MS_COLOR : FRAME_INTERVAL_MS;
}

/* Logging levels */
typedef enum {
  LOG_DEBUG = 0,
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR,
  LOG_FATAL
} log_level_t;

/* ============================================================================
 * Utility Macros
 * ============================================================================
 */

/* Safe memory allocation with error checking */
#define SAFE_MALLOC(ptr, size)                                                 \
  do {                                                                         \
    (ptr) = malloc(size);                                                      \
    if (!(ptr)) {                                                              \
      log_error("Memory allocation failed: %zu bytes", (size_t)(size));        \
      return ASCIICHAT_ERR_MALLOC;                                             \
    }                                                                          \
  } while (0)

/* Safe memory reallocation */
#define SAFE_REALLOC(ptr, new_ptr, size)                                       \
  do {                                                                         \
    (new_ptr) = realloc((ptr), (size));                                        \
    if (!(new_ptr)) {                                                          \
      log_error("Memory reallocation failed: %zu bytes", (size_t)(size));      \
      return ASCIICHAT_ERR_MALLOC;                                             \
    }                                                                          \
    (ptr) = (new_ptr);                                                         \
  } while (0)

/* Safe string copy */
#define SAFE_STRNCPY(dst, src, size)                                           \
  do {                                                                         \
    strncpy((dst), (src), (size) - 1);                                         \
    (dst)[(size) - 1] = '\0';                                                  \
  } while (0)

/* Min/Max macros */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Array size */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* ============================================================================
 * Function Declarations
 * ============================================================================
 */

/* Error handling */
const char *asciichat_error_string(asciichat_error_t error);

/* Logging functions */
void log_init(const char *filename, log_level_t level);
void log_destroy(void);
void log_set_level(log_level_t level);
void log_msg(log_level_t level, const char *file, int line, const char *func,
             const char *fmt, ...);

/* Logging macros */
#define log_debug(...)                                                         \
  log_msg(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_info(...)                                                          \
  log_msg(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warn(...)                                                          \
  log_msg(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_error(...)                                                         \
  log_msg(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_fatal(...)                                                         \
  log_msg(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

/* Memory debugging (only in debug builds) */
#ifdef DEBUG_MEMORY
void *debug_malloc(size_t size, const char *file, int line);
void debug_free(void *ptr, const char *file, int line);
void debug_memory_report(void);

#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#endif

#endif /* ASCII_CHAT_COMMON_H */