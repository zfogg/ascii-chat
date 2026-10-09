/** @file platform/linux/process_title.c Linux process comm naming. */
#include "../process_title_internal.h"
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

asciichat_error_t platform_process_title_native_set(const char *title, const char *short_name) {
  (void)title;
  if (syscall(SYS_gettid) != getpid()) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Process title must be set by the main thread");
  }
  // PR_SET_NAME truncates to 15 bytes and changes only the calling thread.
  // On the main thread this also updates /proc/PID/comm.
  if (prctl(PR_SET_NAME, short_name, 0UL, 0UL, 0UL) != 0) {
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to set Linux process comm");
  }
  return ASCIICHAT_OK;
}
