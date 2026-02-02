/**
 * @file network/rate_limit/memory.h
 * @brief 🧠 In-memory rate limiting backend interface
 */

#pragma once

#include <ascii-chat/network/rate_limit/rate_limit.h>

/**
 * @brief Memory backend operations vtable
 */
extern const rate_limiter_backend_ops_t memory_backend_ops;

/**
 * @brief Create memory backend instance
 * @return Backend instance or NULL on failure
 */
void *memory_backend_create(void);

/**
 * @brief Helper: Get current time in milliseconds
 */
uint64_t rate_limiter_get_time_ms(void);

/**
 * @brief Helper: Get event type string for logging
 */
const char *rate_limiter_event_type_string(rate_event_type_t event_type);
