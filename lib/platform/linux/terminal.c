/**
 * @file platform/linux/terminal.c
 * @ingroup platform
 * @brief Pixel renderer for render-file using Ghostty's SGR parser for ANSI colors
 */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <ghostty/vt.h>
#include <ghostty/vt/color.h>
#include <ghostty/vt/sgr.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <string.h>
#include <stdlib.h>
#include <ascii-chat/video/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/terminal.h>

// Terminal cell with character and color attributes
typedef struct {
    uint32_t codepoint;
    uint8_t fg_r, fg_g, fg_b;
    uint8_t bg_r, bg_g, bg_b;
} terminal_cell_t;

// ANSI 256-color palette lookup
static void ansi_256_to_rgb(uint8_t idx, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (idx < 16) {
        // Standard 16 colors (ANSI colors 0-15)
        static const uint8_t palette16[16*3] = {
            0,0,0,         128,0,0,       0,128,0,       128,128,0,
            0,0,128,       128,0,128,     0,128,128,     192,192,192,
            128,128,128,   255,0,0,       0,255,0,       255,255,0,
            0,0,255,       255,0,255,     0,255,255,     255,255,255
        };
        *r = palette16[idx * 3 + 0];
        *g = palette16[idx * 3 + 1];
        *b = palette16[idx * 3 + 2];
    } else if (idx < 232) {
        // 216-color cube (216 = 6^3, colors 16-231)
        int color_idx = idx - 16;
        *r = (uint8_t)((color_idx / 36) * 51);
        *g = (uint8_t)(((color_idx / 6) % 6) * 51);
        *b = (uint8_t)((color_idx % 6) * 51);
    } else {
        // Grayscale (24 shades, colors 232-255)
        int gray = 8 + (idx - 232) * 10;
        *r = *g = *b = (uint8_t)gray;
    }
}

typedef struct {
    uint32_t cols, rows;
    terminal_cell_t *cells;  // Grid of terminal cells
    // Current SGR attributes for next character
    uint8_t cur_fg_r, cur_fg_g, cur_fg_b;
    uint8_t cur_bg_r, cur_bg_g, cur_bg_b;
} ghostty_terminal_t;

static ghostty_terminal_t *terminal_new(uint32_t cols, uint32_t rows) {
    ghostty_terminal_t *t = SAFE_MALLOC(sizeof(*t), ghostty_terminal_t *);
    t->cols = cols;
    t->rows = rows;
    t->cells = SAFE_CALLOC((size_t)cols * rows, sizeof(terminal_cell_t), terminal_cell_t *);

    // Initialize all cells to space with default colors
    uint8_t def_fg_r, def_fg_g, def_fg_b;
    uint8_t def_bg_r, def_bg_g, def_bg_b;
    int theme = terminal_has_dark_background() ? 0 : 1;
    terminal_get_default_foreground_color(theme, &def_fg_r, &def_fg_g, &def_fg_b);
    terminal_get_default_background_color(theme, &def_bg_r, &def_bg_g, &def_bg_b);

    for (uint32_t i = 0; i < cols * rows; i++) {
        t->cells[i].codepoint = ' ';
        t->cells[i].fg_r = def_fg_r;
        t->cells[i].fg_g = def_fg_g;
        t->cells[i].fg_b = def_fg_b;
        t->cells[i].bg_r = def_bg_r;
        t->cells[i].bg_g = def_bg_g;
        t->cells[i].bg_b = def_bg_b;
    }

    t->cur_fg_r = def_fg_r;
    t->cur_fg_g = def_fg_g;
    t->cur_fg_b = def_fg_b;
    t->cur_bg_r = def_bg_r;
    t->cur_bg_g = def_bg_g;
    t->cur_bg_b = def_bg_b;

    return t;
}

static void terminal_free(ghostty_terminal_t *term) {
    if (!term) return;
    SAFE_FREE(term->cells);
    SAFE_FREE(term);
}

