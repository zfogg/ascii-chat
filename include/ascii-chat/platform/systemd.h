/** @file platform/systemd.h @brief Optional systemd service notifications. */
#pragma once
#include <stdbool.h>
bool platform_systemd_is_managed(void);
void platform_systemd_notify_ready(void);
void platform_systemd_notify_status(const char *status);
void platform_systemd_notify_reloading(void);
bool platform_systemd_watchdog_enabled(unsigned long long *usec);
