#pragma once

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

// =============================================================================
// Test Environment Detection
// =============================================================================

/**
 * Check if we're running in an environment without webcam support
 * (CI, Docker, or WSL)
 *
 * @return true if running in CI/Docker/WSL, false otherwise
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
