/**
 * @file platform/windows/fs.c
 * @ingroup platform
 * @brief Windows file system operations
 */

#ifdef _WIN32

#include "../fs.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include <windows.h>
#include <errno.h>
#include <string.h>

/**
 * @brief Create a directory (Windows implementation)
 */
asciichat_error_t platform_mkdir(const char *path, int mode) {
  UNUSED(mode); // Windows doesn't use Unix-style permissions

  if (!path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path to platform_mkdir");
    return ERROR_INVALID_PARAM;
  }

  if (CreateDirectoryA(path, NULL)) {
    return ASCIICHAT_OK;
  }

  DWORD error = GetLastError();
  if (error == ERROR_ALREADY_EXISTS) {
    // Verify it's a directory
    DWORD attrib = GetFileAttributesA(path);
    if (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY)) {
      return ASCIICHAT_OK;
    }
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Path exists but is not a directory: %s", path);
  }

  return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", path);
}

/**
 * @brief Create directories recursively (Windows implementation)
 */
asciichat_error_t platform_mkdir_recursive(const char *path, int mode) {
  UNUSED(mode); // Windows doesn't use Unix-style permissions

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
  // Skip drive letter on Windows (e.g., C:\ or C:/)
  char *start = tmp;
  if (len >= 2 && tmp[1] == ':') {
    start = tmp + 2;
  }

  for (char *p = start + 1; *p; p++) {
    if (*p == '/' || *p == '\\') {
      char orig = *p;
      *p = '\0';

      // Skip empty components
      if (tmp[0] != '\0' && strcmp(tmp, ".") != 0) {
        if (!CreateDirectoryA(tmp, NULL)) {
          DWORD error = GetLastError();
          if (error != ERROR_ALREADY_EXISTS) {
            *p = orig; // Restore before returning error
            return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", tmp);
          }
        }
      }

      *p = orig;
    }
  }

  // Create the final directory
  if (!CreateDirectoryA(tmp, NULL)) {
    DWORD error = GetLastError();
    if (error != ERROR_ALREADY_EXISTS) {
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", tmp);
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Get file statistics (Windows implementation)
 */
asciichat_error_t platform_stat(const char *path, platform_stat_t *stat_out) {
  if (!path || !stat_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to platform_stat");
    return ERROR_INVALID_PARAM;
  }

  WIN32_FILE_ATTRIBUTE_DATA attr;
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attr)) {
    return SET_ERRNO_SYS(ERROR_FILE_NOT_FOUND, "Failed to stat file: %s", path);
  }

  // Combine high and low parts to get full file size
  ULARGE_INTEGER size;
  size.LowPart = attr.nFileSizeLow;
  size.HighPart = attr.nFileSizeHigh;

  stat_out->size = (size_t)size.QuadPart;
  stat_out->mode = 0; // Windows doesn't have Unix-style modes

  // Check file type
  stat_out->is_directory = (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
  stat_out->is_symlink = (attr.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0;
  // Regular file excludes directories and symlinks (matches POSIX S_ISREG behavior)
  stat_out->is_regular_file = (stat_out->is_directory || stat_out->is_symlink) ? 0 : 1;

  return ASCIICHAT_OK;
}

/**
 * @brief Check if a path is a regular file (Windows implementation)
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
 * @brief Check if a path is a directory (Windows implementation)
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
