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
      // Verify it's actually a directory (use stat() to follow symlinks)
      struct stat sb;
      if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
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
 * @brief Create directories recursively (POSIX implementation)
 */
asciichat_error_t platform_mkdir_recursive(const char *path, int mode) {
  if (!path || path[0] == '\0') {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path to platform_mkdir_recursive");
    return ERROR_INVALID_PARAM;
  }

  char tmp[512];
  size_t len = strlen(path);
  if (len >= sizeof(tmp)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Path too long for platform_mkdir_recursive: %zu", len);
    return ERROR_INVALID_PARAM;
  }

  strncpy(tmp, path, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  // Create each directory in the path
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/' || *p == '\\') {
      char orig = *p;
      *p = '\0';

      // Skip empty components
      if (tmp[0] != '\0' && strcmp(tmp, ".") != 0) {
        if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
          // Propagate error if not EEXIST
          return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", tmp);
        }
      }

      *p = orig;
    }
  }

  // Create the final directory
  if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", tmp);
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
