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
void platform_sleep_us(unsigned int us);

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

// Signal constants for cross-platform use
#ifdef _WIN32
// Windows doesn't have these signals
#define SIGWINCH 28 // Window size change (not supported on Windows)
#define SIGTERM 15  // Termination signal (limited support on Windows)
#endif

// Debug/stack trace functions
int platform_backtrace(void **buffer, int size);
char **platform_backtrace_symbols(void *const *buffer, int size);
void platform_backtrace_symbols_free(char **strings);
