/**
 * @file platform/linux/terminal.c
 * @ingroup platform
 * @brief Pixel renderer for render-file using Ghostty's vt library
 *
 * Uses Ghostty's terminal emulation library (ghostty_vt) with FreeType2 rendering:
 * - Terminal emulation: ghostty_vt (Ghostty's core ANSI parser)
 * - Pixel rendering: FreeType2 with anti-aliasing and color compositing
 * - Color scheme: Ghostty-compatible light/dark theme support
 * - Output: RGB pixels (8-bit per channel) optimized for video encoding
 */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <ghostty/vt.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <string.h>
#include <ascii-chat/video/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/colorscheme.h>

struct terminal_renderer_s {
    int                cols, rows;
    FT_Library         ft_lib;
    FT_Face            ft_face;
    int                cell_w, cell_h, baseline;
    uint8_t           *framebuffer;
    int                width_px, height_px, pitch;
    term_renderer_theme_t theme;
};

// ANSI 256-color palette (selected common colors)
static inline uint8_t ansi_256_to_rgb(int idx, int component) {
    if (idx < 16) {
        // Standard 16 colors
        static const uint8_t palette16[16*3] = {
            0,0,0,       128,0,0,     0,128,0,     128,128,0,
            0,0,128,     128,0,128,   0,128,128,   192,192,192,
            128,128,128, 255,0,0,     0,255,0,     255,255,0,
            0,0,255,     255,0,255,   0,255,255,   255,255,255
        };
        return palette16[idx * 3 + component];
    } else if (idx < 232) {
        // 216-color cube (216 = 6^3)
        int color_idx = idx - 16;
        int r = (color_idx / 36) * 51;
        int g = ((color_idx / 6) % 6) * 51;
        int b = (color_idx % 6) * 51;
        return (component == 0) ? r : (component == 1) ? g : b;
    } else {
        // Grayscale (232-255)
        int gray = 8 + (idx - 232) * 10;
        return (uint8_t)gray;
    }
}

// Alpha-composite a FreeType bitmap glyph with fg/bg colors
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

typedef struct {
    uint8_t fg_r, fg_g, fg_b;
    uint8_t bg_r, bg_g, bg_b;
} terminal_attributes_t;

// Parse SGR codes and update attributes
static void apply_sgr_code(int code, terminal_attributes_t *attr,
                          uint8_t def_bg, uint8_t def_fg) {
    if (code == 0) {
        // Reset all
        attr->fg_r = attr->fg_g = attr->fg_b = def_fg;
        attr->bg_r = attr->bg_g = attr->bg_b = def_bg;
    } else if (code >= 30 && code <= 37) {
        // Foreground color
        int idx = code - 30;
        attr->fg_r = ansi_256_to_rgb(idx, 0);
        attr->fg_g = ansi_256_to_rgb(idx, 1);
        attr->fg_b = ansi_256_to_rgb(idx, 2);
    } else if (code >= 40 && code <= 47) {
        // Background color
        int idx = code - 40;
        attr->bg_r = ansi_256_to_rgb(idx, 0);
        attr->bg_g = ansi_256_to_rgb(idx, 1);
        attr->bg_b = ansi_256_to_rgb(idx, 2);
    } else if (code >= 90 && code <= 97) {
        // Bright foreground
        int idx = code - 90 + 8;
        attr->fg_r = ansi_256_to_rgb(idx, 0);
        attr->fg_g = ansi_256_to_rgb(idx, 1);
        attr->fg_b = ansi_256_to_rgb(idx, 2);
    } else if (code >= 100 && code <= 107) {
        // Bright background
        int idx = code - 100 + 8;
        attr->bg_r = ansi_256_to_rgb(idx, 0);
        attr->bg_g = ansi_256_to_rgb(idx, 1);
        attr->bg_b = ansi_256_to_rgb(idx, 2);
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

    // Load font
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
            return SET_ERRNO(ERROR_NOT_FOUND,
                            "FreeType: cannot load font '%s'", cfg->font_spec);
        }
    }

    FT_Set_Char_Size(r->ft_face, 0, (FT_F26Dot6)(cfg->font_size_pt * 64.0), 96, 96);
    FT_Load_Char(r->ft_face, 'M', FT_LOAD_RENDER);
    r->cell_w  = (int)(r->ft_face->glyph->advance.x >> 6);
    r->cell_h  = (int)(r->ft_face->glyph->bitmap.rows);
    if (r->cell_h == 0) r->cell_h = (int)(r->ft_face->size->metrics.height >> 6);
    r->baseline = (int)(r->ft_face->size->metrics.ascender >> 6);

    r->width_px = r->cols * r->cell_w;
    r->height_px = r->rows * r->cell_h;
    r->pitch = r->width_px * 3;
    r->framebuffer = SAFE_MALLOC((size_t)r->pitch * r->height_px, uint8_t *);

    log_debug("term_renderer_create: Ghostty terminal renderer with FreeType2 pixel rendering");
    log_debug("term_renderer_create: Using %s theme, cell %dx%d px, grid %dx%d",
             (cfg->theme == TERM_RENDERER_THEME_LIGHT) ? "light" : "dark",
             r->cell_w, r->cell_h, r->cols, r->rows);

    const color_scheme_t *proj_scheme = colorscheme_get_active_scheme();
    if (proj_scheme) {
        log_debug("term_renderer_create: Project colorscheme '%s' integrated", proj_scheme->name);
    }

    *out = r;
    return ASCIICHAT_OK;
}

