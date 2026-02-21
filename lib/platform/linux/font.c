/**
 * @file platform/linux/font.c
 * @ingroup platform
 * @brief Font resolution for Linux: fontconfig name → absolute .ttf path
 */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <ascii-chat/platform/font.h>
#include <ascii-chat/font.h>
#include <ascii-chat/log/logging.h>
#include <fontconfig/fontconfig.h>
#include <string.h>
#include <sys/stat.h>

// Platform-guaranteed defaults (always present on a stock Linux install).
// "monospace" is a fontconfig generic alias — always resolves to the distro's
// preferred monospace. DejaVu Sans Mono ships with the vast majority of distros.
static const char *k_default  = "monospace";
static const char *k_fallback = "DejaVu Sans Mono";

static bool is_absolute_path(const char *s) { return s && s[0] == '/'; }
static bool file_exists(const char *p)       { struct stat st; return stat(p, &st) == 0; }

static asciichat_error_t resolve_via_fontconfig(const char *name,
                                                char *out, size_t sz) {
    FcConfig  *fc  = FcInitLoadConfigAndFonts();
    FcPattern *pat = FcNameParse((const FcChar8 *)name);
    if (!pat)
        return SET_ERRNO(ERROR_INVALID_PARAM,
                         "platform_font_resolve: bad name '%s'", name);

    FcConfigSubstitute(fc, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult   result;
    FcPattern *match = FcFontMatch(fc, pat, &result);
    FcPatternDestroy(pat);

    if (!match || result != FcResultMatch)
        return SET_ERRNO(ERROR_NOT_FOUND,
                         "platform_font_resolve: no font matching '%s'", name);

    FcChar8 *file = NULL;
    if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch || !file) {
        FcPatternDestroy(match);
        return SET_ERRNO(ERROR_NOT_FOUND,
                         "platform_font_resolve: fontconfig matched but no file");
    }
    snprintf(out, sz, "%s", (const char *)file);
    log_debug("platform_font_resolve: '%s' → %s", name, out);
    FcPatternDestroy(match);
    return ASCIICHAT_OK;
}

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
        out[0] = '\0';
        *out_is_path = false;
        log_debug("platform_font_resolve: using bundled matrix font");
        return ASCIICHAT_OK;
    }

    *out_is_path = true;
    asciichat_error_t err = resolve_via_fontconfig(eff, out, out_size);
    if (err != ASCIICHAT_OK && strcmp(eff, k_fallback) != 0) {
        log_warn("platform_font_resolve: '%s' not found, trying '%s'", eff, k_fallback);
        err = resolve_via_fontconfig(k_fallback, out, out_size);
    }

    // Final fallback: bundled matrix font
    if (err != ASCIICHAT_OK) {
        log_warn("platform_font_resolve: system font resolution failed, using bundled matrix font");
        if (out_font_data) *out_font_data = g_font_matrix_resurrected;
        if (out_font_data_size) *out_font_data_size = g_font_matrix_resurrected_size;
        out[0] = '\0';
        *out_is_path = false;
        err = ASCIICHAT_OK;
    }

    return err;
}
#endif
