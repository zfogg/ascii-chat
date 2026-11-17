#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @file tests/common.h
 * @brief Common test utilities and environment detection
 * @ingroup testing
 * @addtogroup testing
 * @{
 *
 * This header provides only environment and executable-path helpers for tests.
 * Test files include their own Catch2 and project headers directly.
 *
 * CORE FEATURES:
 * ==============
 * - Test environment detection (CI, Docker, WSL)
 * - Platform-specific environment checks (POSIX/Windows)
 * - Headless environment detection for webcam tests
 *
 * TEST ENVIRONMENT:
 * ================
 * This header includes only the C library headers required by its inline
 * environment helpers. Catch2, logging, and project APIs are intentionally
 * included by each test that uses them.
 *
 * HEADLESS ENVIRONMENT DETECTION:
 * ===============================
 * Tests that require hardware (like webcam) can use
 * test_is_in_headless_environment() to skip when running in CI/Docker/WSL.
 *
 * @note This header must be included before any test code that uses
 *       the test environment detection functions.
 * @note Include this header only when a test needs one of these helpers.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

/* Storage capacities in elements; text buffers include the terminating NUL. */
#define TESTS_COMMON_VERSION_BUFFER_SIZE 256

#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#ifndef F_OK
#define F_OK 0
#endif
#endif

// Standard C headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// System headers needed by tests
#ifndef _WIN32
#include <unistd.h> // For unlink(), access(), etc.
#endif
#include <sys/stat.h> // For stat(), struct stat

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
 *     SKIP("Skipping webcam test in headless environment");
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

  // Docker and WSL detection use POSIX paths and are not applicable to native
  // Windows test processes.
#ifndef _WIN32
  if (access("/.dockerenv", F_OK) == 0) {
    return true;
  }

  // Check for WSL (look for "microsoft" or "WSL" in /proc/version)
  FILE *version_file = fopen("/proc/version", "r");
  if (version_file) {
    char version_buf[TESTS_COMMON_VERSION_BUFFER_SIZE];
    if (fgets(version_buf, sizeof(version_buf), version_file)) {
      if (strstr(version_buf, "microsoft") || strstr(version_buf, "Microsoft") || strstr(version_buf, "WSL")) {
        (void)fclose(version_file);
        return true;
      }
    }
    (void)fclose(version_file);
  }
#endif

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
#ifdef __cplusplus
extern "C" {
#endif
const char *test_get_binary_path(void);
#ifdef __cplusplus
}
#endif
