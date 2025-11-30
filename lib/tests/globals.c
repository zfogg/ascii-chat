/**
 * @file tests/globals.c
 * @ingroup testing
 * @brief 🔗 Global symbol stubs for test executables to satisfy linker dependencies
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include "options.h"
#include "platform/system.h"

/**
 * @brief Test environment initialization (runs before main)
 * Sets TESTING environment variable so libraries can detect test mode at runtime.
 * This provides a fallback for when tests are run directly (not via ctest).
 * CTest also sets TESTING=1 via set_tests_properties() in Tests.cmake.
 */
__attribute__((constructor)) static void init_test_environment(void) {
  platform_setenv("TESTING", "1");
}

/**
 * Global shutdown flag referenced by lib/logging.c and lib/debug/lock.c
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
