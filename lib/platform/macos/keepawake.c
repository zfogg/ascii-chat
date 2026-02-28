/**
 * @file platform/macos/keepawake.c
 * @brief macOS system sleep prevention using IOKit power assertions
 * @ingroup platform
 */

#include <ascii-chat/platform/system.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/asciichat_errno.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

static IOPMAssertionID g_power_assertion = kIOPMNullAssertionID;

asciichat_error_t platform_enable_keepawake(void) {
  // macOS: Use IOKit power assertions
  if (g_power_assertion != kIOPMNullAssertionID) {
    log_debug("Keepawake already enabled");
    return ASCIICHAT_OK;
  }

  CFStringRef reason = CFSTR("ascii-chat is running");
  IOReturn result = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventSystemSleep, kIOPMAssertionLevelOn, reason,
                                                &g_power_assertion);

  if (result != kIOReturnSuccess) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to create power assertion (error %d)", result);
  }

  log_debug("Keepawake enabled via IOKit (assertion ID: %u)", g_power_assertion);
  return ASCIICHAT_OK;
}

void platform_disable_keepawake(void) {
  if (g_power_assertion != kIOPMNullAssertionID) {
    IOPMAssertionRelease(g_power_assertion);
    log_debug("Keepawake disabled (released assertion ID: %u)", g_power_assertion);
    g_power_assertion = kIOPMNullAssertionID;
  }
}
