#include <ascii-chat/platform/systemd.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#include <stdlib.h>
extern int sd_booted(void) __attribute__((weak));
extern int sd_notify(int unset_environment, const char *state) __attribute__((weak));
extern int sd_watchdog_enabled(int unset_environment, uint64_t *usec) __attribute__((weak));
extern int sd_journal_send(const char *format, ...) __attribute__((weak));
#endif
/** @brief Return whether systemd service-manager integration is available. */
bool platform_systemd_is_managed(void) {
#ifdef HAVE_LIBSYSTEMD
  return sd_booted != NULL && sd_notify != NULL && (getenv("INVOCATION_ID") != NULL || getenv("NOTIFY_SOCKET") != NULL);
#else
  return false;
#endif
}
/** @brief Notify systemd that service initialization completed. */
void platform_systemd_notify_ready(void) {
#ifdef HAVE_LIBSYSTEMD
  if (platform_systemd_is_managed() && sd_notify != NULL) {
    char message[128];
    (void)snprintf(message, sizeof(message), "READY=1\nMAINPID=%ld", (long)getpid());
    (void)sd_notify(0, message);
  }
#endif
}
/** @brief Publish a human-readable service status to systemd. */
void platform_systemd_notify_status(const char *status) {
#ifdef HAVE_LIBSYSTEMD
  if (platform_systemd_is_managed() && status) {
    char message[512];
    (void)snprintf(message, sizeof(message), "STATUS=%s", status);
    (void)sd_notify(0, message);
  }
#else
  (void)status;
#endif
}
/** @brief Notify systemd that a reload transition has started. */
void platform_systemd_notify_reloading(void) {
#ifdef HAVE_LIBSYSTEMD
  if (platform_systemd_is_managed())
    (void)sd_notify(0, "RELOADING=1");
#endif
}
/** @brief Send a watchdog keepalive when systemd configured one. */
void platform_systemd_notify_watchdog(void) {
#ifdef HAVE_LIBSYSTEMD
  if (platform_systemd_is_managed() && sd_watchdog_enabled != NULL) {
    uint64_t usec = 0;
    if (sd_watchdog_enabled(0, &usec) > 0)
      (void)sd_notify(0, "WATCHDOG=1");
  }
#endif
}
/** @brief Read the configured watchdog interval in microseconds. */
bool platform_systemd_watchdog_enabled(unsigned long long *usec) {
#ifdef HAVE_LIBSYSTEMD
  uint64_t value = 0;
  if (!usec || sd_watchdog_enabled == NULL || sd_watchdog_enabled(0, &value) <= 0)
    return false;
  *usec = (unsigned long long)value;
  return true;
#else
  (void)usec;
  return false;
#endif
}
/** @brief Send a structured log record to journald when systemd manages us. */
void platform_systemd_journal_log(int level, const char *message, const char *file, int line, const char *function) {
#ifdef HAVE_LIBSYSTEMD
  if (!platform_systemd_is_managed() || !sd_journal_send || !message)
    return;
  int priority = level >= 5 ? 2 : level >= 4 ? 3 : level >= 3 ? 4 : level >= 2 ? 6 : 7;
  (void)sd_journal_send("MESSAGE=%s", message, "PRIORITY=%d", priority, "CODE_FILE=%s", file ? file : "",
                        "CODE_LINE=%d", line, "CODE_FUNC=%s", function ? function : "", "SYSLOG_IDENTIFIER=%s",
                        "ascii-chat");
#else
  (void)level;
  (void)message;
  (void)file;
  (void)line;
  (void)function;
#endif
}
