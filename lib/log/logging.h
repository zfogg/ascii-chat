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
// C11 stdatomic.h conflicts with MSVC's C++ <atomic> header on Windows.
// When compiling C++ with MSVC runtime headers, the atomic types are in std:: namespace.
// We bring them into global namespace for compatibility with C-style struct definitions.
#if defined(__cplusplus) && defined(_WIN32)
#include <atomic>
using std::atomic_compare_exchange_weak_explicit;
using std::atomic_load_explicit;
using std::memory_order_relaxed;
// C11 _Atomic(T) syntax doesn't work in C++ - use std::atomic<T> instead
#define LOG_ATOMIC_UINT64 std::atomic<uint64_t>
// C++ uses brace initialization for std::atomic
#define LOG_ATOMIC_UINT64_INIT(val) {val}
#else
#include <stdatomic.h>
// C11 _Atomic type qualifier syntax
#define LOG_ATOMIC_UINT64 _Atomic uint64_t
// C11 uses direct initialization for atomic types
#define LOG_ATOMIC_UINT64_INIT(val) val
#endif
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "platform/socket.h"
#include "platform/system.h"

/* Forward declarations */
struct crypto_context_t;

/**
 * @brief Logging levels enumeration
 * @ingroup logging
 *
 * @note This typedef MUST be defined before #include "network/logging.h"
 *       to avoid circular dependency issues with packet.h
 */
typedef enum {
  LOG_DEV = 0, /**< Development messages (most verbose) */
  LOG_DEBUG,   /**< Debug messages */
  LOG_WARN,    /**< Warning messages */
  LOG_INFO,    /**< Informational messages */
  LOG_ERROR,   /**< Error messages */
  LOG_FATAL    /**< Fatal error messages (most severe) */
} log_level_t;

#include "network/logging.h"

#ifdef NDEBUG
/** @brief Default log level for release builds (INFO and above) */
#define DEFAULT_LOG_LEVEL LOG_INFO
#else
/** @brief Default log level for debug builds (DEBUG and above) */
#define DEFAULT_LOG_LEVEL LOG_DEBUG
#endif

/** @brief Maximum log file size in bytes (3MB) before rotation */
#define MAX_LOG_SIZE (3 * 1024 * 1024)

/** @brief Maximum size of terminal output buffer (64KB) */
#define MAX_TERMINAL_BUFFER_SIZE (64 * 1024)

/** @brief Maximum number of buffered log entries */
#define MAX_TERMINAL_BUFFER_ENTRIES 256

/* ============================================================================
 * Logging Buffer Size Constants
 * ============================================================================
 * Named constants for internal buffer sizes. Messages exceeding these sizes
 * will be truncated with no error indication.
 */

/** @brief Maximum size of a single log message (including formatting) */
#define LOG_MSG_BUFFER_SIZE 4096

/** @brief Maximum size of a log message in mmap mode */
#define LOG_MMAP_MSG_BUFFER_SIZE 1024

/** @brief Maximum size of a log header (timestamp, level, file:line:func) */
#define LOG_HEADER_BUFFER_SIZE 512

/** @brief Maximum size of a timestamp string */
#define LOG_TIMESTAMP_BUFFER_SIZE 32

/* ============================================================================
 * Compile-Time Log Level Stripping
 * ============================================================================
 * By default, all log levels are compiled in (LOG_DEV) so that runtime
 * verbosity flags like -vvv work in both debug and release builds.
 *
 * Define LOG_COMPILE_LEVEL to strip logs at compile-time for smaller binaries:
 *   -DLOG_COMPILE_LEVEL=LOG_INFO  -> Strip DEV and DEBUG
 *   -DLOG_COMPILE_LEVEL=LOG_WARN  -> Strip DEV, DEBUG, and INFO
 *   -DLOG_COMPILE_LEVEL=LOG_DEV   -> Keep all log levels (default)
 */
#ifndef LOG_COMPILE_LEVEL
/** @brief Compile-time minimum log level (DEV keeps all, allowing runtime -vvv) */
#define LOG_COMPILE_LEVEL LOG_DEV
#endif

/** @brief A single buffered log entry */
typedef struct {
  bool use_stderr; /* true for stderr, false for stdout */
  char *message;   /* Formatted message (heap allocated) */
} log_buffer_entry_t;

/* Implementation details (g_log struct, color arrays) are in logging.c */

/**
 * @brief Color enum for logging - indexes into color arrays
 * @ingroup logging
 *
 * These values directly index into level_colors arrays.
 * Order matches DEV, DEBUG, WARN, INFO, ERROR, FATAL, GREY, RESET.
 */
