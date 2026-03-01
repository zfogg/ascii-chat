/**
 * @file video/render/file/terminal.c
 * @ingroup video
 * @brief Pixel renderer for render-file: libvterm + FreeType2 software compositor
 *
 * Cross-platform implementation using:
 * - libvterm: Terminal emulation without any display backend
 * - FreeType2: Glyph rasterization
 * - fontconfig: Font resolution (Linux/macOS)
 *
 * No platform-specific code (#if guards) needed — this code compiles on all platforms.
 */

#include <ascii-chat/video/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/log.h>
#include <vterm.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include <string.h>

struct terminal_renderer_s {
  VTerm *vt;
  VTermScreen *vts;
  int cols, rows;
  FT_Library ft_lib;
  FT_Face ft_face;
  int cell_w, cell_h, baseline;
  uint8_t *framebuffer;
  int width_px, height_px, pitch;
  term_renderer_theme_t theme;
  bool is_matrix_font;
};

static int screen_damage(VTermRect r, void *u) {
  (void)r;
  (void)u;
  return 1;
}
static VTermScreenCallbacks g_vterm_cbs = {.damage = screen_damage};

/**
 * Map ASCII characters to Matrix font's Private Use Area glyphs (U+E900-U+E91A).
 * The Matrix-Resurrected font has 27 decorative glyphs in the PUA.
 * Map printable ASCII characters to cycle through these glyphs.
 */
static uint32_t matrix_char_map(uint32_t ascii_char) {
  // Matrix font glyphs: U+E900 to U+E91A (27 glyphs total)
  const uint32_t matrix_start = 0xE900;
  const uint32_t matrix_count = 27;

  // If it's already in the PUA range, return as-is
  if (ascii_char >= 0xE900 && ascii_char <= 0xE91A) {
    return ascii_char;
  }

  // Map printable ASCII (32-126) to matrix glyphs
  if (ascii_char >= 32 && ascii_char <= 126) {
    // Cycle through available glyphs
    uint32_t offset = (ascii_char - 32) % matrix_count;
    return matrix_start + offset;
  }

  // Return unmapped characters as-is (will have no glyph)
  return ascii_char;
}

/**
 * Alpha-composite a FreeType bitmap glyph at pixel (px,py) with fg/bg colors.
 */
static void blit_glyph(terminal_renderer_t *r, FT_Bitmap *bm, int px, int py, uint8_t fr, uint8_t fg, uint8_t fb,
                       uint8_t br, uint8_t bg, uint8_t bb) {
  for (unsigned row = 0; row < bm->rows; row++) {
    int dy = py + (int)row;
    if (dy < 0 || dy >= r->height_px)
      continue;
    for (unsigned col = 0; col < bm->width; col++) {
      int dx = px + (int)col;
      if (dx < 0 || dx >= r->width_px)
        continue;
      uint8_t a = bm->buffer[row * bm->pitch + col];
      uint8_t *dst = r->framebuffer + dy * r->pitch + dx * 3;
      dst[0] = (uint8_t)((fr * a + br * (255 - a)) / 255);
      dst[1] = (uint8_t)((fg * a + bg * (255 - a)) / 255);
      dst[2] = (uint8_t)((fb * a + bb * (255 - a)) / 255);
    }
  }
}

