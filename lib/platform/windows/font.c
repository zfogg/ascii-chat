/**
 * @file platform/windows/font.c
 * @brief Windows font resolution for render-file output
 */
#ifdef _WIN32

#include <ascii-chat/platform/font.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

// Default monospace fonts to try, in order of preference
static const char *g_default_fonts[] = {
    "consola.ttf",      // Consolas
    "cour.ttf",         // Courier New
    "lucon.ttf",        // Lucida Console
    NULL
};

asciichat_error_t platform_font_resolve(const char *spec,
                                        char *out, size_t out_size,
                                        bool *out_is_path,
                                        const uint8_t **out_font_data,
                                        size_t *out_font_data_size) {
    if (!out || out_size == 0 || !out_is_path || !out_font_data || !out_font_data_size) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    }

    *out_font_data = NULL;
    *out_font_data_size = 0;
    *out_is_path = true;

    char fonts_dir[MAX_PATH];
    UINT len = GetWindowsDirectoryA(fonts_dir, sizeof(fonts_dir));
    if (len == 0 || len >= sizeof(fonts_dir) - 8) {
        return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to get Windows directory");
    }
    strcat(fonts_dir, "\\Fonts\\");

    // If spec is an absolute path, use it directly
    if (spec && spec[0] != '\0') {
        if ((spec[0] == '/' || spec[0] == '\\') ||
            (strlen(spec) > 2 && spec[1] == ':')) {
            snprintf(out, out_size, "%s", spec);
            return ASCIICHAT_OK;
        }

        // Try spec as a font filename in Windows\Fonts
        char candidate[MAX_PATH];
        snprintf(candidate, sizeof(candidate), "%s%s", fonts_dir, spec);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            snprintf(out, out_size, "%s", candidate);
            return ASCIICHAT_OK;
        }

        // Try with .ttf extension
        snprintf(candidate, sizeof(candidate), "%s%s.ttf", fonts_dir, spec);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            snprintf(out, out_size, "%s", candidate);
            return ASCIICHAT_OK;
        }
    }

    // Fall back to default monospace fonts
    for (int i = 0; g_default_fonts[i] != NULL; i++) {
        char candidate[MAX_PATH];
        snprintf(candidate, sizeof(candidate), "%s%s", fonts_dir, g_default_fonts[i]);
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
            snprintf(out, out_size, "%s", candidate);
            return ASCIICHAT_OK;
        }
    }

    return SET_ERRNO(ERROR_NOT_SUPPORTED,
                     "No suitable monospace font found in %s", fonts_dir);
}
#endif