typedef enum {
  LOG_COLOR_DEV = 0,   /**< Blue - DEV messages */
  LOG_COLOR_DEBUG = 1, /**< Cyan - DEBUG messages */
  LOG_COLOR_WARN = 2,  /**< Yellow - WARN messages */
  LOG_COLOR_INFO = 3,  /**< Green - INFO messages */
  LOG_COLOR_ERROR = 4, /**< Red - ERROR messages */
  LOG_COLOR_FATAL = 5, /**< Magenta - FATAL messages */
  LOG_COLOR_GREY = 6,  /**< Grey - for neutral messages or labels */
  LOG_COLOR_RESET = 7  /**< Reset to default */
} log_color_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the logging system
 * @param filename Log file path (or NULL for no file logging)
 * @param level Minimum log level to output
 * @param force_stderr If true, route ALL logs to stderr (for client mode to keep stdout clean)
 * @param use_mmap If true, use fully lock-free mmap logging (recommended). If mmap fails, uses stderr only (no mutex
 * fallback).
 * @ingroup logging
 *
 * @note When use_mmap=true, the entire logging path is lock-free:
 *       - File output uses atomic operations on mmap'd memory
 *       - Terminal output uses atomic fprintf/fwrite to fd
 *       - No mutex is ever acquired in the hot path
 */
void log_init(const char *filename, log_level_t level, bool force_stderr, bool use_mmap);

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
 * @brief Force all terminal log output to stderr
 * @param enabled true to force all logs to stderr, false for normal routing
 *
 * When enabled, all log messages (including INFO, DEBUG, DEV) go to stderr
 * instead of the default behavior where INFO/DEBUG/DEV go to stdout and
 * WARN/ERROR/FATAL go to stderr. This is used by the client to keep stdout
 * clean for ASCII art output.
 *
 * @ingroup logging
 */
void log_set_force_stderr(bool enabled);

/**
 * @brief Get current force_stderr setting
 * @return true if all logs are forced to stderr, false otherwise
 * @ingroup logging
 */
bool log_get_force_stderr(void);

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
 * @brief Plain logging to stderr with newline
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 *
 * Writes to both log file and stderr without timestamps or log levels, with trailing newline.
 * @ingroup logging
 */
void log_plain_stderr_msg(const char *fmt, ...);

/**
 * @brief Plain logging to stderr without trailing newline
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 *
 * Writes to both log file and stderr without timestamps, log levels, or trailing newline.
 * Useful for interactive prompts where the user's response should be on the same line.
 * @ingroup logging
 */
void log_plain_stderr_nonewline_msg(const char *fmt, ...);

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
 * @brief Print a labeled message with color
 * @param label The label text to print (appears before the message)
 * @param color Color for the label (from log_color_t enum)
 * @param message Format string (printf-style) for the message
 * @param ... Format arguments
 *
 * Used for consistent formatting of section headers and labeled output.
 * The label is colored, followed by the message content.
 * Output goes to both stderr and log file.
 * @ingroup logging
 */
void log_labeled(const char *label, log_color_t color, const char *message, ...);

/**
 * @brief Get color string for a given color enum
 * @param color Color enum value
 * @return ANSI color code string
 * @ingroup logging
 */
const char *log_level_color(log_color_t color);

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
 * @brief Lock terminal output for exclusive access by the calling thread
 *
 * Call this before interactive prompts (like password entry, yes/no questions)
 * to ensure only the calling thread can output to the terminal. Other threads'
 * log messages will be buffered and flushed when the terminal is unlocked.
 *
 * While locked:
 * - The locking thread can use log_plain() to write to terminal
 * - Other threads' log messages go to log file and are buffered
 * - Buffered messages are flushed to terminal on unlock
 *
 * Must be paired with log_unlock_terminal().
 *
 * @return The previous terminal lock state (for nested calls)
 * @ingroup logging
 */
bool log_lock_terminal(void);

/**
 * @brief Release terminal lock and flush buffered messages
 *
 * Call this after interactive prompts complete to release the terminal lock.
 * Buffered log messages from other threads will be flushed to terminal.
 *
 * @param previous_state The value returned by log_lock_terminal()
 * @ingroup logging
 */
void log_unlock_terminal(bool previous_state);

/**
 * @brief Set the delay between flushing buffered log entries
 *
 * When terminal output is re-enabled after an interactive prompt, buffered
 * log entries are flushed to the terminal. This setting adds a delay between
 * each entry for a visual animation effect.
 *
 * @param delay_ms Delay in milliseconds between each log entry (0 = no delay)
 * @ingroup logging
 */
