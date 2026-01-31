/**
 * @file platform/windows/keepawake.c
 * @brief Windows system sleep prevention using SetThreadExecutionState
 * @ingroup platform
 */

#include "../system.h"
#include "../../log/logging.h"
#include "../../asciichat_errno.h"
#include <windows.h>

asciichat_error_t platform_enable_keepawake(void) {
  // Windows: Use SetThreadExecutionState
  EXECUTION_STATE state = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

  if (state == 0) {
    DWORD error = GetLastError();
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "SetThreadExecutionState failed (error %lu)", error);
  }

  log_debug("Keepawake enabled via SetThreadExecutionState");
  return ASCIICHAT_OK;
}

void platform_disable_keepawake(void) {
  // Clear all execution state flags (allow sleep)
  SetThreadExecutionState(ES_CONTINUOUS);
  log_debug("Keepawake disabled");
}