// Parse CSI escape sequence with ghostty SGR parser
static void parse_csi_sequence(ghostty_terminal_t *term, const uint8_t *seq, size_t len) {
    // Find the SGR parameters between '[' and 'm'
    if (len < 3 || seq[0] != '\x1b' || seq[1] != '[') return;

    // Find the terminating 'm'
    size_t end = 2;
    while (end < len && seq[end] != 'm') end++;
    if (end >= len) return;

    // Extract parameter string
    size_t param_len = end - 2;
    if (param_len == 0) {
        // Empty params means reset
        int theme = terminal_has_dark_background() ? 0 : 1;
        terminal_get_default_foreground_color(theme, &term->cur_fg_r, &term->cur_fg_g, &term->cur_fg_b);
        terminal_get_default_background_color(theme, &term->cur_bg_r, &term->cur_bg_g, &term->cur_bg_b);
        return;
    }

    // Parse parameters manually (ghostty parser is overkill for this)
    uint16_t params[32];
    size_t param_count = 0;

    uint32_t current_param = 0;
    for (size_t i = 2; i <= end && param_count < 32; i++) {
        uint8_t c = seq[i];
        if (c >= '0' && c <= '9') {
            current_param = current_param * 10 + (c - '0');
            if (current_param > 65535) current_param = 65535;  // Clamp to uint16_t max
        } else if (c == ';' || c == 'm') {
            params[param_count++] = (uint16_t)current_param;
            current_param = 0;
        }
    }

    // Apply SGR codes
    int theme = terminal_has_dark_background() ? 0 : 1;
    uint8_t def_fg_r, def_fg_g, def_fg_b;
    uint8_t def_bg_r, def_bg_g, def_bg_b;
    terminal_get_default_foreground_color(theme, &def_fg_r, &def_fg_g, &def_fg_b);
    terminal_get_default_background_color(theme, &def_bg_r, &def_bg_g, &def_bg_b);

    for (size_t i = 0; i < param_count; i++) {
        uint16_t code = params[i];

        if (code == 0) {
            // Reset all
            term->cur_fg_r = def_fg_r;
            term->cur_fg_g = def_fg_g;
            term->cur_fg_b = def_fg_b;
            term->cur_bg_r = def_bg_r;
            term->cur_bg_g = def_bg_g;
            term->cur_bg_b = def_bg_b;
        } else if (code >= 30 && code <= 37) {
            // Foreground color (8-color)
            ansi_256_to_rgb((uint8_t)(code - 30), &term->cur_fg_r, &term->cur_fg_g, &term->cur_fg_b);
        } else if (code >= 40 && code <= 47) {
            // Background color (8-color)
            ansi_256_to_rgb((uint8_t)(code - 40), &term->cur_bg_r, &term->cur_bg_g, &term->cur_bg_b);
        } else if (code >= 90 && code <= 97) {
            // Bright foreground color
            ansi_256_to_rgb((uint8_t)(code - 90 + 8), &term->cur_fg_r, &term->cur_fg_g, &term->cur_fg_b);
        } else if (code >= 100 && code <= 107) {
            // Bright background color
            ansi_256_to_rgb((uint8_t)(code - 100 + 8), &term->cur_bg_r, &term->cur_bg_g, &term->cur_bg_b);
        } else if (code == 38 && i + 2 < param_count && params[i+1] == 5) {
            // 256-color foreground
            ansi_256_to_rgb((uint8_t)params[i+2], &term->cur_fg_r, &term->cur_fg_g, &term->cur_fg_b);
            i += 2;
        } else if (code == 48 && i + 2 < param_count && params[i+1] == 5) {
            // 256-color background
            ansi_256_to_rgb((uint8_t)params[i+2], &term->cur_bg_r, &term->cur_bg_g, &term->cur_bg_b);
            i += 2;
        } else if (code == 38 && i + 4 < param_count && params[i+1] == 2) {
            // RGB foreground
            term->cur_fg_r = (uint8_t)params[i+2];
            term->cur_fg_g = (uint8_t)params[i+3];
            term->cur_fg_b = (uint8_t)params[i+4];
            i += 4;
        } else if (code == 48 && i + 4 < param_count && params[i+1] == 2) {
            // RGB background
            term->cur_bg_r = (uint8_t)params[i+2];
            term->cur_bg_g = (uint8_t)params[i+3];
            term->cur_bg_b = (uint8_t)params[i+4];
            i += 4;
        }
    }
}

