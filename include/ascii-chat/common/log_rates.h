/**
 * @defgroup log_rates Logging Rate Limits
 * @ingroup module_core
 * @brief Standard rate limits for log_*_every() macros
 *
 * @file log_rates.h
 * @brief Logging rate limit constants (in microseconds)
 * @ingroup log_rates
 * @addtogroup log_rates
 * @{
 *
 * Standard rate limits for log_*_every() macros to reduce log spam
 * in high-frequency code paths.
 *
 * Usage: log_debug_every(LOG_RATE_VIDEO_FRAME, "message")
 *
 * @ingroup log_rates
 */

#pragma once

/** @brief Log rate limit: 0.1 seconds (100,000 microseconds) - for very high frequency operations */
#define LOG_RATE_VERY_FAST (100000)

/** @brief Log rate limit: 1 second (1,000,000 microseconds) */
#define LOG_RATE_FAST (1000000)

/** @brief Log rate limit: 3 seconds (3,000,000 microseconds) */
#define LOG_RATE_NORMAL (3000000)

/** @brief Log rate limit: 5 seconds (5,000,000 microseconds) - default for audio/video packets */
#define LOG_RATE_DEFAULT (5000000)

/** @brief Log rate limit: 10 seconds (10,000,000 microseconds) */
#define LOG_RATE_SLOW (10000000)

/** @} */
