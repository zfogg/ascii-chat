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
