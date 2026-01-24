/**
 * @file platform/posix/filesystem.c
 * @brief POSIX filesystem operations
 * @ingroup platform
 */

#ifndef _WIN32

#include "../filesystem.h"
#include "../../common.h"
#include "../../log/logging.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

// ============================================================================
// Directory Management
// ============================================================================

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

// ============================================================================
// File Statistics
// ============================================================================

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
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: path=%p", path);
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
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: path=%p", path);
    return 0;
  }

  platform_stat_t stat_info;
  if (platform_stat(path, &stat_info) != ASCIICHAT_OK) {
    return 0;
  }

  return stat_info.is_directory;
}

// ============================================================================
// Temporary Files and Directories
// ============================================================================

int platform_create_temp_file(char *path_out, size_t path_size, const char *prefix, int *fd) {
  /* Unix temp file creation with mkstemp() - include PID for concurrent process safety */
  int needed = snprintf(path_out, path_size, "/tmp/%s_%d_XXXXXX", prefix, getpid());
  if (needed < 0 || (size_t)needed >= path_size) {
    return -1;
  }

  int temp_fd = mkstemp(path_out);
  if (temp_fd < 0) {
    return -1;
  }

  *fd = temp_fd;
  return 0;
}

int platform_delete_temp_file(const char *path) {
  return unlink(path);
}

asciichat_error_t platform_mkdtemp(char *path_out, size_t path_size, const char *prefix) {
  if (!path_out || path_size < 32 || !prefix) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for platform_mkdtemp");
  }

  /* Create temp directory template with PID for concurrent process safety */
  int needed = snprintf(path_out, path_size, "/tmp/%s_%d_XXXXXX", prefix, getpid());
  if (needed < 0 || (size_t)needed >= path_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Path buffer too small for temporary directory");
  }

  /* mkdtemp modifies the template in-place and returns the path on success */
  if (mkdtemp(path_out) == NULL) {
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create temporary directory");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t platform_rmdir_recursive(const char *path) {
  if (!path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path is NULL");
  }

  DIR *dir = opendir(path);
  if (!dir) {
    /* Path doesn't exist or isn't a directory - treat as success (no-op) */
    return ASCIICHAT_OK;
  }

  struct dirent *entry;
  asciichat_error_t result = ASCIICHAT_OK;

  while ((entry = readdir(dir)) != NULL) {
    /* Skip . and .. */
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    /* Build full path to entry */
    char full_path[PLATFORM_MAX_PATH_LENGTH];
    int len = snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
    if (len < 0 || len >= (int)sizeof(full_path)) {
      log_warn("Path too long during directory cleanup: %s/%s", path, entry->d_name);
      continue;
    }

    /* Check if it's a directory using stat */
    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
      /* Recursively delete subdirectory */
      asciichat_error_t subdir_result = platform_rmdir_recursive(full_path);
      if (subdir_result != ASCIICHAT_OK) {
        result = subdir_result;
      }
    } else {
      /* Delete file */
      if (unlink(full_path) != 0) {
        log_warn("Failed to delete file during cleanup: %s", full_path);
        result = ERROR_FILE_OPERATION;
      }
    }
  }

  closedir(dir);

  /* Delete the directory itself */
  if (rmdir(path) != 0) {
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to delete directory: %s", path);
  }

  return result;
}

// ============================================================================
// Key File Security
// ============================================================================

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

#endif
