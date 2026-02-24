/**
 * @file platform/posix/process.c
 * @ingroup platform
 * @brief POSIX process execution utilities
 */

#ifndef _WIN32

#include <ascii-chat/platform/process.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

/**
 * @brief Get current process ID (POSIX implementation)
 * @return Process ID as pid_t
 */
pid_t platform_get_pid(void) {
  return getpid();
}

/**
 * @brief Execute a command and return a file stream (POSIX implementation)
 */
asciichat_error_t platform_popen(const char *name, const char *command, const char *mode, FILE **out_stream) {
  if (!name || !command || !mode || !out_stream) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to platform_popen");
    return ERROR_INVALID_PARAM;
  }

  FILE *stream = popen(command, mode);
  if (!stream) {
    return SET_ERRNO_SYS(ERROR_PROCESS_FAILED, "Failed to execute command: %s", command);
  }

  int fd = fileno(stream);
  if (fd >= 0) {
    NAMED_REGISTER_FD(fd, name);
    log_dev("Opened process pipe with file descriptor %d for %s: %s", fd, name, command);
  }

  *out_stream = stream;
  return ASCIICHAT_OK;
}

/**
 * @brief Close a process stream (POSIX implementation)
 */
asciichat_error_t platform_pclose(FILE **stream_ptr) {
  if (!stream_ptr || !*stream_ptr) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid stream pointer to platform_pclose");
    return ERROR_INVALID_PARAM;
  }

  int status = pclose(*stream_ptr);
  *stream_ptr = NULL;

  if (status == -1) {
    return SET_ERRNO_SYS(ERROR_PROCESS_FAILED, "Failed to close process stream");
  }

  return ASCIICHAT_OK;
}

#endif
