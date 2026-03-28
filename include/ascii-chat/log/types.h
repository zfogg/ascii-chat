/**
 * @file log/types.h
 * @brief Log level types and constants
 * @ingroup logging
 *
 * Lightweight header containing only log level definitions.
 * This header has NO dependencies to avoid circular inclusion issues.
 */

#pragma once

/**
 * @brief Logging levels enumeration
 * @ingroup logging
 *
 * Defines the severity levels for log messages.
 * Used throughout the logging system without circular dependencies.
 *
 * Note: We use our own log levels. On WASM with raylib, we wrap raylib's enum values.
 */

#ifndef RAYLIB_H
/* Non-WASM or raylib not yet included: define our own enum */
typedef enum {
  ASCIICHAT_LOG_DEV = 0, /**< Development messages (most verbose) */
  ASCIICHAT_LOG_DEBUG,   /**< Debug messages */
  ASCIICHAT_LOG_INFO,    /**< Informational messages */
  ASCIICHAT_LOG_WARN,    /**< Warning messages */
  ASCIICHAT_LOG_ERROR,   /**< Error messages */
  ASCIICHAT_LOG_FATAL    /**< Fatal error messages (most severe) */
} log_level_t;

#define LOG_DEV ASCIICHAT_LOG_DEV
#define LOG_DEBUG ASCIICHAT_LOG_DEBUG
#define LOG_INFO ASCIICHAT_LOG_INFO
#define LOG_WARN ASCIICHAT_LOG_WARN
#define LOG_ERROR ASCIICHAT_LOG_ERROR
#define LOG_FATAL ASCIICHAT_LOG_FATAL

#else
/* raylib.h is included: use its enum values */
typedef int log_level_t;

#define LOG_DEV LOG_TRACE       /**< Development messages (raylib: LOG_TRACE) */
#define LOG_WARN LOG_WARNING    /**< Warning messages (raylib: LOG_WARNING) */
/* LOG_DEBUG, LOG_INFO, LOG_ERROR, LOG_FATAL already defined by raylib */
#endif
