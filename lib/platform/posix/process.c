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
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/io.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/resource.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
asciichat_error_t platform_process_stats(platform_process_stats_t *out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing process stats output");
  *out = (platform_process_stats_t){0};
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage))
    return SET_ERRNO_SYS(ERROR_PROCESS_FAILED, "Cannot query process CPU time");
  out->cpu_seconds = (double)usage.ru_utime.tv_sec + (double)usage.ru_stime.tv_sec +
                     ((double)usage.ru_utime.tv_usec + (double)usage.ru_stime.tv_usec) / 1e6;
#ifdef __APPLE__
  struct task_basic_info info;
  mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS)
    out->resident_bytes = info.resident_size;
  thread_act_array_t threads;
  mach_msg_type_number_t n;
  if (task_threads(mach_task_self(), &threads, &n) == KERN_SUCCESS) {
    out->threads = n;
    for (mach_msg_type_number_t i = 0; i < n; ++i)
      mach_port_deallocate(mach_task_self(), threads[i]);
    vm_deallocate(mach_task_self(), (vm_address_t)threads, n * sizeof(*threads));
  }
#elif defined(__linux__)
  FILE *status = fopen("/proc/self/status", "r");
  if (status) {
    char line[256];
    unsigned long long value;
    while (fgets(line, sizeof(line), status)) {
      if (sscanf(line, "VmRSS: %llu", &value) == 1)
        out->resident_bytes = value * 1024;
      if (sscanf(line, "Threads: %llu", &value) == 1)
        out->threads = value;
    }
    fclose(status);
  }
#endif
  return ASCIICHAT_OK;
}

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
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    return ERROR_INVALID_PARAM;
  }

  FILE *stream = NULL;
  LOG_IO(name, {
    stream = popen(command, mode); // NOLINT(bugprone-command-processor) - intentional shell usage
  });
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
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid stream pointer");
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
