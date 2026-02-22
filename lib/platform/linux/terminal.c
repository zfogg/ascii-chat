/**
 * @file platform/linux/terminal.c
 * @ingroup platform
 * @brief Pixel renderer for render-file using Ghostty's full terminal emulation
 */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <ft2build.h>
#include FT_FREETYPE_H
#include <string.h>
#include <ascii-chat/video/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/terminal.h>

// Stub terminal type - not used yet
typedef struct {
    uint32_t cols, rows;
} ghostty_terminal_t;

static ghostty_terminal_t *terminal_new(uint32_t cols, uint32_t rows) {
    ghostty_terminal_t *t = SAFE_MALLOC(sizeof(*t), ghostty_terminal_t *);
    t->cols = cols;
    t->rows = rows;
    return t;
}

static void terminal_free(ghostty_terminal_t *term) {
    SAFE_FREE(term);
}

static void terminal_feed(ghostty_terminal_t *term, const uint8_t *data, size_t len) {
    (void)term;
    (void)data;
    (void)len;
    // TODO: Parse ANSI sequences using ghostty's API
}

static void terminal_get_cell(ghostty_terminal_t *term, uint32_t row, uint32_t col,
    uint32_t *codepoint_out, uint8_t *fg_r, uint8_t *fg_g, uint8_t *fg_b,
    uint8_t *bg_r, uint8_t *bg_g, uint8_t *bg_b) {
    (void)term;
    (void)row;
    (void)col;

    // Query actual terminal background color to respect user's theme
    uint8_t term_bg_r = 0, term_bg_g = 0, term_bg_b = 0;
    bool got_bg_color = terminal_query_background_color(&term_bg_r, &term_bg_g, &term_bg_b);

    // Determine if terminal has dark or light background
    bool is_dark = terminal_has_dark_background();

    // Default empty space
    *codepoint_out = ' ';

    // Set background from terminal query if successful
    if (got_bg_color) {
        *bg_r = term_bg_r;
        *bg_g = term_bg_g;
        *bg_b = term_bg_b;
    } else {
        // Fallback based on theme detection
        if (is_dark) {
            *bg_r = 0; *bg_g = 0; *bg_b = 0;  // Black for dark theme
        } else {
            *bg_r = 255; *bg_g = 255; *bg_b = 255;  // White for light theme
        }
    }

    // Set text color with good contrast for detected background theme
    if (is_dark) {
        *fg_r = TERMINAL_COLOR_THEME_DARK_DEFAULT_R;
        *fg_g = TERMINAL_COLOR_THEME_DARK_DEFAULT_G;
        *fg_b = TERMINAL_COLOR_THEME_DARK_DEFAULT_B;
    } else {
        *fg_r = TERMINAL_COLOR_THEME_LIGHT_DEFAULT_R;
        *fg_g = TERMINAL_COLOR_THEME_LIGHT_DEFAULT_G;
        *fg_b = TERMINAL_COLOR_THEME_LIGHT_DEFAULT_B;
    }
}

struct terminal_renderer_s {
    int cols, rows;
    FT_Library ft_lib;
    FT_Face ft_face;
    int cell_w, cell_h, baseline;
    uint8_t *framebuffer;
    int width_px, height_px, pitch;
    term_renderer_theme_t theme;
    ghostty_terminal_t *ghostty_term;
};

static void blit_glyph(terminal_renderer_t *r, FT_Bitmap *bm, int px, int py,
                        uint8_t fr, uint8_t fg, uint8_t fb,
                        uint8_t br, uint8_t bg, uint8_t bb) {
    for (unsigned row = 0; row < bm->rows; row++) {
        int dy = py + (int)row;
        if (dy < 0 || dy >= r->height_px) continue;
        for (unsigned col = 0; col < bm->width; col++) {
            int dx = px + (int)col;
            if (dx < 0 || dx >= r->width_px) continue;
            uint8_t a = bm->buffer[row * bm->pitch + col];
            uint8_t *dst = r->framebuffer + dy * r->pitch + dx * 3;
            dst[0] = (uint8_t)((fr * a + br * (255 - a)) / 255);
            dst[1] = (uint8_t)((fg * a + bg * (255 - a)) / 255);
            dst[2] = (uint8_t)((fb * a + bb * (255 - a)) / 255);
        }
    }
}

