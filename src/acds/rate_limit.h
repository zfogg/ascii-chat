/**
 * @file acds/rate_limit.h
 * @brief 🚦 Rate limiting API for ACDS
 *
 * Sliding window rate limiting to prevent abuse:
 * - Tracks events per IP address and event type
 * - Configurable limits (max events per time window)
 * - Automatic cleanup of old events
 */

#pragma once

#include "asciichat_errno.h"
#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Rate limit event types
 */
typedef enum {
  RATE_EVENT_SESSION_CREATE = 0, ///< Session creation
  RATE_EVENT_SESSION_LOOKUP = 1, ///< Session lookup
  RATE_EVENT_SESSION_JOIN = 2,   ///< Session join
  RATE_EVENT_MAX                 ///< Sentinel value
} rate_event_type_t;

/**
 * @brief Rate limit configuration
 */
typedef struct {
  uint32_t max_events;  ///< Maximum events allowed
  uint32_t window_secs; ///< Time window in seconds
} rate_limit_config_t;

/**
 * @brief Default rate limits for each event type
 */
extern const rate_limit_config_t DEFAULT_RATE_LIMITS[RATE_EVENT_MAX];

/**
 * @brief Check if an event from an IP address should be rate limited
 *
 * Uses sliding window: counts events in the last `window_secs` seconds.
 *
 * @param db SQLite database handle
 * @param ip_address IP address string (IPv4 or IPv6)
 * @param event_type Type of event
 * @param config Rate limit configuration (NULL = use defaults)
 * @param[out] allowed True if event is allowed, false if rate limited
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t rate_limit_check(sqlite3 *db, const char *ip_address, rate_event_type_t event_type,
                                   const rate_limit_config_t *config, bool *allowed);

/**
 * @brief Record a rate limit event
 *
 * Should be called after rate_limit_check() returns allowed=true.
 *
 * @param db SQLite database handle
 * @param ip_address IP address string
 * @param event_type Type of event
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t rate_limit_record(sqlite3 *db, const char *ip_address, rate_event_type_t event_type);

/**
 * @brief Clean up old rate limit events
 *
 * Deletes events older than the maximum window size to prevent database bloat.
 * Should be called periodically (e.g., every 5 minutes).
 *
 * @param db SQLite database handle
 * @param max_age_secs Delete events older than this (0 = use default 1 hour)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t rate_limit_cleanup(sqlite3 *db, uint32_t max_age_secs);
