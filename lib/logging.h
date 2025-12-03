/**
 * @defgroup logging Logging System
 * @ingroup module_core
 * @brief 📝 Logging API with multiple log levels and terminal output control
 *
 * @file logging.h
 * @brief 📝 Logging API with multiple log levels and terminal output control
 * @ingroup logging
 * @addtogroup logging
 * @{
 *
 * This header provides a comprehensive logging system with:
 * - Multiple log levels (DEV, DEBUG, INFO, WARN, ERROR, FATAL)
 * - File and terminal output with automatic color coding
 * - Terminal capability detection (16-color, 256-color, truecolor)
 * - Rate-limited logging macros for high-frequency logging
 * - Thread-safe logging operations
 * - Automatic log file rotation when size limit is reached
 *
 * @note In debug builds, log macros include file/line/function information.
 *       In release builds, this information is omitted.
 */

#pragma once

#include <stdarg.h>
#include "platform/mutex.h"
#include "platform/socket.h"
#include "network/logging.h"
struct crypto_context_t;

/* PLATFORM_MAX_PATH_LENGTH is defined in common.h but we can't include it here
 * to avoid circular dependencies. Define it here to match the definition pattern.
 * This matches the definition in common.h (see common.h comments for details).
 */
#ifndef PLATFORM_MAX_PATH_LENGTH
#ifdef _WIN32
#define PLATFORM_MAX_PATH_LENGTH 32767
#elif defined(__linux__)
#define PLATFORM_MAX_PATH_LENGTH 4096
#elif defined(__APPLE__)
#define PLATFORM_MAX_PATH_LENGTH 1024
#else
#define PLATFORM_MAX_PATH_LENGTH 4096
#endif
#endif

/**
 * @brief Logging levels enumeration
 * @ingroup logging
 */
typedef enum {
  LOG_DEV = 0, /**< Development messages (most verbose) */
  LOG_DEBUG,   /**< Debug messages */
  LOG_INFO,    /**< Informational messages */
  LOG_WARN,    /**< Warning messages */
  LOG_ERROR,   /**< Error messages */
  LOG_FATAL    /**< Fatal error messages (most severe) */
} log_level_t;

#ifdef NDEBUG
/** @brief Default log level for release builds (INFO and above) */
#define DEFAULT_LOG_LEVEL LOG_INFO
#else
/** @brief Default log level for debug builds (DEBUG and above) */
#define DEFAULT_LOG_LEVEL LOG_DEBUG
#endif

/** @brief Maximum log file size in bytes (3MB) before rotation */
#define MAX_LOG_SIZE (3 * 1024 * 1024)

static struct log_context_t {
  int file;
  log_level_t level;
  mutex_t mutex;
  bool initialized;
  char filename[PLATFORM_MAX_PATH_LENGTH]; /* Store filename for rotation */
  size_t current_size;                     /* Track current file size */
  bool terminal_output_enabled;            /* Control stderr output to terminal */
  bool level_manually_set;                 /* Track if level was set manually */
} g_log = {
    .file = 2, /* STDERR_FILENO - fd 0 is STDIN (read-only!) */
    .level = DEFAULT_LOG_LEVEL,
    .initialized = false,
    .filename = {0},
    .current_size = 0,
    .terminal_output_enabled = true,
    .level_manually_set = false,
};