asciichat_error_t term_renderer_create(const term_renderer_config_t *cfg,
                                       terminal_renderer_t **out) {
    terminal_renderer_t *r = SAFE_CALLOC(1, sizeof(*r), terminal_renderer_t *);
    r->cols = cfg->cols;
    r->rows = cfg->rows;
    r->theme = cfg->theme;

    if (FT_Init_FreeType(&r->ft_lib)) {
        SAFE_FREE(r);
        return SET_ERRNO(ERROR_INIT, "FreeType init failed");
    }

    if (cfg->font_data && cfg->font_data_size > 0) {
        if (FT_New_Memory_Face(r->ft_lib, cfg->font_data, (FT_Long)cfg->font_data_size,
                              0, &r->ft_face)) {
            FT_Done_FreeType(r->ft_lib);
            SAFE_FREE(r);
            return SET_ERRNO(ERROR_INIT, "FreeType: cannot load bundled font");
        }
    } else {
        if (FT_New_Face(r->ft_lib, cfg->font_spec, 0, &r->ft_face)) {
            FT_Done_FreeType(r->ft_lib);
            SAFE_FREE(r);
            return SET_ERRNO(ERROR_NOT_FOUND, "FreeType: cannot load font '%s'", cfg->font_spec);
        }
    }

    FT_Set_Char_Size(r->ft_face, 0, (FT_F26Dot6)(cfg->font_size_pt * 64.0), 96, 96);
    FT_Load_Char(r->ft_face, 'M', FT_LOAD_RENDER);
    r->cell_w = (int)(r->ft_face->glyph->advance.x >> 6);
    r->cell_h = (int)(r->ft_face->glyph->bitmap.rows);
    if (r->cell_h == 0) r->cell_h = (int)(r->ft_face->size->metrics.height >> 6);
    r->baseline = (int)(r->ft_face->size->metrics.ascender >> 6);

    r->width_px = r->cols * r->cell_w;
    r->height_px = r->rows * r->cell_h;
    r->pitch = r->width_px * 3;
    r->framebuffer = SAFE_MALLOC((size_t)r->pitch * r->height_px, uint8_t *);
    r->ghostty_term = terminal_new(cfg->cols, cfg->rows);

    log_debug("term_renderer_create: Theme: %s, cell %dx%d px, grid %dx%d",
             (cfg->theme == TERM_RENDERER_THEME_LIGHT) ? "light" : (cfg->theme == TERM_RENDERER_THEME_DARK) ? "dark" : "auto-detect",
             r->cell_w, r->cell_h, r->cols, r->rows);

    *out = r;
    return ASCIICHAT_OK;
}

asciichat_error_t term_renderer_feed(terminal_renderer_t *r,
                                     const char *ansi_frame, size_t len) {
    // Select theme-appropriate default colors based on terminal background
    uint8_t def_bg = (r->theme == TERM_RENDERER_THEME_LIGHT) ? 255 : 0;
    uint8_t def_fg_r = (r->theme == TERM_RENDERER_THEME_LIGHT) ? TERMINAL_COLOR_THEME_LIGHT_DEFAULT_R : TERMINAL_COLOR_THEME_DARK_DEFAULT_R;
    uint8_t def_fg_g = (r->theme == TERM_RENDERER_THEME_LIGHT) ? TERMINAL_COLOR_THEME_LIGHT_DEFAULT_G : TERMINAL_COLOR_THEME_DARK_DEFAULT_G;
    uint8_t def_fg_b = (r->theme == TERM_RENDERER_THEME_LIGHT) ? TERMINAL_COLOR_THEME_LIGHT_DEFAULT_B : TERMINAL_COLOR_THEME_DARK_DEFAULT_B;

    // Fill framebuffer with theme-appropriate background
    for (int py = 0; py < r->height_px; py++) {
        for (int px = 0; px < r->width_px; px++) {
            uint8_t *dst = r->framebuffer + py * r->pitch + px * 3;
            dst[0] = def_bg;
            dst[1] = def_bg;
            dst[2] = def_bg;
        }
    }

    if (!r->ghostty_term) return ASCIICHAT_OK;
    terminal_feed(r->ghostty_term, (const uint8_t *)ansi_frame, len);

    // Render each cell with theme-aware default colors
    for (int row = 0; row < r->rows; row++) {
        for (int col = 0; col < r->cols; col++) {
            uint32_t codepoint = 0;
            // Start with theme-appropriate defaults
            uint8_t fg_r = def_fg_r, fg_g = def_fg_g, fg_b = def_fg_b;
            uint8_t bg_r = def_bg, bg_g = def_bg, bg_b = def_bg;

            terminal_get_cell(r->ghostty_term, row, col, &codepoint,
                            &fg_r, &fg_g, &fg_b, &bg_r, &bg_g, &bg_b);

            if (codepoint >= 32 && codepoint < 127) {
                FT_UInt gi = FT_Get_Char_Index(r->ft_face, (FT_ULong)codepoint);
                if (gi && FT_Load_Glyph(r->ft_face, gi, FT_LOAD_RENDER) == 0) {
                    int px = col * r->cell_w;
                    int py = row * r->cell_h;
                    FT_GlyphSlot g = r->ft_face->glyph;
                    blit_glyph(r, &g->bitmap, px + g->bitmap_left, py + r->baseline - g->bitmap_top,
                               fg_r, fg_g, fg_b, bg_r, bg_g, bg_b);
                }
            }
        }
    }
    return ASCIICHAT_OK;
}

const uint8_t *term_renderer_pixels(terminal_renderer_t *r) { return r->framebuffer; }
int term_renderer_width_px(terminal_renderer_t *r) { return r->width_px; }
int term_renderer_height_px(terminal_renderer_t *r) { return r->height_px; }
int term_renderer_pitch(terminal_renderer_t *r) { return r->pitch; }

void term_renderer_destroy(terminal_renderer_t *r) {
    if (!r) return;
    if (r->ghostty_term) terminal_free(r->ghostty_term);
    FT_Done_Face(r->ft_face);
    FT_Done_FreeType(r->ft_lib);
    SAFE_FREE(r->framebuffer);
    SAFE_FREE(r);
}
#endif
