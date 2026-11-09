/** @file platform/systemd.h @brief Optional systemd service notifications. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool platform_systemd_is_managed(void);
void platform_systemd_notify_ready(void);
void platform_systemd_notify_status(const char *status);
void platform_systemd_notify_reloading(void);
void platform_systemd_notify_watchdog(void);
bool platform_systemd_watchdog_enabled(unsigned long long *usec);
void platform_systemd_journal_log(int level, const char *message, const char *file, int line, const char *function);
