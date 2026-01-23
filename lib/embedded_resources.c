/**
 * @file embedded_resources.c
 * @brief Embedded resource access implementation
 * @ingroup common
 *
 * Provides runtime access to embedded or filesystem-based resources
 * based on build configuration.
 */

#include "embedded_resources.h"
#include "common.h"
#include "log/logging.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

// =============================================================================
// External Declarations for Embedded Data
// =============================================================================
//
// These symbols are defined in auto-generated C files (generated at build time
// by cmake/utils/EmbedTextFile.cmake). They always exist (generated from
// CMakeLists.txt custom commands), but are only used when USE_EMBEDDED_RESOURCES=1
//
extern const char embedded_manpage_template[];
extern const size_t embedded_manpage_template_len;
extern const char embedded_manpage_content[];
extern const size_t embedded_manpage_content_len;

// =============================================================================
// Resource Access Implementation
// =============================================================================

int get_manpage_template(FILE **out_file, const char **out_content, size_t *out_len) {
#if USE_EMBEDDED_RESOURCES
  // Production build: Return embedded data
  if (out_content) {
    *out_content = embedded_manpage_template;
  }
  if (out_len) {
    *out_len = embedded_manpage_template_len;
  }
  if (out_file) {
    *out_file = NULL;
  }

  log_debug("Using embedded man page template (%zu bytes)", embedded_manpage_template_len);
  return 0;
#else
  // Development build: Read from filesystem
  if (!out_file) {
    SET_ERRNO(ERROR_INVALID_PARAM, "out_file cannot be NULL in development mode");
    return -1;
  }

  // Construct absolute path using ASCIICHAT_RESOURCE_DIR (set at build time)
#ifdef ASCIICHAT_RESOURCE_DIR
  static char path_buffer[PATH_MAX];
  snprintf(path_buffer, sizeof(path_buffer), "%s/share/man/man1/ascii-chat.1.in", ASCIICHAT_RESOURCE_DIR);
  const char *path = path_buffer;
#else
  // Fallback: try relative path if ASCIICHAT_RESOURCE_DIR not defined
  const char *path = "share/man/man1/ascii-chat.1.in";
#endif

  FILE *f = fopen(path, "r");
  if (!f) {
    SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open man page template: %s", path);
    return -1;
  }

  *out_file = f;
  if (out_content) {
    *out_content = NULL;
  }
  if (out_len) {
    *out_len = 0;
  }

  log_debug("Using filesystem man page template: %s", path);
  return 0;
#endif
}

int get_manpage_content(FILE **out_file, const char **out_content, size_t *out_len) {
#if USE_EMBEDDED_RESOURCES
  // Production build: Return embedded data
  if (out_content) {
    *out_content = embedded_manpage_content;
  }
  if (out_len) {
    *out_len = embedded_manpage_content_len;
  }
  if (out_file) {
    *out_file = NULL;
  }

  log_debug("Using embedded man page content (%zu bytes)", embedded_manpage_content_len);
  return 0;
#else
  // Development build: Read from filesystem
  if (!out_file) {
    SET_ERRNO(ERROR_INVALID_PARAM, "out_file cannot be NULL in development mode");
    return -1;
  }

  // Construct absolute path using ASCIICHAT_RESOURCE_DIR (set at build time)
#ifdef ASCIICHAT_RESOURCE_DIR
  static char path_buffer[PATH_MAX];
  snprintf(path_buffer, sizeof(path_buffer), "%s/share/man/man1/ascii-chat.1.content", ASCIICHAT_RESOURCE_DIR);
  const char *path = path_buffer;
#else
  // Fallback: try relative path if ASCIICHAT_RESOURCE_DIR not defined
  const char *path = "share/man/man1/ascii-chat.1.content";
#endif

  FILE *f = fopen(path, "r");
  if (!f) {
    SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open man page content: %s", path);
    return -1;
  }

  *out_file = f;
  if (out_content) {
    *out_content = NULL;
  }
  if (out_len) {
    *out_len = 0;
  }

  log_debug("Using filesystem man page content: %s", path);
  return 0;
#endif
}

void release_manpage_resources(FILE *file) {
#if USE_EMBEDDED_RESOURCES
  // Production build: No-op (embedded data is static, not heap-allocated)
  (void)file; // Suppress unused parameter warning
#else
  // Development build: Close file handle if present
  if (file) {
    fclose(file);
  }
#endif
}
