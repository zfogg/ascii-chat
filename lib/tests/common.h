#pragma once

/**
 * @file tests/common.h
 * @ingroup testing
 * @brief Common test utilities and environment detection
 *
 * This header provides common utilities and helpers for writing ascii-chat tests.
 * It includes standard test headers, platform detection, and environment checks
 * to help tests run reliably across different environments (CI, Docker, WSL).
 *
 * CORE FEATURES:
 * ==============
 * - Test environment detection (CI, Docker, WSL)
 * - Standard test framework includes (Criterion)
 * - Common project headers and utilities
 * - Platform-specific includes (POSIX/Windows)
 * - Headless environment detection for webcam tests
 *
 * TEST ENVIRONMENT:
 * ================
 * This header automatically includes:
 * - Criterion test framework
 * - Project common headers
 * - Test logging utilities
 * - Platform-specific system headers
 *
 * HEADLESS ENVIRONMENT DETECTION:
 * ===============================
 * Tests that require hardware (like webcam) can use
 * test_is_in_headless_environment() to skip when running in CI/Docker/WSL.
 *
 * @note This header must be included before any test code that uses
 *       the test environment detection functions.
 * @note All test files should include this header for consistent
 *       test infrastructure.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

#include <stdint.h>
#include <stdbool.h>

// Standard C headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// System headers needed by tests
#ifndef _WIN32
#include <unistd.h> // For unlink(), access(), etc.
#endif
#include <sys/stat.h> // For stat(), struct stat

// Now include Criterion which will pull in system headers
#include <criterion/criterion.h>

// Project headers - use relative paths from tests directory
#include "../common.h"
#include "logging.h"
#include "test_env.h"

// =============================================================================
// Test Environment Detection
// =============================================================================

/**
 * @brief Check if running in a headless environment without hardware support
 *
 * Detects if tests are running in an environment without access to hardware
 * devices like webcams. This is useful for skipping hardware-dependent tests
 * in CI, Docker, or WSL environments.
 *
 * Checks for:
 * - CI environment variables (CI=1)
 * - Docker container (/.dockerenv file)
 * - WSL (microsoft/WSL in /proc/version)
 *
 * @return true if running in CI/Docker/WSL (headless), false otherwise
 *
 * @note This function is safe to call on both POSIX and Windows systems.
 *       On Windows, Docker and CI checks still work, but WSL detection
 *       is POSIX-only.
 *
 * @ingroup testing
 *
 * @par Example
 * @code
 * Test(my_suite, webcam_test) {
 *   if (test_is_in_headless_environment()) {
 *     cr_skip_test("Skipping webcam test in headless environment");
 *   }
 *   // ... perform webcam test ...
 * }
 * @endcode
 */
static inline bool test_is_in_headless_environment(void) {
  // Check for CI environment
  if (getenv("CI") != NULL) {
    return true;
  }

  // Check for Docker
  if (access("/.dockerenv", F_OK) == 0) {
    return true;
  }

  // Check for WSL (look for "microsoft" or "WSL" in /proc/version)
  FILE *version_file = fopen("/proc/version", "r");
  if (version_file) {
    char version_buf[256];
    if (fgets(version_buf, sizeof(version_buf), version_file)) {
      if (strstr(version_buf, "microsoft") || strstr(version_buf, "Microsoft") || strstr(version_buf, "WSL")) {
        (void)fclose(version_file);
        return true;
      }
    }
    (void)fclose(version_file);
  }

  return false;
}

// =============================================================================
// Test Binary Path Detection
// =============================================================================

/**
 * @brief Get the path to the ascii-chat binary for integration tests
 *
 * This function finds the ascii-chat binary by trying multiple candidate paths.
 * It handles both direct test invocation from the repo root and ctest invocation
 * from the build directory.
 *
 * Search order:
 * 1. BUILD_DIR environment variable (if set)
 * 2. ./build_docker/bin/ascii-chat (Docker from repo root)
 * 3. ./build/bin/ascii-chat (local from repo root)
 * 4. ./bin/ascii-chat (from build directory - ctest)
 * 5. /app/build_docker/bin/ascii-chat (Docker absolute)
 *
 * @return Path to the ascii-chat binary, or a fallback path if not found
 *
 * @note The returned string is static - do not free it.
 * @note On Windows, returns paths with .exe extension.
 *
 * @ingroup testing
 *
 * @par Example
 * @code
 * Test(my_suite, integration_test) {
 *   const char *binary = test_get_binary_path();
 *   pid_t pid = fork();
 *   if (pid == 0) {
 *     execl(binary, "ascii-chat", "server", "--help", NULL);
 *     exit(127);
 *   }
 *   // ...
 * }
 * @endcode
 */
const char *test_get_binary_path(void);