static const char *level_strings[] = {"DEV", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

// 16-color colors
static const char *level_colors_16[] = {
    "\x1b[34m", /* DEV: Blue */
    "\x1b[36m", /* DEBUG: Cyan */
    "\x1b[32m", /* INFO: Green */
    "\x1b[33m", /* WARN: Yellow */
    "\x1b[31m", /* ERROR: Red */
    "\x1b[35m", /* FATAL: Magenta */
    "\x1b[0m",  /* Reset */
};

// 256-color colors
static const char *level_colors_256[] = {
    "\x1b[94m", /* DEV: Bright Blue */
    "\x1b[96m", /* DEBUG: Bright Cyan */
    "\x1b[92m", /* INFO: Bright Green */
    "\x1b[33m", /* WARN: Yellow */
    "\x1b[31m", /* ERROR: Red */
    "\x1b[35m", /* FATAL: Magenta */
    "\x1b[0m",  /* Reset */
};

// Truecolor RGB colors
static const char *level_colors_truecolor[] = {
    "\x1b[38;2;150;100;205m", /* DEV: Blue */
    "\x1b[38;2;30;205;255m",  /* DEBUG: Cyan */
    "\x1b[38;2;50;205;100m",  /* INFO: Green */
    "\x1b[38;2;205;185;40m",  /* WARN: Yellow */
    "\x1b[38;2;205;30;100m",  /* ERROR: Red */
    "\x1b[38;2;205;80;205m",  /* FATAL: Magenta */
    "\x1b[0m",                /* Reset */
};

/**
 * @brief Color names enum for terminal output
 * @ingroup logging
 */
typedef enum {
  COLOR_BLUE = 0, /**< Blue color */
  COLOR_CYAN,     /**< Cyan color */
  COLOR_GREEN,    /**< Green color */
  COLOR_YELLOW,   /**< Yellow color */
  COLOR_RED,      /**< Red color */
  COLOR_MAGENTA,  /**< Magenta color */
  COLOR_RESET,    /**< Reset color to default */
} color_t;

/**
 * @brief Logging-specific color enum that maps to log levels
 * @ingroup logging
 */
typedef enum {
  LOGGING_COLOR_DEBUG = COLOR_CYAN,    /**< Color for DEBUG messages */
  LOGGING_COLOR_INFO = COLOR_GREEN,    /**< Color for INFO messages */
  LOGGING_COLOR_WARN = COLOR_YELLOW,   /**< Color for WARN messages */
  LOGGING_COLOR_ERROR = COLOR_RED,     /**< Color for ERROR messages */
  LOGGING_COLOR_FATAL = COLOR_MAGENTA, /**< Color for FATAL messages */
  LOGGING_COLOR_DEV = COLOR_BLUE,      /**< Color for DEV messages */
  LOGGING_COLOR_RESET = COLOR_RESET    /**< Reset color */
} logging_color_t;

/**
 * @brief Initialize the logging system
 * @param filename Log file path (or NULL for no file logging)
 * @param level Minimum log level to output
 * @ingroup logging
 */
void log_init(const char *filename, log_level_t level);

/**
 * @brief Destroy the logging system and close log file
 * @ingroup logging
 */
void log_destroy(void);

/**
 * @brief Set the minimum log level
 * @param level Minimum log level to output
 * @ingroup logging
 */
void log_set_level(log_level_t level);

/**
 * @brief Get the current minimum log level
 * @return Current log level
 * @ingroup logging
 */
log_level_t log_get_level(void);

/**
 * @brief Control stderr output to terminal
 * @param enabled true to enable terminal output, false to disable
 * @ingroup logging
 */
void log_set_terminal_output(bool enabled);

/**
 * @brief Get current terminal output setting
 * @return true if terminal output is enabled, false otherwise
 * @ingroup logging
 */
bool log_get_terminal_output(void);

/**
 * @brief Manually truncate large log files
 *
 * Checks if the log file exceeds MAX_LOG_SIZE and truncates it if necessary.
 * @ingroup logging
 */
void log_truncate_if_large(void);

/**
 * @brief Log a message at a specific level
 * @param level Log level (LOG_DEV, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL)
 * @param file Source file name (or NULL to omit)
 * @param line Source line number (or 0 to omit)
 * @param func Function name (or NULL to omit)
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 * @ingroup logging
 */
void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...);

/**
 * @brief Plain logging without timestamps or levels
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 *
 * Writes to both log file and stderr without timestamps or log levels.
 * @ingroup logging
 */
void log_plain_msg(const char *fmt, ...);

/**
 * @brief Log to file only, no stderr output
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 *
 * Writes to log file only, without terminal output.
 * @ingroup logging
 */
