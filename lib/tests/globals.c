/**
 * @file tests/globals.c
 * @ingroup tests
 * @brief 🔗 Global symbol stubs for test executables to satisfy linker dependencies
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
 * For tests, we define them here as weak symbols so they can be overridden by options.c
 * if it's linked, but still provide defaults if options.c isn't linked.
 */
__attribute__((weak)) terminal_color_mode_t opt_color_mode = COLOR_MODE_AUTO;
__attribute__((weak)) render_mode_t opt_render_mode = RENDER_MODE_FOREGROUND;
