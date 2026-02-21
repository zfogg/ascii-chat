/**
 * @file platform/linux/terminal.c
 * @ingroup platform
 * @brief Pixel renderer for render-file: libvterm + FreeType2 software compositor
 */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <ascii-chat/video/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/logging.h>
#include <vterm.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <string.h>

struct terminal_renderer_s {
    VTerm         *vt;
    VTermScreen   *vts;
    int            cols, rows;
    FT_Library     ft_lib;
    FT_Face        ft_face;
    int            cell_w, cell_h, baseline;
    uint8_t       *framebuffer;
    int            width_px, height_px, pitch;
    term_renderer_theme_t theme;
};

static int screen_damage(VTermRect r, void *u) { (void)r; (void)u; return 1; }
static VTermScreenCallbacks g_vterm_cbs = { .damage = screen_damage };

// Alpha-composite a FreeType bitmap glyph at pixel (px,py) with fg/bg colors.
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

    // Load font from either file path or memory
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

    // FT_Set_Char_Size takes 1/64pt units and DPI — supports fractional point sizes.
    // 96 DPI is the standard screen DPI used here; the 64 factor is the 26.6 fixed-point scale.
    FT_Set_Char_Size(r->ft_face, 0, (FT_F26Dot6)(cfg->font_size_pt * 64.0), 96, 96);

    FT_Load_Char(r->ft_face, 'M', FT_LOAD_RENDER);
    r->cell_w  = (int)(r->ft_face->glyph->advance.x >> 6);
    r->cell_h  = r->cell_w;
    r->baseline = (int)(r->ft_face->size->metrics.ascender >> 6);

    r->width_px = r->cols * r->cell_w;
    r->height_px = r->rows * r->cell_h;
    r->pitch = r->width_px * 3;
    r->framebuffer = SAFE_MALLOC((size_t)r->pitch * r->height_px, uint8_t *);

    r->vt  = vterm_new(r->rows, r->cols);
    r->vts = vterm_obtain_screen(r->vt);
    vterm_screen_set_callbacks(r->vts, &g_vterm_cbs, r);
    vterm_screen_reset(r->vts, 1);

    *out = r;
    return ASCIICHAT_OK;
}

asciichat_error_t term_renderer_feed(terminal_renderer_t *r,
                                     const char *ansi_frame, size_t len) {
    static const char home[] = "\033[H";
    vterm_input_write(r->vt, home, sizeof(home) - 1);
    vterm_input_write(r->vt, ansi_frame, len);

    uint8_t def_bg = (r->theme == TERM_RENDERER_THEME_LIGHT) ? 255 : 0;

    for (int row = 0; row < r->rows; row++) {
        for (int col = 0; col < r->cols; col++) {
            VTermScreenCell cell;
            vterm_screen_get_cell(r->vts, (VTermPos){row, col}, &cell);

            uint8_t fr, fg, fb, br, bg, bb;
            if (VTERM_COLOR_IS_RGB(&cell.fg))
                { fr=cell.fg.rgb.red; fg=cell.fg.rgb.green; fb=cell.fg.rgb.blue; }
            else { fr=fg=fb=204; }

            if (VTERM_COLOR_IS_RGB(&cell.bg))
                { br=cell.bg.rgb.red; bg=cell.bg.rgb.green; bb=cell.bg.rgb.blue; }
            else { br=bg=bb=def_bg; }

            int px = col * r->cell_w, py = row * r->cell_h;
            for (int dy = 0; dy < r->cell_h; dy++) {
                uint8_t *line = r->framebuffer + (py+dy)*r->pitch + px*3;
                for (int dx = 0; dx < r->cell_w; dx++)
                    { line[dx*3]=br; line[dx*3+1]=bg; line[dx*3+2]=bb; }
            }

            if (cell.chars[0] && cell.chars[0] != ' ') {
                FT_UInt gi = FT_Get_Char_Index(r->ft_face, cell.chars[0]);
                if (gi && FT_Load_Glyph(r->ft_face, gi, FT_LOAD_RENDER) == 0) {
                    FT_GlyphSlot g = r->ft_face->glyph;
                    blit_glyph(r, &g->bitmap,
                               px + g->bitmap_left,
                               py + r->baseline - g->bitmap_top,
                               fr,fg,fb, br,bg,bb);
                }
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
    vterm_free(r->vt);
    FT_Done_Face(r->ft_face);
    FT_Done_FreeType(r->ft_lib);
    SAFE_FREE(r->framebuffer);
    SAFE_FREE(r);
}
#endif
