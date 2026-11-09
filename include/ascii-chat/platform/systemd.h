/** @file platform/systemd.h @brief Optional systemd service notifications. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(__linux__)
bool platform_systemd_is_managed(void);
void platform_systemd_notify_ready(void);
void platform_systemd_notify_status(const char *status);
void platform_systemd_notify_reloading(void);
void platform_systemd_notify_watchdog(void);
bool platform_systemd_watchdog_enabled(unsigned long long *usec);
void platform_systemd_journal_log(int level, const char *message, const char *file, int line, const char *function);
#else
static inline bool platform_systemd_is_managed(void) {
  return false;
}
static inline void platform_systemd_notify_ready(void) {}
static inline void platform_systemd_notify_status(const char *status) {
  (void)status;
}
static inline void platform_systemd_notify_reloading(void) {}
static inline void platform_systemd_notify_watchdog(void) {}
static inline bool platform_systemd_watchdog_enabled(unsigned long long *usec) {
  (void)usec;
  return false;
}
static inline void platform_systemd_journal_log(int level, const char *message, const char *file, int line,
                                                const char *function) {
  (void)level;
  (void)message;
  (void)file;
  (void)line;
  (void)function;
}
#endif
