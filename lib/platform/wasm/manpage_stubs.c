/**
 * @file platform/wasm/manpage_stubs.c
 * @brief Manpage resource stubs for WASM (not needed for mirror mode)
 */

#include <stdio.h>
#include <stddef.h>

// Manpage template stubs - not needed in WASM
const char *get_manpage_template(void) {
  return ""; // Empty template - man pages not supported in WASM
}

void release_manpage_resources(FILE *file) {
  if (file) {
    fclose(file);
  }
}
