/**
 * @file platform/wasm/stubs/manpage.c
 * @brief Manpage resource stubs for WASM (not needed for mirror mode)
 * @ingroup platform
 */

#include <stdio.h>
#include <stddef.h>

// Empty embedded resources for WASM
static const char empty_manpage[] = "";
static const size_t empty_manpage_len = 0;

// Stub implementation for get_manpage_template - matches embedded_resources.h signature
int get_manpage_template(FILE **out_file, const char **out_content, size_t *out_len) {
  if (!out_file || !out_content || !out_len) {
    return -1;
  }

  // Return embedded empty string (WASM doesn't support man pages)
  *out_file = NULL;
  *out_content = empty_manpage;
  *out_len = empty_manpage_len;
  return 0;
}

// Stub implementation for get_manpage_content - matches embedded_resources.h signature
int get_manpage_content(FILE **out_file, const char **out_content, size_t *out_len) {
  if (!out_file || !out_content || !out_len) {
    return -1;
  }

  // Return embedded empty string (WASM doesn't support man pages)
  *out_file = NULL;
  *out_content = empty_manpage;
  *out_len = empty_manpage_len;
  return 0;
}

void release_manpage_resources(FILE *file) {
  if (file) {
    fclose(file);
  }
}
