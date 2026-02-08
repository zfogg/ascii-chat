/**
 * @file platform/wasm/system.c
 * @brief System utilities (environment) for WASM/Emscripten
 * @ingroup platform
 */

#include <ascii-chat/platform/abstraction.h>
#include <stdlib.h>
#include <string.h>

const char *platform_getenv(const char *name) {
  (void)name;
  // Environment variables not supported in WASM browser context
  // Calling getenv() causes "memory access out of bounds" errors
  return NULL;
}

const char *platform_strerror(int errnum) {
  return strerror(errnum);
}

int platform_setenv(const char *name, const char *value) {
  (void)name;
  (void)value;
  return -1; // Not supported in WASM
}

int platform_unsetenv(const char *name) {
  (void)name;
  return -1; // Not supported in WASM
}
