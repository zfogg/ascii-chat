/**
 * @file platform/wasm/environment.c
 * @brief Environment and string utilities for WASM/Emscripten
 */

#include <ascii-chat/platform/abstraction.h>
#include <stdlib.h>
#include <string.h>

const char *platform_getenv(const char *name) {
  if (!name)
    return NULL;
  return getenv(name);
}

const char *platform_strerror(int errnum) {
  return strerror(errnum);
}

int platform_setenv(const char *name, const char *value, int overwrite) {
  (void)name;
  (void)value;
  (void)overwrite;
  return -1; // Not supported in WASM
}

int platform_unsetenv(const char *name) {
  (void)name;
  return -1; // Not supported in WASM
}
