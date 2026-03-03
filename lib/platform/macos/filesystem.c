/**
 * @file platform/macos/filesystem.c
 * @brief macOS filesystem operations
 * @ingroup platform
 */

#ifdef __APPLE__

#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <mach-o/dyld.h>
#include <stdint.h>

/**
 * @brief Get the path to the currently running executable (macOS implementation)
 */
bool platform_get_executable_path(char *exe_path, size_t path_size) {
  if (!exe_path || path_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: exe_path=%p, path_size=%zu", (void *)exe_path, path_size);
    return false;
  }

  uint32_t bufsize = (uint32_t)path_size;
  int result = _NSGetExecutablePath(exe_path, &bufsize);
  if (result != 0) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "_NSGetExecutablePath failed: path requires %u bytes, buffer size = %zu bytes",
              bufsize, path_size);
    return false;
  }
  return true;
}

#endif
