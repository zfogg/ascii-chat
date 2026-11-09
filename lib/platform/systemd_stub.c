#include <ascii-chat/platform/systemd.h>

/** @brief Return whether systemd service-manager integration is available. */
bool platform_systemd_is_managed(void) {
  return false;
}
/** @brief Notify systemd that service initialization completed. */
void platform_systemd_notify_ready(void) {}
/** @brief Publish a human-readable service status to systemd. */
void platform_systemd_notify_status(const char *status) {
  (void)status;
}
/** @brief Notify systemd that a reload transition has started. */
void platform_systemd_notify_reloading(void) {}
/** @brief Send a watchdog keepalive when systemd configured one. */
void platform_systemd_notify_watchdog(void) {}
/** @brief Read the configured watchdog interval in microseconds. */
bool platform_systemd_watchdog_enabled(unsigned long long *usec) {
  (void)usec;
  return false;
}
