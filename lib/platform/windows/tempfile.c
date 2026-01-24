/**
 * @file platform/windows/tempfile.c
 * @brief Windows temporary file creation implementation
 */

#include "../tempfile.h"
#include "../../common.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

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