void log_set_flush_delay(unsigned int delay_ms);

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

#include "crypto/crypto.h"

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
asciichat_error_t log_net_message(socket_t sockfd, const struct crypto_context_t *crypto_ctx, log_level_t level,
                                  remote_log_direction_t direction, const char *file, int line, const char *func,
                                  const char *fmt, ...);

/**
 * @name Logging Macros
 * @{
 *
 * @note Compile-time log level stripping: In release builds, log_dev() and log_debug()
 *       are compiled out completely (no runtime overhead). Override with LOG_COMPILE_LEVEL.
 */

/**
 * @brief Log a DEV message (most verbose, development only)
 * @param ... Format string and arguments (printf-style)
 *
 * @note DEV messages are stripped at compile-time in release builds.
 * @note In debug builds, includes file/line/function.
 * @ingroup logging
 */
#if LOG_COMPILE_LEVEL <= LOG_DEV
#ifdef NDEBUG
#define log_dev(...) log_msg(LOG_DEV, NULL, 0, NULL, __VA_ARGS__)
#else
#define log_dev(...) log_msg(LOG_DEV, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif
#else
#define log_dev(...) ((void)0)
#endif

/**
 * @brief Log a DEBUG message
 * @param ... Format string and arguments (printf-style)
 *
 * @note DEBUG messages are stripped at compile-time in release builds.
 * @note In debug builds, includes file/line/function.
 * @ingroup logging
 */
#if LOG_COMPILE_LEVEL <= LOG_DEBUG
#ifdef NDEBUG
#define log_debug(...) log_msg(LOG_DEBUG, NULL, 0, NULL, __VA_ARGS__)
#else
#define log_debug(...) log_msg(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif
#else
#define log_debug(...) ((void)0)
#endif

/**
 * @brief Log an INFO message
 * @param ... Format string and arguments (printf-style)
 *
 * @note INFO messages can be stripped via LOG_COMPILE_LEVEL=LOG_WARN.
 * @ingroup logging
 */
#if LOG_COMPILE_LEVEL <= LOG_INFO
#ifdef NDEBUG
#define log_info(...) log_msg(LOG_INFO, NULL, 0, NULL, __VA_ARGS__)
#else
#define log_info(...) log_msg(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif
#else
#define log_info(...) ((void)0)
#endif

/**
 * @brief Log a WARN message
 * @param ... Format string and arguments (printf-style)
 *
 * @note WARN messages can be stripped via LOG_COMPILE_LEVEL=LOG_ERROR.
 * @ingroup logging
 */
#if LOG_COMPILE_LEVEL <= LOG_WARN
#ifdef NDEBUG
#define log_warn(...) log_msg(LOG_WARN, NULL, 0, NULL, __VA_ARGS__)
#else
#define log_warn(...) log_msg(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif
#else
#define log_warn(...) ((void)0)
#endif

/**
 * @brief Log an ERROR message
 * @param ... Format string and arguments (printf-style)
 *
 * @note ERROR messages can be stripped via LOG_COMPILE_LEVEL=LOG_FATAL.
 * @ingroup logging
 */
#if LOG_COMPILE_LEVEL <= LOG_ERROR
#ifdef NDEBUG
#define log_error(...) log_msg(LOG_ERROR, NULL, 0, NULL, __VA_ARGS__)
#else
#define log_error(...) log_msg(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif
#else
#define log_error(...) ((void)0)
#endif

/**
 * @brief Log a FATAL message
 * @param ... Format string and arguments (printf-style)
 *
 * @note FATAL messages are never stripped (always compiled in).
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
 * @brief Plain logging to stderr with newline
 * @param ... Format string and arguments (printf-style)
 * @ingroup logging
 */
#define log_plain_stderr(...) log_plain_stderr_msg(__VA_ARGS__)

/**
 * @brief Plain logging to stderr without newline - for interactive prompts
 * @param ... Format string and arguments (printf-style)
 * @ingroup logging
 */
#define log_plain_stderr_nonewline(...) log_plain_stderr_nonewline_msg(__VA_ARGS__)

/**
 * @brief File-only logging - writes to log file only, no stderr output
 * @param ... Format string and arguments (printf-style)
 * @ingroup logging
 */
#define log_file(...) log_file_msg(__VA_ARGS__)

/** @} */