void log_file_msg(const char *fmt, ...);

/**
 * @brief Get color string for a given color enum
 * @param color Color enum value
 * @return ANSI color code string
 * @ingroup logging
 */
const char *log_level_color(logging_color_t color);

/**
 * @brief Get the appropriate color array based on terminal capabilities
 * @return Pointer to color array (16-color, 256-color, or truecolor)
 *
 * Automatically detects terminal capabilities and returns the appropriate color array.
 * @ingroup logging
 */
const char **log_get_color_array(void);

/**
 * @brief Re-detect terminal capabilities after logging is initialized
 *
 * Useful when terminal capabilities change or need to be refreshed.
 * @ingroup logging
 */
void log_redetect_terminal_capabilities(void);

/**
 * @brief Format a message using va_list
 * @param format Format string
 * @param args Variable arguments list
 * @return Formatted message string (must be freed by caller)
 * @ingroup logging
 */
char *format_message(const char *format, va_list args);

/**
 * @brief Get current time as formatted string
 * @param time_buf Output buffer for formatted time
 * @return Number of characters written (excluding null terminator)
 * @ingroup logging
 */
size_t get_current_time_formatted(char *time_buf);

/**
 * @brief Send a formatted log message over the network.
 * @param sockfd Destination socket
 * @param crypto_ctx Optional crypto context for encryption (NULL if not ready)
 * @param level Log severity used for remote and local logging
 * @param direction Remote log direction metadata
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t log_network_message(socket_t sockfd, const struct crypto_context_t *crypto_ctx, log_level_t level,
                                      remote_log_direction_t direction, const char *fmt, ...);

/**
 * @brief Log a message to all destinations (network, file, and terminal).
 * @param sockfd Destination socket
 * @param crypto_ctx Optional crypto context for encryption (NULL if not ready)
 * @param level Log severity used for remote and local logging
 * @param direction Remote log direction metadata
 * @param file Source file name (or NULL to omit)
 * @param line Source line number (or 0 to omit)
 * @param func Function name (or NULL to omit)
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t log_all_message(socket_t sockfd, const struct crypto_context_t *crypto_ctx, log_level_t level,
                                  remote_log_direction_t direction, const char *file, int line, const char *func,
                                  const char *fmt, ...);

/**
 * @name Logging Macros
 * @{
 */

/**
 * @brief Log a DEBUG message
 * @param ... Format string and arguments (printf-style)
 *
 * @note In debug builds, includes file/line/function. In release builds, this information is omitted.
 * @ingroup logging
 */
#ifdef NDEBUG
#define log_debug(...) log_msg(LOG_DEBUG, NULL, 0, NULL, __VA_ARGS__)
#else
#define log_debug(...) log_msg(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif

/**
 * @brief Log an INFO message
 * @param ... Format string and arguments (printf-style)
 * @ingroup logging
 */
#ifdef NDEBUG
#define log_info(...) log_msg(LOG_INFO, NULL, 0, NULL, __VA_ARGS__)
#else
#define log_info(...) log_msg(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif

/**
 * @brief Log a WARN message
 * @param ... Format string and arguments (printf-style)
 * @ingroup logging
 */
#ifdef NDEBUG
#define log_warn(...) log_msg(LOG_WARN, NULL, 0, NULL, __VA_ARGS__)
#else
#define log_warn(...) log_msg(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif

/**
 * @brief Log an ERROR message
 * @param ... Format string and arguments (printf-style)
 * @ingroup logging
 */
#ifdef NDEBUG
#define log_error(...) log_msg(LOG_ERROR, NULL, 0, NULL, __VA_ARGS__)
#else
#define log_error(...) log_msg(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif

/**
 * @brief Log a FATAL message
 * @param ... Format string and arguments (printf-style)
 * @ingroup logging
 */
#ifdef NDEBUG
#define log_fatal(...) log_msg(LOG_FATAL, NULL, 0, NULL, __VA_ARGS__)
#else
#define log_fatal(...) log_msg(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif

