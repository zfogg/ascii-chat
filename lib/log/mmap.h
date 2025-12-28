#pragma once

/**
 * @file mmap.h
 * @brief Lock-free memory-mapped text logging with crash safety
 * @ingroup logging
 *
 * This module provides crash-safe logging by writing human-readable text
 * directly to a memory-mapped log file. Key features:
 *
 * - Lock-free logging: Uses atomic fetch_add for position claiming
 * - Crash-safe: Text is written directly to mmap'd file, readable after crash
 * - No flusher needed: Logs are human-readable in the file immediately
 * - Pure text: No binary header, works with grep/rg/cat without special flags
 * - Simple design: Just mmap the .log file and write text to it
 *
 * Usage:
 * @code
 * // Initialize mmap logging (call once at startup)
 * log_mmap_init("/tmp/myapp.log", 4 * 1024 * 1024);  // 4MB log file
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

/** @brief Default mmap log file size (4MB) */
#define LOG_MMAP_DEFAULT_SIZE (4 * 1024 * 1024)

/**
 * @brief Configuration for mmap logging
 */
typedef struct log_mmap_config {
  const char *log_path; /**< Path to log file (required) */
  size_t max_size;      /**< Maximum file size (0 = default 4MB) */
} log_mmap_config_t;

/**
 * @brief Initialize mmap-based text logging
 *
 * Creates or opens a memory-mapped log file. Text is written directly
 * to the file, so it's readable even after a crash.
 *
 * @param config Configuration options
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t log_mmap_init(const log_mmap_config_t *config);

/**
 * @brief Initialize mmap logging with simple parameters
 *
 * Convenience wrapper around log_mmap_init().
 *
 * @param log_path Path to log file
 * @param max_size Maximum file size (0 = default 4MB)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t log_mmap_init_simple(const char *log_path, size_t max_size);

/**
 * @brief Shutdown mmap logging
 *
 * Syncs and unmaps the log file.
 */
void log_mmap_destroy(void);

/**
 * @brief Write a log entry directly to the mmap'd file (lock-free)
 *
 * Formats the log message and writes it directly as human-readable text.
 * Uses atomic operations only, no mutex.
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
 * @brief Force sync the mmap'd file to disk
 *
 * Ensures all written data is flushed to the underlying file.
 * Called automatically for ERROR/FATAL levels and on shutdown.
 */
void log_mmap_sync(void);

/**
 * @brief Install signal handlers for crash safety
 *
 * Registers handlers for SIGSEGV, SIGABRT, etc. that sync the mmap
 * before the process terminates.
 *
 * @note Signal handlers are automatically installed by log_mmap_init().
 */
void log_mmap_install_crash_handlers(void);

/**
 * @brief Get statistics about the mmap log
 *
 * @param[out] bytes_written Total bytes written since init
 * @param[out] wrap_count Number of times log has wrapped
 */
void log_mmap_get_stats(uint64_t *bytes_written, uint64_t *wrap_count);

/**
 * @brief Get current mmap log usage
 *
 * @param[out] used Current bytes used in the log
 * @param[out] capacity Total capacity of the log
 * @return true if mmap is active and values are valid
 */
bool log_mmap_get_usage(size_t *used, size_t *capacity);

/**
 * @brief Rotate the mmap log (tail-keeping rotation)
 *
 * Keeps the most recent log entries (tail) and discards old ones.
 * This is the mmap equivalent of file-based log rotation.
 *
 * @note Caller must hold the rotation mutex from logging.c.
 *       This is called by maybe_rotate_log() which handles locking.
 */
void log_mmap_rotate(void);

#ifdef __cplusplus
}
#endif
