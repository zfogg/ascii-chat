/**
 * @file platform/posix/tempfile.c
 * @brief POSIX temporary file creation implementation
 */

#include "../tempfile.h"
#include "../../common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

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
