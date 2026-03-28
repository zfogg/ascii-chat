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
 * Note: If raylib is included (detected via RAYLIB_H), we skip redefining
 * the enumerators and just use int for log_level_t, since raylib already
 * defines LOG_DEBUG, LOG_INFO, LOG_ERROR, LOG_FATAL with compatible values.
 */
#ifndef RAYLIB_H
typedef enum {
  LOG_DEV = 0, /**< Development messages (most verbose) */
  LOG_DEBUG,   /**< Debug messages */
  LOG_INFO,    /**< Informational messages */
  LOG_WARN,    /**< Warning messages */
  LOG_ERROR,   /**< Error messages */
  LOG_FATAL    /**< Fatal error messages (most severe) */
} log_level_t;
#else
/* raylib is included; its enumerators (LOG_DEBUG, LOG_INFO, LOG_ERROR, LOG_FATAL)
   have compatible numeric values. Provide ascii-chat aliases for missing names. */
typedef int log_level_t;
#define LOG_DEV   LOG_TRACE
#define LOG_WARN  LOG_WARNING
#endif