asciichat_error_t term_renderer_create(const term_renderer_config_t *cfg, terminal_renderer_t **out) {
  terminal_renderer_t *r = SAFE_CALLOC(1, sizeof(*r), terminal_renderer_t *);
  r->cols = cfg->cols;
  r->rows = cfg->rows;
  r->theme = cfg->theme;

  log_debug_every(1000, "term_renderer_create: Initializing FreeType");
  if (FT_Init_FreeType(&r->ft_lib)) {
    SAFE_FREE(r);
    return SET_ERRNO(ERROR_INIT, "FreeType init failed");
  }

  // Load font from either file path or memory
  if (cfg->font_data && cfg->font_data_size > 0) {
    log_debug_every(1000, "term_renderer_create: Loading font from memory (%zu bytes)", cfg->font_data_size);
    if (FT_New_Memory_Face(r->ft_lib, cfg->font_data, (FT_Long)cfg->font_data_size, 0, &r->ft_face)) {
      FT_Done_FreeType(r->ft_lib);
      SAFE_FREE(r);
      return SET_ERRNO(ERROR_INIT, "FreeType: cannot load bundled font");
    }
  } else {
    log_debug_every(1000, "term_renderer_create: Loading font from path '%s'", cfg->font_spec);
    if (FT_New_Face(r->ft_lib, cfg->font_spec, 0, &r->ft_face)) {
      FT_Done_FreeType(r->ft_lib);
      SAFE_FREE(r);
      return SET_ERRNO(ERROR_NOT_FOUND, "FreeType: cannot load font '%s'", cfg->font_spec);
    }
  }

  // Detect if this is the matrix font (needs character mapping to Private Use Area)
  r->is_matrix_font = (strstr(cfg->font_spec, "matrix") != NULL || strstr(cfg->font_spec, "Matrix") != NULL);
  if (r->is_matrix_font) {
    log_debug("term_renderer_create: Detected matrix font - will use Private Use Area character mapping");
  }

  // Handle scalable vs bitmap fonts differently
  // Bitmap fonts (like matrix) don't respond to FT_Set_Char_Size
  // Instead, we need to select the best available bitmap strike
  log_debug("term_renderer_create: font='%s' num_fixed_sizes=%d", cfg->font_spec, r->ft_face->num_fixed_sizes);
  if (r->ft_face->num_fixed_sizes > 0) {
    // Bitmap font: select the best matching bitmap strike
    int err = FT_Select_Size(r->ft_face, 0); // Use first available strike
    log_debug("term_renderer_create: [BITMAP] FT_Select_Size(0) returned %d", err);
  } else {
    // Scalable font: use FT_Set_Char_Size
    // FT_Set_Char_Size takes 1/64pt units and DPI — supports fractional point sizes.
    // 96 DPI is the standard screen DPI used here; the 64 factor is the 26.6 fixed-point scale.
    int err = FT_Set_Char_Size(r->ft_face, 0, (FT_F26Dot6)(cfg->font_size_pt * 64.0), 96, 96);
    log_debug("term_renderer_create: [SCALABLE] FT_Set_Char_Size(size_pt=%.1f → %ld 1/64pt) returned %d",
              cfg->font_size_pt, (long)(cfg->font_size_pt * 64.0), err);
  }

  int load_err = FT_Load_Char(r->ft_face, 'M', FT_LOAD_RENDER);
  log_debug("term_renderer_create: FT_Load_Char('M', FT_LOAD_RENDER) returned %d", load_err);

  // For monospace ASCII grid: use advance.x (proper character spacing) and rendered height
  r->cell_w = (int)(r->ft_face->glyph->advance.x >> 6);

  log_debug("term_renderer_create: [GLYPH_M] advance.x=%ld (→ cell_w=%d), bitmap.rows=%d, bitmap.width=%d, "
            "bitmap_top=%d, size->metrics.height=%ld (→ %.1f)",
            r->ft_face->glyph->advance.x, r->cell_w, r->ft_face->glyph->bitmap.rows, r->ft_face->glyph->bitmap.width,
            r->ft_face->glyph->bitmap_top, r->ft_face->size->metrics.height,
            (double)(r->ft_face->size->metrics.height >> 6));

  // For both bitmap and scalable fonts, use the actual glyph bitmap height
  // This ensures text doesn't overflow cells. Using size->metrics.height (line spacing)
  // instead of bitmap.rows causes cells to be too large and text to overflow.
  r->cell_h = r->ft_face->glyph->bitmap.rows;
  log_debug("term_renderer_create: cell_w=%d, cell_h=%d (from bitmap.rows=%d)", r->cell_w, r->cell_h,
            r->ft_face->glyph->bitmap.rows);
  r->baseline = r->ft_face->glyph->bitmap_top;

  r->width_px = r->cols * r->cell_w;
  r->height_px = r->rows * r->cell_h;
  r->pitch = r->width_px * 3;
  r->framebuffer = SAFE_MALLOC((size_t)r->pitch * r->height_px, uint8_t *);

  log_debug("term_renderer_create: Final dims: %dx%d cells, %dx%d pixels, cell(w=%d,h=%d)", r->cols, r->rows,
            r->width_px, r->height_px, r->cell_w, r->cell_h);

  r->vt = vterm_new(r->rows, r->cols);
  r->vts = vterm_obtain_screen(r->vt);
  vterm_screen_set_callbacks(r->vts, &g_vterm_cbs, r);
  vterm_screen_reset(r->vts, 1);

  log_debug_every(1000, "term_renderer_create: Renderer created (%dx%d cells, %dx%d pixels)", r->cols, r->rows,
                  r->width_px, r->height_px);

  *out = r;
  return ASCIICHAT_OK;
}

