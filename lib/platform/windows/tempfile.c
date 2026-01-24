/**
 * @file platform/windows/tempfile.c
 * @brief Windows temporary file creation implementation
 */

#include "../tempfile.h"
#include "../../common.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int platform_create_temp_file(char *path_out, size_t path_size, const char *prefix, int *fd) {
  /* Windows temp file creation with process ID for concurrent process safety */
  char temp_dir[PLATFORM_MAX_PATH_LENGTH];
  DWORD temp_dir_len = GetTempPathA(sizeof(temp_dir), temp_dir);
  if (temp_dir_len == 0 || temp_dir_len >= sizeof(temp_dir)) {
    return -1;
  }

  /* Create process-specific temp file prefix (e.g., "asc_sig_12345_") */
  char temp_prefix[32];
  safe_snprintf(temp_prefix, sizeof(temp_prefix), "%s_%lu_", prefix, GetCurrentProcessId());

  /* Create temp file with GetTempFileName */
  if (GetTempFileNameA(temp_dir, temp_prefix, 0, path_out) == 0 || (int)strlen(path_out) >= (int)path_size) {
    return -1;
  }

  /* Windows: Return -1 for fd since the file is already created and closed by GetTempFileName */
  *fd = -1;
  return 0;
}

int platform_delete_temp_file(const char *path) {
  if (DeleteFileA(path)) {
    return 0;
  }
  return -1;
}

asciichat_error_t platform_mkdtemp(char *path_out, size_t path_size, const char *prefix) {
  if (!path_out || path_size < 32 || !prefix) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for platform_mkdtemp");
  }

  /* Get Windows temp directory */
  char temp_dir[PLATFORM_MAX_PATH_LENGTH];
  DWORD temp_dir_len = GetTempPathA(sizeof(temp_dir), temp_dir);
  if (temp_dir_len == 0 || temp_dir_len >= sizeof(temp_dir)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Failed to get temp directory path");
  }

  /* Create process-specific temp directory prefix (e.g., "ascii-chat-gpg_12345_") */
  char temp_prefix[32];
  safe_snprintf(temp_prefix, sizeof(temp_prefix), "%s_%lu_", prefix, GetCurrentProcessId());

  /* Create unique temp directory name */
  char unique_name[32];
  static unsigned int counter = 0;
  safe_snprintf(unique_name, sizeof(unique_name), "%s%u", temp_prefix, InterlockedIncrement((LONG *)&counter));

  /* Build full path */
  int needed = safe_snprintf(path_out, path_size, "%s%s", temp_dir, unique_name);
  if (needed < 0 || (size_t)needed >= path_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Path buffer too small for temporary directory");
  }

  /* Create the directory */
  if (CreateDirectoryA(path_out, NULL)) {
    return ASCIICHAT_OK;
  }

  return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create temporary directory");
}

asciichat_error_t platform_rmdir_recursive(const char *path) {
  if (!path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path is NULL");
  }

  /* Check if path exists and is a directory */
  WIN32_FILE_ATTRIBUTE_DATA attrs;
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attrs)) {
    /* Path doesn't exist - treat as success (no-op) */
    return ASCIICHAT_OK;
  }

  if (!(attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    /* Not a directory - treat as error */
    return SET_ERRNO(ERROR_INVALID_PARAM, "path is not a directory");
  }

  /* Find all files in the directory */
  WIN32_FIND_DATAA find_data;
  HANDLE find_handle;
  asciichat_error_t result = ASCIICHAT_OK;

  char search_path[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(search_path, sizeof(search_path), "%s\\*", path);

  find_handle = FindFirstFileA(search_path, &find_data);
  if (find_handle == INVALID_HANDLE_VALUE) {
    /* Can't read directory - might be empty, try removing it directly */
    if (RemoveDirectoryA(path)) {
      return ASCIICHAT_OK;
    }
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to delete directory: %s", path);
  }

  do {
    /* Skip . and .. */
    if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
      continue;
    }

    /* Build full path to entry */
    char full_path[PLATFORM_MAX_PATH_LENGTH];
    safe_snprintf(full_path, sizeof(full_path), "%s\\%s", path, find_data.cFileName);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      /* Recursively delete subdirectory */
      asciichat_error_t subdir_result = platform_rmdir_recursive(full_path);
      if (subdir_result != ASCIICHAT_OK) {
        result = subdir_result;
      }
    } else {
      /* Delete file */
      if (!DeleteFileA(full_path)) {
        result = ERROR_FILE_OPERATION;
      }
    }
  } while (FindNextFileA(find_handle, &find_data));

  FindClose(find_handle);

  /* Delete the directory itself */
  if (RemoveDirectoryA(path)) {
    return result;
  }

  return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to delete directory: %s", path);
}
