/**
 * @file platform/windows/pipe.c
 * @ingroup platform
 * @brief 🔌 Windows pipe/agent implementation using named pipes
 */

#ifdef _WIN32

#include <windows.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "common.h"
#include "platform/pipe.h"

pipe_t platform_pipe_connect(const char *path) {
  if (!path) {
    return INVALID_PIPE_VALUE;
  }

  // On Windows, path is the named pipe path (e.g., "\\\\.\\pipe\\openssh-ssh-agent")
  HANDLE pipe = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

  if (pipe != INVALID_HANDLE_VALUE) {
    log_debug("Connected to agent via Windows named pipe: %s", path);
  } else {
    log_debug("Failed to connect to named pipe %s: error %lu", path, GetLastError());
  }

  return pipe;
}

int platform_pipe_close(pipe_t pipe) {
  if (pipe == INVALID_PIPE_VALUE) {
    return 0;
  }

  if (CloseHandle(pipe)) {
    return 0;
  } else {
    log_debug("Failed to close pipe: error %lu", GetLastError());
    return -1;
  }
}

ssize_t platform_pipe_read(pipe_t pipe, void *buf, size_t len) {
  if (pipe == INVALID_PIPE_VALUE || !buf || len == 0) {
    return -1;
  }

  // On Windows, DWORD is 32-bit, so limit to UINT32_MAX
  if (len > (size_t)UINT32_MAX) {
    len = (size_t)UINT32_MAX;
  }

  DWORD bytes_read = 0;
  if (!ReadFile(pipe, buf, (DWORD)len, &bytes_read, NULL)) {
    DWORD error = GetLastError();
    if (error != ERROR_MORE_DATA && error != ERROR_BROKEN_PIPE) {
      log_debug("Failed to read from pipe: error %lu", error);
    }
    return -1;
  }

  return (ssize_t)bytes_read;
}

ssize_t platform_pipe_write(pipe_t pipe, const void *buf, size_t len) {
  if (pipe == INVALID_PIPE_VALUE || !buf || len == 0) {
    return -1;
  }

  // On Windows, DWORD is 32-bit, so limit to UINT32_MAX
  if (len > (size_t)UINT32_MAX) {
    len = (size_t)UINT32_MAX;
  }

  DWORD bytes_written = 0;
  if (!WriteFile(pipe, buf, (DWORD)len, &bytes_written, NULL)) {
    log_debug("Failed to write to pipe: error %lu", GetLastError());
    return -1;
  }

  return (ssize_t)bytes_written;
}

bool platform_pipe_is_valid(pipe_t pipe) {
  return pipe != INVALID_PIPE_VALUE;
}

#endif // _WIN32
