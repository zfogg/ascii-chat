#pragma once

/**
 * @file platform/agent.h
 * @brief Cross-platform SSH/GPG agent socket discovery
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent functions for locating and connecting to:
 * - SSH agent (ssh-agent) via SSH_AUTH_SOCK
 * - GPG agent (gpg-agent) via GNUPGHOME or gpgconf
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stddef.h>
#include "system.h"

/* ============================================================================
 * Path Validation Helper Macro
 * ============================================================================ */

/**
 * @brief Validate and copy agent socket path
 * @param path Source path string
 * @param path_out Output buffer
 * @param path_size Output buffer size
 * @param context Description for error messages (e.g., "SSH_AUTH_SOCK")
 *
 * Checks that path is not empty, fits in buffer, and copies it safely.
 * Returns -1 on failure (logs error), returns 0 on success.
 */
#define VALIDATE_AGENT_PATH(path, path_out, path_size, context)                                                        \
  do {                                                                                                                 \
    if (!path || strlen(path) == 0) {                                                                                  \
      log_debug(context " not set");                                                                                   \
      return -1;                                                                                                       \
    }                                                                                                                  \
    if (strlen(path) >= path_size) {                                                                                   \
      log_error(context " path too long");                                                                             \
      return -1;                                                                                                       \
    }                                                                                                                  \
    SAFE_STRNCPY(path_out, path, path_size);                                                                           \
    return 0;                                                                                                          \
  } while (0)

/**
 * @brief Get home directory or special Windows path
 * @param env_var Primary environment variable (e.g., "HOME", "APPDATA")
 * @param fallback_env Fallback environment variable (e.g., "USERPROFILE")
 * @return Directory path or NULL if not found
 */
static inline const char *get_home_or_fallback(const char *env_var, const char *fallback_env) {
  const char *path = platform_getenv(env_var);
  if (!path || path[0] == '\0') {
    path = fallback_env ? platform_getenv(fallback_env) : NULL;
  }
  return path;
}

/**
 * Get the SSH agent socket/pipe path.
 *
 * Windows: Default named pipe "\\\\.\\pipe\\openssh-ssh-agent" or SSH_AUTH_SOCK.
 * Unix: Uses SSH_AUTH_SOCK environment variable.
 *
 * @param path_out Buffer to store path (should be at least 256 bytes)
 * @param path_size Size of path_out buffer
 * @return 0 on success, -1 on error
 */
int platform_get_ssh_agent_socket(char *path_out, size_t path_size);

/**
 * Get the GPG agent socket/named pipe path.
 *
 * Attempts to use gpgconf first, then falls back to default locations:
 * Windows: %APPDATA%\\gnupg\\S.gpg-agent
 * Unix: $GNUPGHOME/S.gpg-agent or ~/.gnupg/S.gpg-agent
 *
 * @param path_out Buffer to store path (should be at least 256 bytes)
 * @param path_size Size of path_out buffer
 * @return 0 on success, -1 on error
 */
int platform_get_gpg_agent_socket(char *path_out, size_t path_size);

/** @} */
