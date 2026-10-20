/**
 * @file platform/linux/filesystem.c
 * @brief Linux filesystem operations
 * @ingroup platform
 */

#ifdef __linux__

#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/paths.h>
#include <ascii-chat/util/string.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

asciichat_error_t platform_get_data_dir_candidates(platform_directory_list_t *list_out) {
  if (!list_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "list_out cannot be NULL");
  }

  list_out->candidates = SAFE_CALLOC(1, sizeof(*list_out->candidates), platform_directory_candidate_t *);
  list_out->count = 0;
  if (!list_out->candidates) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate system data directory list");
  }

  size_t len = strlen(ASCIICHAT_INSTALL_PREFIX) + strlen("/var/ascii-chat/") + 1;
  list_out->candidates[0].path = SAFE_MALLOC(len, char *);
  if (!list_out->candidates[0].path) {
    platform_directory_list_destroy(list_out);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate system data directory path");
  }

  safe_snprintf(list_out->candidates[0].path, len, "%s/var/ascii-chat/", ASCIICHAT_INSTALL_PREFIX);
  list_out->candidates[0].mode = 0755;
  list_out->count = 1;
  return ASCIICHAT_OK;
}

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
  // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound) - len bounded by path_size check above
  exe_path[len] = '\0';
  return true;
}

#endif
