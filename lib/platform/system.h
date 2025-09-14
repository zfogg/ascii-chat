#pragma once

/**
 * @file system.h
 * @brief Cross-platform system functions interface for ASCII-Chat
 *
 * This header provides unified system functions including process management,
 * environment variables, TTY operations, and signal handling.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <signal.h>

// Signal handler type
typedef void (*signal_handler_t)(int);

// ============================================================================
// System Functions
// ============================================================================

// Platform initialization
int platform_init(void);
void platform_cleanup(void);

// Time functions
void platform_sleep_ms(unsigned int ms);

// Process functions
int platform_get_pid(void);
const char *platform_get_username(void);

// Signal handling
signal_handler_t platform_signal(int sig, signal_handler_t handler);

// Environment variables
const char *platform_getenv(const char *name);
int platform_setenv(const char *name, const char *value);

// TTY functions
int platform_isatty(int fd);
const char *platform_ttyname(int fd);
int platform_fsync(int fd);

// Debug/stack trace functions
int platform_backtrace(void **buffer, int size);
char **platform_backtrace_symbols(void *const *buffer, int size);
void platform_backtrace_symbols_free(char **strings);

// Crash handling
void platform_install_crash_handler(void);
void platform_print_backtrace(void);
