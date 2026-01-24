/**
 * @file platform/posix/file.c
 * @brief POSIX file operations implementation
 * @ingroup platform
 */

#include "platform/file.h"
#include "common.h"
#include "log/logging.h"

#include <sys/stat.h>

// Group and other permissions mask - keys should only be readable by owner
#define SSH_KEY_PERMISSIONS_MASK (S_IRWXG | S_IRWXO)

asciichat_error_t platform_validate_key_file_permissions(const char *key_path) {
  if (!key_path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p", key_path);
    return ERROR_INVALID_PARAM;
  }

  struct stat st;
  if (stat(key_path, &st) != 0) {
    return SET_ERRNO_SYS(ERROR_CRYPTO_KEY, "Cannot stat key file: %s", key_path);
  }

  // Check for overly permissive permissions
  // SSH keys should only be readable by owner (permissions 0600 or 0400)
  if ((st.st_mode & SSH_KEY_PERMISSIONS_MASK) != 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Key file has overly permissive permissions: %o (recommended: 600 or 400)",
                     st.st_mode & 0777);
  }

  return ASCIICHAT_OK;
}
