
/**
 * @file platform/abstraction.c
 * @ingroup platform
 * @brief 🏗️ Common platform abstraction stubs (OS-specific code in posix/ and windows/ subdirectories)
 */

#include "terminal.h"
#include "abstraction.h"
#include "../options/options.h"
#include "../common.h"

// ============================================================================
// Common Platform Functions
// ============================================================================
// This file is reserved for common platform functions that don't need
// OS-specific implementations. Currently all functions are OS-specific.
//
// The OS-specific implementations are in:
// - platform_windows.c (Windows)
// - platform_posix.c (POSIX/Unix/Linux/macOS)
//
// Socket-specific implementations are in:
// - platform/socket.c (Common socket utilities like socket_optimize_for_streaming)
// ============================================================================

/**
 * @brief Check if terminal control sequences should be used for the given fd
 * @param fd File descriptor to check
 * @return true if terminal control sequences should be used, false otherwise
 */
bool terminal_should_use_control_sequences(int fd) {
  if (fd < 0) {
    return false;
  }
  if (GET_OPTION(snapshot_mode)) {
    return false;
  }
  const char *testing_env = SAFE_GETENV("TESTING");
  if (testing_env != NULL) {
    return false;
  }
  return platform_isatty(fd) != 0;
}