static void terminal_feed(ghostty_terminal_t *term, const uint8_t *data, size_t len) {
    log_info("terminal_feed: START - received %zu bytes, grid %dx%d", len, term->cols, term->rows);

    if (len > 0) {
        log_info("terminal_feed: first 100 bytes: %.*s", (int)((len < 100) ? len : 100), (const char*)data);
    }

    int row = 0, col = 0;
    int char_count = 0;

    for (size_t i = 0; i < len && row < (int)term->rows; i++) {
        uint8_t c = data[i];

        // Handle CSI escape sequences (SGR color codes)
        if (c == '\x1b' && i + 1 < len && data[i+1] == '[') {
            // Find end of CSI sequence
            size_t seq_end = i + 2;
            while (seq_end < len && data[seq_end] != 'm' && seq_end - i < 100) {
                seq_end++;
            }

            if (seq_end < len && data[seq_end] == 'm') {
                parse_csi_sequence(term, &data[i], seq_end - i + 1);
                i = seq_end;
                continue;
            }
        }

        // Handle newline
        if (c == '\n') {
            row++;
            col = 0;
            continue;
        }

        // Handle carriage return
        if (c == '\r') {
            col = 0;
            continue;
        }

        // Handle printable characters
        if (c >= 32 && c < 127 && col < (int)term->cols && row < (int)term->rows) {
            terminal_cell_t *cell = &term->cells[row * term->cols + col];
            cell->codepoint = c;
            cell->fg_r = term->cur_fg_r;
            cell->fg_g = term->cur_fg_g;
            cell->fg_b = term->cur_fg_b;
            cell->bg_r = term->cur_bg_r;
            cell->bg_g = term->cur_bg_g;
            cell->bg_b = term->cur_bg_b;
            col++;
            char_count++;

            if (char_count <= 10) {
                log_debug("  cell[%d,%d]='%c' fg=(%d,%d,%d) bg=(%d,%d,%d)",
                    row, col-1, c, cell->fg_r, cell->fg_g, cell->fg_b,
                    cell->bg_r, cell->bg_g, cell->bg_b);
            }
        }
    }

    log_info("terminal_feed: END - placed %d characters in %d rows", char_count, row);
}

static void terminal_get_cell(ghostty_terminal_t *term, uint32_t row, uint32_t col,
    uint32_t *codepoint_out, uint8_t *fg_r, uint8_t *fg_g, uint8_t *fg_b,
    uint8_t *bg_r, uint8_t *bg_g, uint8_t *bg_b) {

    if (!term || row >= term->rows || col >= term->cols) {
        // Out of bounds - return default empty cell
        *codepoint_out = ' ';
        int theme = terminal_has_dark_background() ? 0 : 1;
        terminal_get_default_foreground_color(theme, fg_r, fg_g, fg_b);
        terminal_get_default_background_color(theme, bg_r, bg_g, bg_b);
        return;
    }

    terminal_cell_t *cell = &term->cells[row * term->cols + col];
    *codepoint_out = cell->codepoint;
    *fg_r = cell->fg_r;
    *fg_g = cell->fg_g;
    *fg_b = cell->fg_b;
    *bg_r = cell->bg_r;
    *bg_g = cell->bg_g;
    *bg_b = cell->bg_b;
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
    log_info("term_renderer_feed: START - %dx%d grid, %zu bytes of ANSI", r->cols, r->rows, len);

    // Get theme-appropriate default colors from platform abstraction
    uint8_t def_bg_r, def_bg_g, def_bg_b;
    uint8_t def_fg_r, def_fg_g, def_fg_b;
    terminal_get_default_background_color(r->theme, &def_bg_r, &def_bg_g, &def_bg_b);
    terminal_get_default_foreground_color(r->theme, &def_fg_r, &def_fg_g, &def_fg_b);

    log_info("  default BG: RGB(%d,%d,%d), FG: RGB(%d,%d,%d)", def_bg_r, def_bg_g, def_bg_b, def_fg_r, def_fg_g, def_fg_b);

    // Fill framebuffer with theme-appropriate background
    for (int py = 0; py < r->height_px; py++) {
        for (int px = 0; px < r->width_px; px++) {
            uint8_t *dst = r->framebuffer + py * r->pitch + px * 3;
            dst[0] = def_bg_r;
            dst[1] = def_bg_g;
            dst[2] = def_bg_b;
        }
    }

    log_info("  filled framebuffer with background (%d pixels)", r->width_px * r->height_px);

    if (!r->ghostty_term) {
        log_warn("term_renderer_feed: ghostty_term is NULL!");
        return ASCIICHAT_OK;
    }

    terminal_feed(r->ghostty_term, (const uint8_t *)ansi_frame, len);

    // Render each cell with theme-aware default colors
    for (int row = 0; row < r->rows; row++) {
        for (int col = 0; col < r->cols; col++) {
            uint32_t codepoint = 0;
            // Start with theme-appropriate defaults
            uint8_t fg_r = def_fg_r, fg_g = def_fg_g, fg_b = def_fg_b;
            uint8_t bg_r = def_bg_r, bg_g = def_bg_g, bg_b = def_bg_b;

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
