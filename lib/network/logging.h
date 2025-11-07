#pragma once

/**
 * @file lib/network/logging.h
 * @ingroup logging
 * @brief Remote logging direction enumeration shared between logging and packet layers.
 */

/**
 * @brief Remote log packet direction enumeration.
 *
 * Indicates the originator of a remote log message so receivers can annotate
 * logs clearly.
 */
typedef enum remote_log_direction {
  REMOTE_LOG_DIRECTION_UNKNOWN = 0,
  REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT = 1,
  REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER = 2
} remote_log_direction_t;
