/**
 * @file session/session_log_buffer.h
 * @brief Session log buffer interface
 * @ingroup session
 */

#pragma once

#include <stddef.h>
#include <time.h>

/**
 * @brief Session log entry
 */
typedef struct {
  char *text;       ///< Log text (allocated)
  size_t length;    ///< Text length
  time_t timestamp; ///< When log was recorded
} session_log_entry_t;
