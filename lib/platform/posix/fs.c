/**
 * @file platform/posix/fs.c
 * @ingroup platform
 * @brief POSIX file system operations
 */

#ifndef _WIN32

#include "../fs.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

/**
 * @brief Create a directory (POSIX implementation)
 */
asciichat_error_t platform_mkdir(const char *path, int mode) {
  if (!path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path to platform_mkdir");
    return ERROR_INVALID_PARAM;
  }

  if (mkdir(path, mode) == -1) {
    // EEXIST is not an error - directory may already exist
    if (errno == EEXIST) {
      // Verify it's actually a directory
      struct stat sb;
      if (lstat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        return ASCIICHAT_OK;
      }
      // Path exists but is not a directory
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Path exists but is not a directory: %s", path);
    }
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", path);
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Get file statistics (POSIX implementation)
 */
asciichat_error_t platform_stat(const char *path, platform_stat_t *stat_out) {
  if (!path || !stat_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to platform_stat");
    return ERROR_INVALID_PARAM;
  }

  struct stat sb;
  if (lstat(path, &sb) == -1) {
    return SET_ERRNO_SYS(ERROR_FILE_NOT_FOUND, "Failed to stat file: %s", path);
  }

  stat_out->size = (size_t)sb.st_size;
  stat_out->mode = sb.st_mode;
  stat_out->is_regular_file = S_ISREG(sb.st_mode) ? 1 : 0;
  stat_out->is_directory = S_ISDIR(sb.st_mode) ? 1 : 0;
  stat_out->is_symlink = S_ISLNK(sb.st_mode) ? 1 : 0;

  return ASCIICHAT_OK;
}

/**
 * @brief Check if a path is a regular file (POSIX implementation)
 */
int platform_is_regular_file(const char *path) {
  if (!path) {
    return 0;
  }

  platform_stat_t stat_info;
  if (platform_stat(path, &stat_info) != ASCIICHAT_OK) {
    return 0;
  }

  return stat_info.is_regular_file;
}

/**
 * @brief Check if a path is a directory (POSIX implementation)
 */
int platform_is_directory(const char *path) {
  if (!path) {
    return 0;
  }

  platform_stat_t stat_info;
  if (platform_stat(path, &stat_info) != ASCIICHAT_OK) {
    return 0;
  }

  return stat_info.is_directory;
}

#endif
