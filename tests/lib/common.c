/**
 * @file tests/common.c
 * @ingroup testing
 * @brief Common test utilities implementation
 */

#include <stdbool.h>
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "../common.h"

const char *test_get_binary_path(void) {
  static char binary_path[256];
  static bool initialized = false;

  if (initialized) {
    return binary_path;
  }

#ifdef _WIN32
  // Windows: try several paths
  const char *candidates[] = {
      "./build/bin/ascii-chat.exe",
      "./bin/ascii-chat.exe",
      "ascii-chat.exe",
  };
  const char *fallback = "./build/bin/ascii-chat.exe";
#else
  // Check if we're in Docker (/.dockerenv file exists)
  bool in_docker = (access("/.dockerenv", F_OK) == 0);
  const char *build_dir = getenv("BUILD_DIR");

  // Try BUILD_DIR first if set
  if (build_dir) {
    safe_snprintf(binary_path, sizeof(binary_path), "./%s/bin/ascii-chat", build_dir);
    if (access(binary_path, X_OK) == 0) {
      initialized = true;
      return binary_path;
    }
  }

  // Try several paths in order of preference
  const char *candidates[] = {
      // Relative paths from repo root
      in_docker ? "./build_docker/bin/ascii-chat" : "./build/bin/ascii-chat",
      // Relative paths from build directory (when ctest runs from there)
      "./bin/ascii-chat",
      // Absolute paths for Docker
      in_docker ? "/app/build_docker/bin/ascii-chat" : NULL,
  };
  const char *fallback = in_docker ? "./build_docker/bin/ascii-chat" : "./build/bin/ascii-chat";
#endif

  // Try each candidate path
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    if (candidates[i] && access(candidates[i], X_OK) == 0) {
      safe_snprintf(binary_path, sizeof(binary_path), "%s", candidates[i]);
      initialized = true;
      return binary_path;
    }
  }

  // Fallback to default (will likely fail but gives useful error)
  safe_snprintf(binary_path, sizeof(binary_path), "%s", fallback);
  initialized = true;
  return binary_path;
}