asciichat_error_t term_renderer_feed(terminal_renderer_t *r, const char *ansi_frame, size_t len) {
  FILE *dbg = fopen("/tmp/render-debug.txt", "a");
  if (dbg) {
    fprintf(dbg, "[TERM_RENDERER_FEED] Called with len=%zu, r=%p, dims=%dx%d\n", len, (void *)r, r->width_px,
            r->height_px);
    fclose(dbg);
  }
  static const char home[] = "\033[H";
  log_debug("term_renderer_feed: Processing ANSI frame (len=%zu, first 100 chars: %.100s)", len, ansi_frame);
  vterm_input_write(r->vt, home, sizeof(home) - 1);
  vterm_input_write(r->vt, ansi_frame, len);

  uint8_t def_bg = (r->theme == TERM_RENDERER_THEME_LIGHT) ? 255 : 0;

  int cells_with_chars = 0, cells_rendered = 0;
  for (int row = 0; row < r->rows; row++) {
    for (int col = 0; col < r->cols; col++) {
      VTermScreenCell cell;
      vterm_screen_get_cell(r->vts, (VTermPos){row, col}, &cell);

      uint8_t fr, fg, fb, br, bg, bb;
      if (VTERM_COLOR_IS_RGB(&cell.fg)) {
        fr = cell.fg.rgb.red;
        fg = cell.fg.rgb.green;
        fb = cell.fg.rgb.blue;
      } else {
        fr = fg = fb = 204;
      }

      if (VTERM_COLOR_IS_RGB(&cell.bg)) {
        br = cell.bg.rgb.red;
        bg = cell.bg.rgb.green;
        bb = cell.bg.rgb.blue;
      } else {
        br = bg = bb = def_bg;
      }

      int px = col * r->cell_w, py = row * r->cell_h;
      for (int dy = 0; dy < r->cell_h; dy++) {
        uint8_t *line = r->framebuffer + (py + dy) * r->pitch + px * 3;
        for (int dx = 0; dx < r->cell_w; dx++) {
          line[dx * 3] = br;
          line[dx * 3 + 1] = bg;
          line[dx * 3 + 2] = bb;
        }
      }

      if (cell.chars[0] && cell.chars[0] != ' ') {
        cells_with_chars++;
        // For matrix font, map ASCII characters to Private Use Area glyphs (U+E900-U+E91A)
        uint32_t char_to_render = r->is_matrix_font ? matrix_char_map(cell.chars[0]) : cell.chars[0];
        FT_UInt gi = FT_Get_Char_Index(r->ft_face, char_to_render);
        if (gi) {
          if (FT_Load_Glyph(r->ft_face, gi, FT_LOAD_RENDER) == 0) {
            FT_GlyphSlot g = r->ft_face->glyph;

            // Blit if we have content
            if (g->bitmap.width > 0 && g->bitmap.rows > 0) {
              blit_glyph(r, &g->bitmap, px + g->bitmap_left, py + r->baseline - g->bitmap_top, fr, fg, fb, br, bg, bb);
              cells_rendered++;
            }
          }
        }
      }
    }
  }

  // Sample pixels from framebuffer to verify content
  uint8_t *sample_top = r->framebuffer;
  uint8_t *sample_mid = r->framebuffer + (r->height_px / 2) * r->pitch;
  uint8_t *sample_bot = r->framebuffer + (r->height_px - 1) * r->pitch;
  log_debug("term_renderer_feed: cells_with_chars=%d, cells_rendered=%d", cells_with_chars, cells_rendered);
  log_debug(
      "term_renderer_feed: pixel samples - top_left RGB(%d,%d,%d), mid_left RGB(%d,%d,%d), bot_left RGB(%d,%d,%d)",
      sample_top[0], sample_top[1], sample_top[2], sample_mid[0], sample_mid[1], sample_mid[2], sample_bot[0],
      sample_bot[1], sample_bot[2]);

  return ASCIICHAT_OK;
}

const uint8_t *term_renderer_pixels(terminal_renderer_t *r) {
  return r->framebuffer;
}
int term_renderer_width_px(terminal_renderer_t *r) {
  return r->width_px;
}
int term_renderer_height_px(terminal_renderer_t *r) {
  return r->height_px;
}
int term_renderer_pitch(terminal_renderer_t *r) {
  return r->pitch;
}

void term_renderer_destroy(terminal_renderer_t *r) {
  if (!r)
    return;
  vterm_free(r->vt);
  FT_Done_Face(r->ft_face);
  FT_Done_FreeType(r->ft_lib);
  SAFE_FREE(r->framebuffer);
  SAFE_FREE(r);
}
