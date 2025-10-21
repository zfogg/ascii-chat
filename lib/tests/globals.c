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
#include "options.h"

/**
 * Global shutdown flag referenced by lib/logging.c and lib/lock_debug.c
 * In production builds, this is defined in main.c and set during shutdown.
 * For tests, we stub it out as false (tests don't perform shutdown).
 */
atomic_bool g_should_exit = false;

/**
 * Terminal color mode and render mode globals referenced by terminal.c
 * In production builds, these are defined in options.c and set from command-line options.
 * For tests, we stub them out with defaults (AUTO color, FOREGROUND render).
 */
terminal_color_mode_t opt_color_mode = COLOR_MODE_AUTO;
render_mode_t opt_render_mode = RENDER_MODE_FOREGROUND;
