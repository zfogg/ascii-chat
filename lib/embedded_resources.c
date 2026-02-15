/**
 * @file embedded_resources.c
 * @brief Embedded resource access implementation
 * @ingroup common
 *
 * Provides runtime access to embedded or filesystem-based resources
 * based on build configuration.
 */

#include <ascii-chat/embedded_resources.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/util.h>
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
// CMakeLists.txt custom commands), but are only used in Release builds (NDEBUG defined)
//
extern const char embedded_manpage_template[];
extern const size_t embedded_manpage_template_len;

// =============================================================================
// Resource Access Implementation
// =============================================================================

int get_manpage_template(FILE **out_file, const char **out_content, size_t *out_len) {
#ifdef NDEBUG
  // Release build: Return embedded data
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
  // Debug build: Read from filesystem
  if (!out_file) {
    SET_ERRNO(ERROR_INVALID_PARAM, "out_file cannot be NULL in development mode");
    return -1;
  }

  // Construct absolute path using ASCIICHAT_RESOURCE_DIR (set at build time)
#ifdef ASCIICHAT_RESOURCE_DIR
  static char path_buffer[PATH_MAX];
  safe_snprintf(path_buffer, sizeof(path_buffer), "%s/share/man/man1/ascii-chat.1.in", ASCIICHAT_RESOURCE_DIR);
  const char *path = path_buffer;
#else
  // Fallback: try relative path if ASCIICHAT_RESOURCE_DIR not defined
  const char *path = "share/man/man1/ascii-chat.1.in";
#endif

  // Use binary mode to ensure fread byte count matches ftell file size.
  // Text mode on Windows translates \r\n to \n, causing size mismatch.
  FILE *f = platform_fopen(path, "rb");
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
  // Content is now consolidated into the template - return empty
  if (out_content) {
    *out_content = "";
  }
  if (out_len) {
    *out_len = 0;
  }
  if (out_file) {
    *out_file = NULL;
  }

  log_debug("Man page content is now consolidated into template (empty)");
  return 0;
}

void release_manpage_resources(FILE *file) {
#ifdef NDEBUG
  // Release build: No-op (embedded data is static, not heap-allocated)
  (void)file; // Suppress unused parameter warning
#else
  // Debug build: Close file handle if present
  if (file) {
    fclose(file);
  }
#endif
}