/**
 * @brief Plain logging - writes to both log file and stderr without timestamps or log levels
 * @param ... Format string and arguments (printf-style)
 * @ingroup logging
 */
#define log_plain(...) log_plain_msg(__VA_ARGS__)

/**
 * @brief File-only logging - writes to log file only, no stderr output
 * @param ... Format string and arguments (printf-style)
 * @ingroup logging
 */
#define log_file(...) log_file_msg(__VA_ARGS__)

/** @} */

/**
 * @brief Rate-limited logging macro
 *
 * Logs at most once per specified time interval. Useful for threads that have
 * an FPS and functions they call to prevent spammy logs.
 *
 * @param log_level Log level (DEV, DEBUG, INFO, WARN, ERROR, FATAL)
 * @param interval_us Minimum microseconds between log messages
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 *
 * @note Each call site maintains its own static timer, so different call sites
 *       can log independently.
 *
 * @ingroup logging
 */
#ifdef NDEBUG
#define log_every(log_level, interval_us, fmt, ...)                                                                    \
  do {                                                                                                                 \
    static uint64_t last_log_time = 0;                                                                                 \
    uint64_t now_us = 0;                                                                                               \
    struct timespec ts;                                                                                                \
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {                                                                    \
      now_us = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;                                      \
    }                                                                                                                  \
    if (now_us - last_log_time >= (uint64_t)(interval_us)) {                                                           \
      last_log_time = now_us;                                                                                          \
      log_msg(LOG_##log_level, NULL, 0, NULL, fmt, ##__VA_ARGS__);                                                     \
    }                                                                                                                  \
  } while (0)
#else
#define log_every(log_level, interval_us, fmt, ...)                                                                    \
  do {                                                                                                                 \
    static uint64_t last_log_time = 0;                                                                                 \
    uint64_t now_us = 0;                                                                                               \
    struct timespec ts;                                                                                                \
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {                                                                    \
      now_us = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;                                      \
    }                                                                                                                  \
    if (now_us - last_log_time >= (uint64_t)(interval_us)) {                                                           \
      last_log_time = now_us;                                                                                          \
      log_msg(LOG_##log_level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);                                      \
    }                                                                                                                  \
  } while (0)
#endif

/**
 * @name Rate-Limited Logging Macros
 * @{
 */

/** @brief Rate-limited DEV logging */
#define log_dev_every(interval_us, fmt, ...) log_every(DEV, interval_us, fmt, ##__VA_ARGS__)

/** @brief Rate-limited DEBUG logging */
#define log_debug_every(interval_us, fmt, ...) log_every(DEBUG, interval_us, fmt, ##__VA_ARGS__)

/** @brief Rate-limited INFO logging */
#define log_info_every(interval_us, fmt, ...) log_every(INFO, interval_us, fmt, ##__VA_ARGS__)

/** @brief Rate-limited WARN logging */
#define log_warn_every(interval_us, fmt, ...) log_every(WARN, interval_us, fmt, ##__VA_ARGS__)

/** @brief Rate-limited ERROR logging */
#define log_error_every(interval_us, fmt, ...) log_every(ERROR, interval_us, fmt, ##__VA_ARGS__)

/** @brief Rate-limited FATAL logging */
#define log_fatal_every(interval_us, fmt, ...) log_every(FATAL, interval_us, fmt, ##__VA_ARGS__)

/** @} */

/**
 * @brief Log a DEBUG message to all destinations (network, file, and terminal)
 */
#ifdef NDEBUG
#define log_debug_all(sockfd, crypto_ctx, direction, fmt, ...)                                                         \
  log_all_message(sockfd, crypto_ctx, LOG_DEBUG, direction, NULL, 0, NULL, fmt, ##__VA_ARGS__)
#else
#define log_debug_all(sockfd, crypto_ctx, direction, fmt, ...)                                                         \
  log_all_message(sockfd, crypto_ctx, LOG_DEBUG, direction, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif

/**
 * @brief Log an INFO message to all destinations (network, file, and terminal)
 */
#ifdef NDEBUG
#define log_info_all(sockfd, crypto_ctx, direction, fmt, ...)                                                          \
  log_all_message(sockfd, crypto_ctx, LOG_INFO, direction, NULL, 0, NULL, fmt, ##__VA_ARGS__)
#else
#define log_info_all(sockfd, crypto_ctx, direction, fmt, ...)                                                          \
  log_all_message(sockfd, crypto_ctx, LOG_INFO, direction, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif

/**
 * @brief Log a WARN message to all destinations (network, file, and terminal)
 */
#ifdef NDEBUG
#define log_warn_all(sockfd, crypto_ctx, direction, fmt, ...)                                                          \
  log_all_message(sockfd, crypto_ctx, LOG_WARN, direction, NULL, 0, NULL, fmt, ##__VA_ARGS__)
#else
#define log_warn_all(sockfd, crypto_ctx, direction, fmt, ...)                                                          \
  log_all_message(sockfd, crypto_ctx, LOG_WARN, direction, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif

/**
 * @brief Log an ERROR message to all destinations (network, file, and terminal)
 */
#ifdef NDEBUG
#define log_error_all(sockfd, crypto_ctx, direction, fmt, ...)                                                         \
  log_all_message(sockfd, crypto_ctx, LOG_ERROR, direction, NULL, 0, NULL, fmt, ##__VA_ARGS__)
#else
#define log_error_all(sockfd, crypto_ctx, direction, fmt, ...)                                                         \
  log_all_message(sockfd, crypto_ctx, LOG_ERROR, direction, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif

/**
 * @brief Log a FATAL message to all destinations (network, file, and terminal)
 */
#ifdef NDEBUG
#define log_fatal_all(sockfd, crypto_ctx, direction, fmt, ...)                                                         \
  log_all_message(sockfd, crypto_ctx, LOG_FATAL, direction, NULL, 0, NULL, fmt, ##__VA_ARGS__)
#else
#define log_fatal_all(sockfd, crypto_ctx, direction, fmt, ...)                                                         \
  log_all_message(sockfd, crypto_ctx, LOG_FATAL, direction, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif

// Don't use the logging functions to log errors about the logging system itself to avoid recursion.
#ifdef NDEBUG
#define LOGGING_INTERNAL_ERROR(error, message, ...)                                                                    \
  do {                                                                                                                 \
    asciichat_set_errno_with_message(error, NULL, 0, NULL, message, ##__VA_ARGS__);                                    \
    static const char *msg_header = "CRITICAL LOGGING SYSTEM ERROR: ";                                                 \
    safe_fprintf(stderr, "%s%s%s: %s", log_level_color(LOGGING_COLOR_ERROR), msg_header,                               \
                 log_level_color(LOGGING_COLOR_RESET), message);                                                       \
    platform_write(g_log.file, msg_header, strlen(msg_header));                                                        \
    platform_write(g_log.file, message, strlen(message));                                                              \
    platform_write(g_log.file, "\n", 1);                                                                               \
    platform_print_backtrace(0);                                                                                       \
  } while (0)
#else
#define LOGGING_INTERNAL_ERROR(error, message, ...)                                                                    \
  do {                                                                                                                 \
    asciichat_set_errno_with_message(error, __FILE__, __LINE__, __func__, message, ##__VA_ARGS__);                     \
    static const char *msg_header = "CRITICAL LOGGING SYSTEM ERROR: ";                                                 \
    safe_fprintf(stderr, "%s%s%s: %s", log_level_color(LOGGING_COLOR_ERROR), msg_header,                               \
                 log_level_color(LOGGING_COLOR_RESET), message);                                                       \
    platform_write(g_log.file, msg_header, strlen(msg_header));                                                        \
    platform_write(g_log.file, message, strlen(message));                                                              \
    platform_write(g_log.file, "\n", 1);                                                                               \
    platform_print_backtrace(0);                                                                                       \
  } while (0)
#endif

/** @} */ /* logging */
