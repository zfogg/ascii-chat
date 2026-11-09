#include <ascii-chat/platform/systemd.h>

bool platform_systemd_is_managed(void) { return false; }
void platform_systemd_notify_ready(void) {}
void platform_systemd_notify_status(const char *status) { (void)status; }
void platform_systemd_notify_reloading(void) {}
void platform_systemd_notify_watchdog(void) {}
bool platform_systemd_watchdog_enabled(unsigned long long *usec) {
  (void)usec;
  return false;
}
