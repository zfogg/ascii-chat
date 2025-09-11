#ifndef PLATFORM_INTERNAL_H
#define PLATFORM_INTERNAL_H

/**
 * @file internal.h
 * @brief Internal platform abstraction layer function declarations
 * 
 * This header provides function declarations shared between platform 
 * abstraction components (system.c, terminal.c, etc.) within the same
 * platform implementation.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get TTY device path
 * @return Path to TTY device
 * @note Implemented in terminal.c for each platform
 */
const char *get_tty_path(void);

/**
 * @brief Get username from environment variables
 * @return Username string or "unknown" if not found
 * @note Implemented in system.c for each platform
 */
const char *get_username_env(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_INTERNAL_H */