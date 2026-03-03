/**
 * @file platform/linux/filesystem.c
 * @brief Linux filesystem operations
 * @ingroup platform
 */

#ifdef __linux__

#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/**
 * @brief Get the path to the currently running executable (Linux implementation)
 */
bool platform_get_executable_path(char *exe_path, size_t path_size) {
  if (!exe_path || path_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: exe_path=%p, path_size=%zu", (void *)exe_path, path_size);
    return false;
  }

  ssize_t len = readlink("/proc/self/exe", exe_path, path_size - 1);
  if (len < 0) {
    SET_ERRNO_SYS(ERROR_INVALID_STATE, "readlink(\"/proc/self/exe\") failed: %s", SAFE_STRERROR(errno));
    return false;
  }
  if ((size_t)len >= path_size - 1) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW,
              "Executable path exceeds buffer size (path length >= %zu bytes, buffer size = %zu bytes)", (size_t)len,
              path_size);
    return false;
  }
  exe_path[len] = '\0';
  return true;
}

#endif
