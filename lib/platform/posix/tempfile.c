/**
 * @file platform/posix/tempfile.c
 * @brief POSIX temporary file creation implementation
 */

#include "../tempfile.h"
#include "../../common.h"
#include "../../log/logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

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
