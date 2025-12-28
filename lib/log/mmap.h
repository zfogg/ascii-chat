#pragma once

/**
 * @file logging_mmap.h
 * @brief High-performance memory-mapped logging with crash safety
 * @ingroup logging
 *
 * This module provides an alternative logging backend using memory-mapped files
 * and atomic operations for minimal contention. Key features:
 *
 * - Lock-free logging: Uses atomic fetch_add for slot claiming, no mutex
 * - Crash-safe: Logs are in mmap'd file, kernel flushes on crash
 * - Optional flusher: Background thread writes human-readable log file
 * - Level-based flush: ERROR/FATAL flush immediately for crash visibility
 *
 * Usage:
 * @code
 * // Initialize mmap logging (call once at startup)
 * log_mmap_init("/tmp/myapp.mmap", "/tmp/myapp.log");
 *
 * // Use normal log_* macros - they automatically use mmap backend
 * log_debug("This uses atomic operations, no mutex!");
 *
 * // Cleanup on shutdown
 * log_mmap_destroy();
 * @endcode
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "common.h"
#include "platform/mmap.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum message length in a single log entry */
#define LOG_MMAP_MAX_MESSAGE 512

/** @brief Default number of log entries in the ring buffer */
#define LOG_MMAP_DEFAULT_ENTRIES 8192

/** @brief Magic number to validate mmap file */
#define LOG_MMAP_MAGIC 0x4C4F474D /* "LOGM" in little-endian */

/** @brief Version of the mmap log format */
#define LOG_MMAP_VERSION 1

/**
 * @brief A single log entry in the mmap ring buffer
 */
typedef struct {
  _Atomic uint64_t sequence;          /**< Entry sequence number (0 = unwritten) */
  uint64_t timestamp_ns;              /**< Nanosecond timestamp (CLOCK_MONOTONIC) */
  uint8_t level;                      /**< Log level (log_level_t) */
  uint8_t reserved[7];                /**< Padding for alignment */
  char message[LOG_MMAP_MAX_MESSAGE]; /**< Null-terminated message */
} log_mmap_entry_t;

/**
 * @brief Header of the mmap log file
 */
typedef struct {
  uint32_t magic;              /**< LOG_MMAP_MAGIC for validation */
  uint32_t version;            /**< LOG_MMAP_VERSION for compatibility */
  uint32_t entry_count;        /**< Number of entries in ring buffer */
  uint32_t entry_size;         /**< Size of each entry in bytes */
  _Atomic uint64_t write_head; /**< Next slot to write (wraps around) */
  uint64_t start_timestamp_ns; /**< Timestamp when logging started */
  char reserved[48];           /**< Reserved for future use */
} log_mmap_header_t;

/**
 * @brief Complete mmap log structure (header + entries)
 *
 * This structure is overlaid on the mmap'd file.
 */
typedef struct {
  log_mmap_header_t header;
  log_mmap_entry_t entries[]; /**< Flexible array of entries */
} log_mmap_buffer_t;

/**
 * @brief Configuration for mmap logging
 */
typedef struct {
  const char *mmap_path;      /**< Path to mmap file (required) */
  const char *text_log_path;  /**< Path to human-readable log (NULL to disable) */
  uint32_t entry_count;       /**< Number of entries (0 = default 8192) */
  uint32_t flush_interval_ms; /**< Flusher interval (0 = default 100ms) */
  bool immediate_error_flush; /**< Flush ERROR/FATAL immediately (default true) */
} log_mmap_config_t;

/**
 * @brief Initialize mmap-based logging
 *
 * Sets up the memory-mapped log buffer and optionally starts a background
 * flusher thread for human-readable output.
 *
 * @param config Configuration options
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t log_mmap_init(const log_mmap_config_t *config);

/**
 * @brief Initialize mmap logging with simple paths
 *
 * Convenience wrapper around log_mmap_init() with default settings.
 *
 * @param mmap_path Path to mmap file
 * @param text_log_path Path to human-readable log (NULL to disable flusher)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t log_mmap_init_simple(const char *mmap_path, const char *text_log_path);

/**
 * @brief Shutdown mmap logging
 *
 * Stops the flusher thread, flushes remaining entries, and unmaps the file.
 */
void log_mmap_destroy(void);

/**
 * @brief Write a log entry (lock-free)
 *
 * This is the fast path for logging. Uses atomic operations only, no mutex.
 *
 * @param level Log level
 * @param file Source file (can be NULL)
 * @param line Source line
 * @param func Function name (can be NULL)
 * @param fmt Format string
 * @param ... Format arguments
 */
void log_mmap_write(int level, const char *file, int line, const char *func, const char *fmt, ...);

/**
 * @brief Check if mmap logging is active
 *
 * @return true if mmap logging is initialized and active
 */
bool log_mmap_is_active(void);

/**
 * @brief Force flush all pending log entries
 *
 * Writes all unwritten entries to the text log file. Blocks until complete.
 * Called automatically on shutdown and for ERROR/FATAL levels.
 */
void log_mmap_flush(void);

/**
 * @brief Install signal handlers for crash safety
 *
 * Registers handlers for SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL that
 * will flush the log buffer before the process terminates.
 *
 * @note Signal handlers are automatically installed by log_mmap_init().
 *       Call this only if you need to reinstall after custom handler setup.
 */
void log_mmap_install_crash_handlers(void);

/**
 * @brief Get statistics about the mmap log
 *
 * @param[out] total_entries Total entries written since init
 * @param[out] flushed_entries Entries flushed to text log
 * @param[out] dropped_entries Entries dropped due to ring overflow
 */
void log_mmap_get_stats(uint64_t *total_entries, uint64_t *flushed_entries, uint64_t *dropped_entries);

#ifdef __cplusplus
}
#endif
