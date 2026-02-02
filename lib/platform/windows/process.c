/**
 * @file platform/windows/process.c
 * @ingroup platform
 * @brief Windows process execution utilities
 */

#ifdef _WIN32

#include <ascii-chat/platform/process.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdlib.h>
#include <errno.h>

/**
 * @brief Execute a command and return a file stream (Windows implementation)
 */
asciichat_error_t platform_popen(const char *command, const char *mode, FILE **out_stream) {
  if (!command || !mode || !out_stream) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to platform_popen");
    return ERROR_INVALID_PARAM;
  }

  FILE *stream = _popen(command, mode);
  if (!stream) {
    return SET_ERRNO_SYS(ERROR_PROCESS_FAILED, "Failed to execute command: %s", command);
  }

  *out_stream = stream;
  return ASCIICHAT_OK;
}

/**
 * @brief Close a process stream (Windows implementation)
 */
asciichat_error_t platform_pclose(FILE **stream_ptr) {
  if (!stream_ptr || !*stream_ptr) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid stream pointer to platform_pclose");
    return ERROR_INVALID_PARAM;
  }

  int status = _pclose(*stream_ptr);
  *stream_ptr = NULL;

  if (status == -1) {
    return SET_ERRNO_SYS(ERROR_PROCESS_FAILED, "Failed to close process stream");
  }

  return ASCIICHAT_OK;
}

#endif
