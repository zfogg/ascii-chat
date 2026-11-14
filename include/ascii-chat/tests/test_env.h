#pragma once

/**
 * @file tests/test_env.h
 * @brief Test environment detection utilities
 * @ingroup testing
 * @addtogroup testing
 * @{
 *
 * This header provides test environment detection functions that can be used
 * by both test code and production code (to adjust behavior like timeouts).
 *
 * Unlike common.h, this header has NO Catch2 dependency, so it can be
 * safely included by any code that needs to detect if it's running in a
 * test environment.
 *
 * @note This is the canonical location for test environment detection.
 *       Do NOT duplicate this logic in other files.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <libplatform/system.h>

/**
 * @brief Check if we're running in a test environment
 *
 * Detects test environment via:
 * - Compile-time: TESTING macro
 * - Runtime: TESTING environment variable
 *
 * This is used by network and crypto code to adjust timeouts and other
 * behavior during testing.
 *
 * @return true if in test environment, false otherwise
 * @ingroup testing
 */
static inline bool is_test_environment(void) {
#if defined(TESTING)
  return true; // Compile-time test environment detection
#else
  // Runtime check for environment variables (set by test harness)
  return platform_getenv("TESTING") != NULL;
#endif
}

/** @} */