/**
 * @brief Rate-limited logging macro (thread-safe)
 *
 * Logs at most once per specified time interval. Useful for threads that have
 * an FPS and functions they call to prevent spammy logs.
 *
 * @param log_level Log level (DEV, DEBUG, INFO, WARN, ERROR, FATAL)
 * @param interval_us Minimum microseconds between log messages
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 *
 * @note Each call site maintains its own static atomic timer, so different call sites
 *       can log independently. Thread-safe via atomic compare-exchange.
 * @note Uses platform_get_monotonic_time_us() for cross-platform time.
 *
 * @ingroup logging
 */
#ifdef NDEBUG
#define log_every(log_level, interval_us, fmt, ...)                                                                    \
  do {                                                                                                                 \
    static LOG_ATOMIC_UINT64 _log_every_last_time = LOG_ATOMIC_UINT64_INIT(0);                                         \
    uint64_t _log_every_now = platform_get_monotonic_time_us();                                                        \
    uint64_t _log_every_last = atomic_load_explicit(&_log_every_last_time, memory_order_relaxed);                      \
    if (_log_every_now - _log_every_last >= (uint64_t)(interval_us)) {                                                 \
      if (atomic_compare_exchange_weak_explicit(&_log_every_last_time, &_log_every_last, _log_every_now,               \
                                                memory_order_relaxed, memory_order_relaxed)) {                         \
        log_msg(LOG_##log_level, NULL, 0, NULL, fmt, ##__VA_ARGS__);                                                   \
      }                                                                                                                \
    }                                                                                                                  \
  } while (0)
#else
#define log_every(log_level, interval_us, fmt, ...)                                                                    \
  do {                                                                                                                 \
    static LOG_ATOMIC_UINT64 _log_every_last_time = LOG_ATOMIC_UINT64_INIT(0);                                         \
    uint64_t _log_every_now = platform_get_monotonic_time_us();                                                        \
    uint64_t _log_every_last = atomic_load_explicit(&_log_every_last_time, memory_order_relaxed);                      \
    if (_log_every_now - _log_every_last >= (uint64_t)(interval_us)) {                                                 \
      if (atomic_compare_exchange_weak_explicit(&_log_every_last_time, &_log_every_last, _log_every_now,               \
                                                memory_order_relaxed, memory_order_relaxed)) {                         \
        log_msg(LOG_##log_level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);                                    \
      }                                                                                                                \
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

/* LOGGING_INTERNAL_ERROR macro is defined in logging.c (internal use only) */

/* Network logging convenience macros (log_*_net) are defined in src/server/client.h
 * since they require access to client_info_t. The base function log_net_message()
 * is available here for generic use. */

/* ============================================================================
 * Lock-Free MMAP Logging
 * ============================================================================ */

/* Forward declaration - full definition in log/mmap.h */
struct log_mmap_config;
typedef struct log_mmap_config log_mmap_config_t;

/**
 * @brief Enable lock-free mmap-based logging
 *
 * When enabled, log messages bypass the mutex and use atomic operations
 * to write directly to a memory-mapped log file as human-readable text.
 *
 * Benefits:
 * - No mutex contention between logging threads
 * - Crash-safe: text is written directly to mmap'd file, readable after crash
 * - Fast path uses atomic fetch_add, no locks
 * - ERROR/FATAL messages sync immediately for visibility
 * - Simple: log file IS the mmap file (no separate binary format)
 *
 * @param log_path Path to the log file (will be memory-mapped)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Call log_init() first, then log_enable_mmap() to upgrade to lock-free
 * @ingroup logging
 */
asciichat_error_t log_enable_mmap(const char *log_path);

/**
 * @brief Enable lock-free mmap logging with custom file size
 *
 * @param log_path Path to the log file
 * @param max_size Maximum file size in bytes (0 = default 4MB)
 * @return ASCIICHAT_OK on success, error code on failure
 * @ingroup logging
 */
asciichat_error_t log_enable_mmap_sized(const char *log_path, size_t max_size);

/**
 * @brief Disable mmap logging and return to mutex-based logging
 *
 * Flushes remaining entries and closes the mmap file.
 * @ingroup logging
 */
void log_disable_mmap(void);

/**
 * @brief Begin shutdown phase - disable console logging but keep file logging
 *
 * Call this before logging shutdown messages. Disables console output but
 * keeps file logging so messages are recorded for debugging.
 *
 * Useful when you want final messages (like "no servers found") to go to the
 * log file only, not to stdout where it might interfere with output.
 *
 * @ingroup logging
 */
void log_shutdown_begin(void);

/**
 * @brief End shutdown phase - restore previous logging settings
 *
 * Call after shutdown messages have been logged to restore console output.
 * @ingroup logging
 */
void log_shutdown_end(void);

#ifdef __cplusplus
}
#endif

/** @} */ /* logging */
