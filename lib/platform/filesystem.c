/**
 * @file filesystem.c
 * @brief Cross-platform filesystem utilities and error reporting
 */

#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/util.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

// Thread-local storage for error messages (avoids allocation)
static _Thread_local char error_msg_buffer[512];

const char *file_read_error_message(const char *path) {
  switch (errno) {
  case ENOENT:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "File does not exist: %s", path);
    break;
  case EACCES:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Permission denied (cannot read): %s", path);
    break;
  case EISDIR:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Is a directory, not a file: %s", path);
    break;
  default:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Failed to open for reading: %s (%s)", path, platform_strerror(errno));
    break;
  }
  return error_msg_buffer;
}

const char *file_write_error_message(const char *path) {
  switch (errno) {
  case ENOENT:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Directory does not exist: %s", path);
    break;
  case EACCES:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Permission denied (cannot write): %s", path);
    break;
  case EROFS:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Read-only filesystem: %s", path);
    break;
  case ENOSPC:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "No space left on device: %s", path);
    break;
  case EISDIR:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Is a directory, not a file: %s", path);
    break;
  default:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Failed to open for writing: %s (%s)", path, platform_strerror(errno));
    break;
  }
  return error_msg_buffer;
}
