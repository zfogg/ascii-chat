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
 */
typedef enum {
  LOG_DEV = 0, /**< Development messages (most verbose) */
  LOG_DEBUG,   /**< Debug messages */
  LOG_INFO,    /**< Informational messages */
  LOG_WARN,    /**< Warning messages */
  LOG_ERROR,   /**< Error messages */
  LOG_FATAL    /**< Fatal error messages (most severe) */
} log_level_t;
