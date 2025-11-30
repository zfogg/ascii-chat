#pragma once

/**
 * @file tests/test_env.h
 * @ingroup testing
 * @brief Test environment detection utilities
 *
 * This header provides test environment detection functions that can be used
 * by both test code and production code (to adjust behavior like timeouts).
 *
 * Unlike common.h, this header has NO Criterion dependency, so it can be
 * safely included by any code that needs to detect if it's running in a
 * test environment.
 *
 * @note This is the canonical location for test environment detection.
 *       Do NOT duplicate this logic in other files.
 */

#include <stdbool.h>
#include <stdlib.h>
#include "../platform/system.h"

/**
 * @brief Check if we're running in a test environment
 *
 * Detects test environment via:
 * - Compile-time: CRITERION_TEST, __CRITERION__, TESTING macros
 * - Runtime: CRITERION_TEST or TESTING environment variables
 *
 * This is used by network and crypto code to adjust timeouts and other
 * behavior during testing.
 *
 * @return true if in test environment, false otherwise
 * @ingroup testing
 */
static inline bool is_test_environment(void) {
#if defined(CRITERION_TEST) || defined(__CRITERION__) || defined(TESTING)
  return true; // Compile-time test environment detection
#else
  // Runtime check for environment variables (set by test harness)
  return platform_getenv("CRITERION_TEST") != NULL || platform_getenv("TESTING") != NULL;
#endif
}
