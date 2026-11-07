/**
 * @file platform/windows/process.c
 * @ingroup platform
 * @brief Windows process execution utilities
 */

#ifdef _WIN32

#include <ascii-chat/platform/process.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/io.h>
#include <stdlib.h>
#include <errno.h>
#include <windows.h>
#include <shellapi.h>
#include <psapi.h>
#include <tlhelp32.h>
// Windows passes a UTF-16 command line, while the narrow CRT argv uses the
// system code page. Keep UTF-8 arguments alive for the lifetime of the app.
static char windows_arg_data[131072];
static char *windows_args[32768];

asciichat_error_t process_utf8_arguments(int *argc, char ***argv) {
  int count = 0;
  wchar_t **wide_args = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!wide_args || count < 1 || count >= 32768) {
    if (wide_args)
      LocalFree(wide_args);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Could not convert Windows command line to UTF-8");
  }
  size_t used = 0;
  for (int i = 0; i < count; i++) {
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_args[i], -1, NULL, 0, NULL, NULL);
    if (length <= 0 || (size_t)length > sizeof(windows_arg_data) - used) {
      LocalFree(wide_args);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Could not convert Windows command line to UTF-8");
    }
    windows_args[i] = windows_arg_data + used;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_args[i], -1, windows_args[i], length, NULL, NULL)) {
      LocalFree(wide_args);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Could not convert Windows command line to UTF-8");
    }
    used += (size_t)length;
  }
  LocalFree(wide_args);
  windows_args[count] = NULL;
  *argc = count;
  *argv = windows_args;
  return ASCIICHAT_OK;
}

asciichat_error_t platform_process_stats(platform_process_stats_t *out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing process stats output");
  *out = (platform_process_stats_t){0};
  FILETIME created, exited, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    return SET_ERRNO(ERROR_PROCESS_FAILED, "Cannot query process CPU time");
  ULARGE_INTEGER k = {.LowPart = kernel.dwLowDateTime, .HighPart = kernel.dwHighDateTime};
  ULARGE_INTEGER u = {.LowPart = user.dwLowDateTime, .HighPart = user.dwHighDateTime};
  out->cpu_seconds = (double)(k.QuadPart + u.QuadPart) / 1e7;
  PROCESS_MEMORY_COUNTERS memory = {.cb = sizeof(memory)};
  if (K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
    out->resident_bytes = memory.WorkingSetSize;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot != INVALID_HANDLE_VALUE) {
    THREADENTRY32 entry = {.dwSize = sizeof(entry)};
    if (Thread32First(snapshot, &entry))
      do {
        if (entry.th32OwnerProcessID == GetCurrentProcessId())
          ++out->threads;
      } while (Thread32Next(snapshot, &entry));
    CloseHandle(snapshot);
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Get current process ID (Windows implementation)
 * @return Process ID as pid_t
 */
pid_t platform_get_pid(void) {
  return (pid_t)GetCurrentProcessId();
}

/**
 * @brief Execute a command and return a file stream (Windows implementation)
 */
asciichat_error_t platform_popen(const char *name, const char *command, const char *mode, FILE **out_stream) {
  if (!name || !command || !mode || !out_stream) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    return ERROR_INVALID_PARAM;
  }

  FILE *stream = NULL;
  LOG_IO(name, { stream = _popen(command, mode); });
  if (!stream) {
    return SET_ERRNO_SYS(ERROR_PROCESS_FAILED, "Failed to execute command: %s", command);
  }

  int fd = _fileno(stream);
  if (fd >= 0) {
    NAMED_REGISTER_FD(fd, name);
  }

  *out_stream = stream;
  return ASCIICHAT_OK;
}

/**
 * @brief Close a process stream (Windows implementation)
 */
asciichat_error_t platform_pclose(FILE **stream_ptr) {
  if (!stream_ptr || !*stream_ptr) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid stream pointer");
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
