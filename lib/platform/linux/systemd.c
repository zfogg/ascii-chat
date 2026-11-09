#include <ascii-chat/platform/systemd.h>
#include <stdio.h>
#include <stdint.h>
#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#include <stdlib.h>
extern int sd_booted(void) __attribute__((weak));
extern int sd_notify(int unset_environment, const char *state) __attribute__((weak));
extern int sd_watchdog_enabled(int unset_environment, uint64_t *usec) __attribute__((weak));
#endif
bool platform_systemd_is_managed(void) {
#ifdef HAVE_LIBSYSTEMD
  return sd_booted != NULL && sd_notify != NULL && getenv("NOTIFY_SOCKET") != NULL;
#else
  return false;
#endif
}
void platform_systemd_notify_ready(void) {
#ifdef HAVE_LIBSYSTEMD
  if (platform_systemd_is_managed()) (void)sd_notify(0, "READY=1");
#endif
}
void platform_systemd_notify_status(const char *status) {
#ifdef HAVE_LIBSYSTEMD
  if (platform_systemd_is_managed() && status) { char message[512]; (void)snprintf(message, sizeof(message), "STATUS=%s", status); (void)sd_notify(0, message); }
#else
  (void)status;
#endif
}
void platform_systemd_notify_reloading(void) {
#ifdef HAVE_LIBSYSTEMD
  if (platform_systemd_is_managed()) (void)sd_notify(0, "RELOADING=1");
#endif
}
void platform_systemd_notify_watchdog(void) {
#ifdef HAVE_LIBSYSTEMD
  if (platform_systemd_is_managed() && sd_watchdog_enabled != NULL) {
    uint64_t usec = 0;
    if (sd_watchdog_enabled(0, &usec) > 0) (void)sd_notify(0, "WATCHDOG=1");
  }
#endif
}
bool platform_systemd_watchdog_enabled(unsigned long long *usec) {
#ifdef HAVE_LIBSYSTEMD
  uint64_t value = 0;
  if (!usec || sd_watchdog_enabled == NULL || sd_watchdog_enabled(0, &value) <= 0) return false;
  *usec = (unsigned long long)value; return true;
#else
  (void)usec; return false;
#endif
}
