/**
 * @file log/raylib.c
 * @brief Raylib logging integration
 * @ingroup logging
 *
 * Separate logging callback for raylib to prevent interference with our logging system.
 */

#ifdef __EMSCRIPTEN__
#include <stdio.h>

/* Forward declare only the function we need, without including raylib.h
 * This avoids enum conflicts between raylib's LOG_* and ours */
void SetTraceLogCallback(void (*callback)(int, const char*));

static void raylib_log_callback(int logLevel, const char *text) {
  (void)logLevel;
  // Send raylib logs directly to console, bypassing our logging system
  fprintf(stderr, "[raylib] %s", text);
}

void log_init_raylib(void) {
  SetTraceLogCallback(raylib_log_callback);
}
#else
void log_init_raylib(void) {
  // No-op on non-WASM platforms
}
#endif
