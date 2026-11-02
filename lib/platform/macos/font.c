/**
 * @file platform/macos/font.c
 * @ingroup platform
 * @brief Font resolution for macOS: file paths, bundled fonts, and unresolved name passthrough
 */
#ifdef __APPLE__
#include <ascii-chat/platform/font.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/font.h>
#include <ascii-chat/log/log.h>
#include <string.h>
#include <sys/stat.h>

static bool is_absolute_path(const char *s) {
  return s && s[0] == '/';
}
static bool file_exists(const char *p) {
  struct stat st;
  return stat(p, &st) == 0;
}

asciichat_error_t platform_font_resolve(const char *spec, char *out, size_t out_size, bool *out_is_path,
                                        const uint8_t **out_font_data, size_t *out_font_data_size) {
  // If no font is specified, use the bundled default font (DejaVu Sans Mono) for render-file output.
  // This ensures render-file always has a suitable fallback font available.
  const char *eff = (spec && spec[0]) ? spec : "default";

  if (out_font_data)
    *out_font_data = NULL;
  if (out_font_data_size)
    *out_font_data_size = 0;

  if (is_absolute_path(eff)) {
    snprintf(out, out_size, "%s", eff);
    *out_is_path = true;
    return file_exists(out) ? ASCIICHAT_OK : SET_ERRNO(ERROR_NOT_FOUND, "render-font: not found: %s", out);
  }

  // Check for bundled font names ("matrix" or "default")
  if (strcmp(eff, "matrix") == 0) {
    if (out_font_data)
      *out_font_data = g_font_matrix_resurrected;
    if (out_font_data_size)
      *out_font_data_size = g_font_matrix_resurrected_size;
    // Also provide a temporary file path for the bundled font.
    static char tmp_path[PLATFORM_MAX_PATH_LENGTH] = {0};
    if (tmp_path[0] == '\0' && out_font_data && *out_font_data) {
      strlcpy(tmp_path, "/tmp/ascii-chat-matrix-XXXXXX.ttf", sizeof(tmp_path));
      int fd = mkstemps(tmp_path, 4); // 4 = ".ttf" suffix
      if (fd >= 0) {
        write(fd, *out_font_data, *out_font_data_size);
        close(fd);
        log_debug("wrote bundled matrix font to %s", tmp_path);
      }
    }
    if (tmp_path[0] != '\0') {
      snprintf(out, out_size, "%s", tmp_path);
      *out_is_path = true;
    }
    return ASCIICHAT_OK;
  }

  if (strcmp(eff, "default") == 0) {
    if (out_font_data)
      *out_font_data = g_font_default;
    if (out_font_data_size)
      *out_font_data_size = g_font_default_size;
    // Also provide a temporary file path for the bundled font.
    static char tmp_path_default[PLATFORM_MAX_PATH_LENGTH] = {0};
    if (tmp_path_default[0] == '\0' && out_font_data && *out_font_data) {
      strlcpy(tmp_path_default, "/tmp/ascii-chat-default-XXXXXX.ttf", sizeof(tmp_path_default));
      int fd = mkstemps(tmp_path_default, 4); // 4 = ".ttf" suffix
      if (fd >= 0) {
        write(fd, *out_font_data, *out_font_data_size);
        close(fd);
        log_debug("wrote bundled default font to %s", tmp_path_default);
      }
    }
    if (tmp_path_default[0] != '\0') {
      snprintf(out, out_size, "%s", tmp_path_default);
      *out_is_path = true;
    }
    return ASCIICHAT_OK;
  }

  // Pass other specifications through unchanged; this does not resolve family names.
  // The FreeType renderer treats this string as a file path and reports load failures.
  snprintf(out, out_size, "%s", eff);
  *out_is_path = false;

  log_debug("passing unresolved font specification '%s' to the FreeType renderer", eff);
  return ASCIICHAT_OK;
}
#endif // __APPLE__