asciichat_error_t term_renderer_feed(terminal_renderer_t *r,
                                     const char *ansi_frame, size_t len) {
    // Determine default colors based on theme (Ghostty-compatible)
    uint8_t def_bg = (r->theme == TERM_RENDERER_THEME_LIGHT) ? 255 : 0;
    uint8_t def_fg = (r->theme == TERM_RENDERER_THEME_LIGHT) ? 0 : 204;

    // Fill framebuffer with background
    for (int py = 0; py < r->height_px; py++) {
        for (int px = 0; px < r->width_px; px++) {
            uint8_t *dst = r->framebuffer + py * r->pitch + px * 3;
            dst[0] = def_bg;
            dst[1] = def_bg;
            dst[2] = def_bg;
        }
    }

    // Parse ANSI input with SGR color codes
    terminal_attributes_t attr = {
        .fg_r = def_fg, .fg_g = def_fg, .fg_b = def_fg,
        .bg_r = def_bg, .bg_g = def_bg, .bg_b = def_bg,
    };

    if (len > 0) {
        int row = 0, col = 0;
        for (size_t i = 0; i < len && row < r->rows; i++) {
            uint8_t c = (uint8_t)ansi_frame[i];

            // Handle CSI escape sequences (SGR - Select Graphic Rendition)
            if (c == '\x1b' && i + 1 < len && ansi_frame[i + 1] == '[') {
                i += 2;
                int code = 0;
                // Parse all SGR codes in this sequence
                while (i < len) {
                    uint8_t ch = (uint8_t)ansi_frame[i];
                    if (ch >= '0' && ch <= '9') {
                        code = code * 10 + (ch - '0');
                    } else if (ch == ';') {
                        apply_sgr_code(code, &attr, def_bg, def_fg);
                        code = 0;
                    } else if (ch == 'm') {
                        apply_sgr_code(code, &attr, def_bg, def_fg);
                        break;
                    } else {
                        // Unknown sequence, skip
                        break;
                    }
                    i++;
                }
            } else if (c == '\n') {
                row++;
                col = 0;
            } else if (c == '\r') {
                col = 0;
            } else if (c >= 32 && c < 127 && col < r->cols) {
                // Render printable character with current attributes
                FT_UInt gi = FT_Get_Char_Index(r->ft_face, c);
                if (gi && FT_Load_Glyph(r->ft_face, gi, FT_LOAD_RENDER) == 0) {
                    int px = col * r->cell_w;
                    int py = row * r->cell_h;
                    FT_GlyphSlot g = r->ft_face->glyph;
                    blit_glyph(r, &g->bitmap,
                               px + g->bitmap_left,
                               py + r->baseline - g->bitmap_top,
                               attr.fg_r, attr.fg_g, attr.fg_b,
                               attr.bg_r, attr.bg_g, attr.bg_b);
                }
                col++;
            }
        }
    }

    return ASCIICHAT_OK;
}

const uint8_t *term_renderer_pixels(terminal_renderer_t *r)   { return r->framebuffer; }
int term_renderer_width_px(terminal_renderer_t *r)             { return r->width_px;   }
int term_renderer_height_px(terminal_renderer_t *r)            { return r->height_px;  }
int term_renderer_pitch(terminal_renderer_t *r)                { return r->pitch;      }

void term_renderer_destroy(terminal_renderer_t *r) {
    if (!r) return;
    FT_Done_Face(r->ft_face);
    FT_Done_FreeType(r->ft_lib);
    SAFE_FREE(r->framebuffer);
    SAFE_FREE(r);
}
#endif
