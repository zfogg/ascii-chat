/**
 * @file globals.c
 * @brief Global symbol definitions for test executables
 *
 * Test executables link against ascii-chat-lib which references certain global
 * symbols that are normally defined in src/server/main.c or src/client/main.c.
 * Since tests don't link against those files, we need to provide stub definitions
 * here to satisfy the linker.
 */

#include <stdatomic.h>
#include <stdbool.h>

/**
 * Global shutdown flag referenced by lib/logging.c and lib/lock_debug.c
 * In production builds, this is defined in main.c and set during shutdown.
 * For tests, we stub it out as false (tests don't perform shutdown).
 */
atomic_bool g_should_exit = false;
