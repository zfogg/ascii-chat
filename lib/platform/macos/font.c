/**
 * @file platform/macos/font.c
 * @ingroup platform
 * @brief Font resolution for macOS: CoreText family-name passthrough + bundled fallback
 */
#ifdef __APPLE__
#include <ascii-chat/platform/font.h>
#include <ascii-chat/font.h>
#include <ascii-chat/log/logging.h>
#include <string.h>
#include <sys/stat.h>

// Platform-guaranteed defaults (always present on a stock macOS install).
// SF Mono ships with macOS 11+. Menlo has been present since macOS 10.6.
static const char *k_default = "SF Mono";

static bool is_absolute_path(const char *s) { return s && s[0] == '/'; }
static bool file_exists(const char *p)       { struct stat st; return stat(p, &st) == 0; }

asciichat_error_t platform_font_resolve(const char *spec,
                                        char *out, size_t out_size,
                                        bool *out_is_path,
                                        const uint8_t **out_font_data,
                                        size_t *out_font_data_size) {
    const char *eff = (spec && spec[0]) ? spec : k_default;

    if (out_font_data) *out_font_data = NULL;
    if (out_font_data_size) *out_font_data_size = 0;

    if (is_absolute_path(eff)) {
        snprintf(out, out_size, "%s", eff);
        *out_is_path = true;
        return file_exists(out) ? ASCIICHAT_OK
               : SET_ERRNO(ERROR_NOT_FOUND, "render-font: not found: %s", out);
    }

    // Check for bundled font name ("matrix")
    if (strcmp(eff, "matrix") == 0) {
        if (out_font_data) *out_font_data = g_font_matrix_resurrected;
        if (out_font_data_size) *out_font_data_size = g_font_matrix_resurrected_size;
        // For macOS, write bundled font to temp file since ghostty expects paths
        static char tmp_path[4096] = {0};
        if (tmp_path[0] == '\0' && out_font_data && *out_font_data) {
            strlcpy(tmp_path, "/tmp/ascii-chat-matrix-XXXXXX.ttf", sizeof(tmp_path));
            int fd = mkstemps(tmp_path, 4);  // 4 = ".ttf" suffix
            if (fd >= 0) {
                write(fd, *out_font_data, *out_font_data_size);
                close(fd);
                log_debug("platform_font_resolve: wrote bundled matrix font to %s", tmp_path);
            }
        }
        if (tmp_path[0] != '\0') {
            snprintf(out, out_size, "%s", tmp_path);
            *out_is_path = true;
        }
        return ASCIICHAT_OK;
    }

    // Family names are resolved by CoreText inside ghostty — pass them through as-is.
    snprintf(out, out_size, "%s", eff);
    *out_is_path = false;
    return ASCIICHAT_OK;
}
#endif // __APPLE__
