/**
 * @file network/rate_limit/rate_limit.h
 * @brief 🚦 Rate limiting API with pluggable backends
 *
 * Supports two backends:
 * - Memory: Thread-safe in-memory tracking with uthash (for ascii-chat server)
 * - SQLite: Persistent database tracking (for acds discovery server)
 *
 * Example usage:
 * @code
 * // Create in-memory rate limiter
 * rate_limiter_t *limiter = rate_limiter_create_memory();
 *
 * // Or SQLite-backed limiter
 * rate_limiter_t *limiter = rate_limiter_create_sqlite("acds.db");
 *
 * // Check rate limit
 * bool allowed = false;
 * rate_limiter_check(limiter, "192.168.1.100", RATE_EVENT_SESSION_CREATE, NULL, &allowed);
 *
 * if (allowed) {
 *     rate_limiter_record(limiter, "192.168.1.100", RATE_EVENT_SESSION_CREATE);
 *     // Process request
 * } else {
 *     // Reject request
 * }
 *
 * // Cleanup (call periodically)
 * rate_limiter_cleanup(limiter, 3600);
 *
 * // Destroy
 * rate_limiter_destroy(limiter);
 * @endcode
 */

#pragma once

#include "../../asciichat_errno.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Rate limit event types
 */
typedef enum {
  // ACDS discovery server events
  RATE_EVENT_SESSION_CREATE = 0, ///< Session creation
  RATE_EVENT_SESSION_LOOKUP = 1, ///< Session lookup
  RATE_EVENT_SESSION_JOIN = 2,   ///< Session join

  // ascii-chat server events
  RATE_EVENT_CONNECTION = 3,  ///< New connection
  RATE_EVENT_IMAGE_FRAME = 4, ///< Image frame from client (PACKET_TYPE_IMAGE_FRAME)
  RATE_EVENT_AUDIO = 5,       ///< Audio packet (PACKET_TYPE_AUDIO, PACKET_TYPE_AUDIO_BATCH)
  RATE_EVENT_PING = 6,        ///< Ping/pong keepalive (PACKET_TYPE_PING, PACKET_TYPE_PONG)
  RATE_EVENT_CLIENT_JOIN = 7, ///< Client join request (PACKET_TYPE_CLIENT_JOIN)
  RATE_EVENT_CONTROL = 8,     ///< Control packets (CAPABILITIES, STREAM_START/STOP, LEAVE)

  RATE_EVENT_MAX ///< Sentinel value
} rate_event_type_t;

/**
 * @brief Rate limit configuration
 */
typedef struct {
  uint32_t max_events;  ///< Maximum events allowed
  uint32_t window_secs; ///< Time window in seconds
} rate_limit_config_t;

/**
 * @brief Opaque rate limiter handle
 *
 * Backend implementation is hidden from users.
 */
typedef struct rate_limiter_s rate_limiter_t;

/**
 * @brief Backend operations vtable
 *
 * Each backend (memory, sqlite) implements these operations.
 */
typedef struct {
  asciichat_error_t (*check)(void *backend_data, const char *ip_address, rate_event_type_t event_type,
                             const rate_limit_config_t *config, bool *allowed);

  asciichat_error_t (*record)(void *backend_data, const char *ip_address, rate_event_type_t event_type);

  asciichat_error_t (*cleanup)(void *backend_data, uint32_t max_age_secs);

  void (*destroy)(void *backend_data);
} rate_limiter_backend_ops_t;

/**
 * @brief Default rate limits for each event type
 */
extern const rate_limit_config_t DEFAULT_RATE_LIMITS[RATE_EVENT_MAX];

// ============================================================================
// Rate Limiter Creation
// ============================================================================

/**
 * @brief Create in-memory rate limiter
 *
 * Thread-safe implementation using uthash and mutexes.
 * Suitable for ascii-chat server where persistence is not needed.
 *
 * @return Rate limiter instance or NULL on failure
 */
rate_limiter_t *rate_limiter_create_memory(void);

/**
 * @brief Create SQLite-backed rate limiter
 *
 * Persistent implementation using SQLite database.
 * Suitable for acds discovery server where persistence is needed.
 *
 * @param db_path Path to SQLite database (NULL = externally managed database)
 * @return Rate limiter instance or NULL on failure
 */
rate_limiter_t *rate_limiter_create_sqlite(const char *db_path);

/**
 * @brief Set SQLite database handle for rate limiter
 *
 * For SQLite-backed rate limiters where the database lifecycle is managed
 * externally (e.g., ACDS manages its own database). Must be called after
 * rate_limiter_create_sqlite(NULL).
 *
 * @param limiter Rate limiter instance (must be SQLite backend)
 * @param db SQLite database handle
 */
void rate_limiter_set_sqlite_db(rate_limiter_t *limiter, void *db);

/**
 * @brief Destroy rate limiter and free resources
 *
 * @param limiter Rate limiter instance (NULL-safe)
 */
void rate_limiter_destroy(rate_limiter_t *limiter);

// ============================================================================
// Rate Limiting Operations
// ============================================================================

/**
 * @brief Check if an event from an IP address should be rate limited
 *
 * Uses sliding window: counts events in the last `window_secs` seconds.
 *
 * @param limiter Rate limiter instance
 * @param ip_address IP address string (IPv4 or IPv6)
 * @param event_type Type of event
 * @param config Rate limit configuration (NULL = use defaults)
 * @param[out] allowed True if event is allowed, false if rate limited
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t rate_limiter_check(rate_limiter_t *limiter, const char *ip_address, rate_event_type_t event_type,
                                     const rate_limit_config_t *config, bool *allowed);

/**
 * @brief Record a rate limit event
 *
 * Should be called after rate_limiter_check() returns allowed=true.
 *
 * @param limiter Rate limiter instance
 * @param ip_address IP address string
 * @param event_type Type of event
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t rate_limiter_record(rate_limiter_t *limiter, const char *ip_address, rate_event_type_t event_type);

/**
 * @brief Clean up old rate limit events
 *
 * Deletes events older than the specified age to prevent unbounded growth.
 * Should be called periodically (e.g., every 5 minutes).
 *
 * @param limiter Rate limiter instance
 * @param max_age_secs Delete events older than this (0 = use default 1 hour)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t rate_limiter_cleanup(rate_limiter_t *limiter, uint32_t max_age_secs);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Get event type string for logging/database storage
 *
 * @param event_type Event type enum value
 * @return Event type name string (e.g., "session_create", "connection")
 */
const char *rate_limiter_event_type_string(rate_event_type_t event_type);
