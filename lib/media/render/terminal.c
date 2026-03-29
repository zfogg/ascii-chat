/**
 * @file media/render/terminal.c
 * @ingroup media
 * @brief Pixel renderer for render-file: libvterm + FreeType2 software compositor
 *
 * Cross-platform implementation using:
 * - libvterm: Terminal emulation without any display backend
 * - FreeType2: Glyph rasterization
 * - fontconfig: Font resolution (Linux/macOS)
 *
 * No platform-specific code (#if guards) needed — this code compiles on all platforms.
 */

#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <vterm.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include <string.h>
#include <stddef.h>

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

      uint8_t *dst = r->framebuffer + dy * r->pitch + dx * 4;
      uint8_t r_val = (uint8_t)((fr * a + br * (255 - a)) / 255);
      uint8_t g_val = (uint8_t)((fg * a + bg * (255 - a)) / 255);
      uint8_t b_val = (uint8_t)((fb * a + bb * (255 - a)) / 255);

      dst[0] = r_val;
      dst[1] = g_val;
      dst[2] = b_val;
      dst[3] = 255; // Alpha (fully opaque)
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
  log_debug("STRUCT_OFFSETS: sizeof(term_renderer_config_t)=%zu", sizeof(term_renderer_config_t));
  log_debug("FIELD_OFFSETS: cols=%zu, rows=%zu, font_size_pt=%zu, theme=%zu, font_spec=%zu, font_is_path=%zu, font_data=%zu, font_data_size=%zu",
           offsetof(term_renderer_config_t, cols),
           offsetof(term_renderer_config_t, rows),
           offsetof(term_renderer_config_t, font_size_pt),
           offsetof(term_renderer_config_t, theme),
           offsetof(term_renderer_config_t, font_spec),
           offsetof(term_renderer_config_t, font_is_path),
           offsetof(term_renderer_config_t, font_data),
           offsetof(term_renderer_config_t, font_data_size));
  log_debug("term_renderer_create: font_data=%p, font_data_size=%zu, font_spec='%s'",
           cfg->font_data, cfg->font_data_size, cfg->font_spec);
  if (cfg->font_data && cfg->font_data_size > 0) {
    log_debug_every(1000, "term_renderer_create: Loading font from memory (%zu bytes) at ptr %p",
                   cfg->font_data_size, cfg->font_data);
    if (FT_New_Memory_Face(r->ft_lib, cfg->font_data, (FT_Long)cfg->font_data_size, 0, &r->ft_face)) {
      log_debug("FT_New_Memory_Face failed with code %d", 0);  // FreeType returns 0 on success
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
  log_debug("DEBUG: FT_Load_Char('M', FT_LOAD_RENDER) returned %d", load_err);
  log_debug("DEBUG: glyph->bitmap.rows=%d, glyph->bitmap.width=%d, glyph->bitmap_top=%d",
            r->ft_face->glyph->bitmap.rows, r->ft_face->glyph->bitmap.width, r->ft_face->glyph->bitmap_top);

  // For monospace ASCII grid: use advance.x (proper character spacing) and rendered height
  FT_Pos advance_x_26_6 = r->ft_face->glyph->advance.x;
  log_debug("DEBUG: advance.x (26.6pt)=%ld", advance_x_26_6);

  // Calculate cell width from font metrics
  r->cell_w = (int)(advance_x_26_6 >> 6);

  // Only add spacing to fill entire frame if --stretch is specified
  bool stretch_mode = GET_OPTION(stretch);
  if (stretch_mode) {
    r->cell_w++;
    log_debug("ADVANCE_X: value=%ld (26.6pt) → cell_w=%d (advance + 1 for stretch), cols=%d → width_px=%d",
              advance_x_26_6, r->cell_w, r->cols, r->cols * r->cell_w);
  } else {
    log_debug("ADVANCE_X: value=%ld (26.6pt) → cell_w=%d (preserve aspect ratio), cols=%d → width_px=%d",
              advance_x_26_6, r->cell_w, r->cols, r->cols * r->cell_w);
  }

  // For both bitmap and scalable fonts, use the actual glyph bitmap height
  // This ensures text doesn't overflow cells. Using size->metrics.height (line spacing)
  // instead of bitmap.rows causes cells to be too large and text to overflow.
  r->cell_h = r->ft_face->glyph->bitmap.rows;
  int raw_bitmap_top = r->ft_face->glyph->bitmap_top;
  log_debug("DEBUG: cell_h=%d (from bitmap.rows), raw_bitmap_top=%d", r->cell_h, raw_bitmap_top);

  // Apply aspect ratio correction unless --stretch is specified
  // Terminal characters are typically 2:1 (height:width) to appear normal
  if (!stretch_mode) {
    int corrected_h = r->cell_w * 2;
    if (corrected_h != r->cell_h && r->cell_h > 0) {
      log_debug("ASPECT_RATIO: Correcting cell_h from %d to %d (2x width=%d)", r->cell_h, corrected_h, r->cell_w);
      r->cell_h = corrected_h;
    }
  }

  // Position baseline vertically within the cell.
  // Baseline should be positioned at approximately 75% of cell height to allow proper
  // glyph rendering with space both above and below. This prevents glyphs from being
  // clipped at the cell boundaries.
  //
  // Baseline position: y_glyph = py + r->baseline - g->bitmap_top
  // We want: 0 <= baseline - bitmap_top < cell_h (glyph fits within cell)
  // Choose: baseline = (3 * cell_h) / 4 (positions at 75% height)
  r->baseline = (3 * r->cell_h) / 4;
  log_debug("DEBUG: baseline set to %d (75%% of cell_h=%d)", r->baseline, r->cell_h);

  r->width_px = r->cols * r->cell_w;
  r->height_px = r->rows * r->cell_h;
  log_debug("DEBUG: calculated dimensions: width_px=%d (cols=%d * cell_w=%d), height_px=%d (rows=%d * cell_h=%d)",
            r->width_px, r->cols, r->cell_w, r->height_px, r->rows, r->cell_h);
  // Pitch in bytes: each pixel is 4 bytes (RGBA), already aligned to 4-byte boundary
  // No padding needed since 4 is already a multiple of 4
  int base_pitch = r->width_px * 4;
  r->pitch = base_pitch;
  log_debug("PITCH_CALC: width_px=%d, base_pitch=%d, final_pitch=%d (RGBA, no padding needed)", r->width_px, base_pitch,
            r->pitch);
  log_debug("DEBUG: Allocating framebuffer: %d bytes total (%d pitch * %d height)",
            (int)((size_t)r->pitch * r->height_px), r->pitch, r->height_px);
  r->framebuffer = SAFE_MALLOC((size_t)r->pitch * r->height_px, uint8_t *);

  r->vt = vterm_new(r->rows, r->cols);
  r->vts = vterm_obtain_screen(r->vt);

  // Set size BEFORE reset to ensure screen is allocated with correct dimensions
  vterm_set_size(r->vt, r->rows, r->cols);
  log_debug("DEBUG: vterm_set_size called with rows=%d cols=%d", r->rows, r->cols);

  // Reset MUST come before any input writing to initialize state correctly
  vterm_screen_reset(r->vts, 1);

  // Verify the size was set correctly after reset
  int vt_rows, vt_cols;
  vterm_get_size(r->vt, &vt_rows, &vt_cols);
  log_debug("DEBUG: vterm_get_size returned rows=%d cols=%d (expected %d,%d)", vt_rows, vt_cols, r->rows, r->cols);

  // CRITICAL: Set size AGAIN after reset, as reset may have affected it
  vterm_set_size(r->vt, r->rows, r->cols);
  log_debug("DEBUG: vterm_set_size called AGAIN after reset");

  *out = r;
  return ASCIICHAT_OK;
}

asciichat_error_t term_renderer_feed(terminal_renderer_t *r, const char *ansi_frame, size_t len) {
  // Clear framebuffer to ensure no leftover pixels from previous frames
  uint8_t def_bg = (r->theme == TERM_RENDERER_THEME_LIGHT) ? 255 : 0;
  memset(r->framebuffer, def_bg, (size_t)r->pitch * r->height_px);

  static const char home[] = "\033[H";
  vterm_input_write(r->vt, home, sizeof(home) - 1);

  // Count newlines to determine CRLF conversion size
  int newline_count = 0;
  for (size_t i = 0; i < len; i++) {
    if (ansi_frame[i] == '\n')
      newline_count++;
  }

  // Convert LF-only line endings to CRLF (vterm expects CRLF)
  // This fixes cursor positioning on alternate rows
  char *fixed_frame = SAFE_MALLOC(len + newline_count, char *);
  if (!fixed_frame) {
    log_error("failed to allocate memory for CRLF conversion");
    return SET_ERRNO(ERROR_MEMORY, "CRLF conversion allocation failed");
  }

  size_t fixed_pos = 0;
  for (size_t i = 0; i < len; i++) {
    fixed_frame[fixed_pos++] = ansi_frame[i];
    if (ansi_frame[i] == '\n' && (i == 0 || ansi_frame[i - 1] != '\r')) {
      // Insert carriage return before LF if not already preceded by CR
      fixed_frame[fixed_pos - 1] = '\r';
      fixed_frame[fixed_pos++] = '\n';
    }
  }

  vterm_input_write(r->vt, fixed_frame, fixed_pos);
  SAFE_FREE(fixed_frame);

  // Calculate centering offsets to position grid in center of canvas
  int grid_width_px = r->cols * r->cell_w;
  int grid_height_px = r->rows * r->cell_h;
  int offset_x = (r->width_px - grid_width_px) / 2;
  int offset_y = (r->height_px - grid_height_px) / 2;

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

      int px = offset_x + col * r->cell_w, py = offset_y + row * r->cell_h;
      for (int dy = 0; dy < r->cell_h; dy++) {
        int y = py + dy;
        if (y < 0 || y >= r->height_px)
          continue;
        uint8_t *line = r->framebuffer + y * r->pitch + px * 4;
        for (int dx = 0; dx < r->cell_w; dx++) {
          int x = px + dx;
          if (x < 0 || x >= r->width_px)
            continue;
          line[dx * 4] = br;
          line[dx * 4 + 1] = bg;
          line[dx * 4 + 2] = bb;
          line[dx * 4 + 3] = 255; // Alpha (fully opaque)
        }
      }

      if (cell.chars[0] && cell.chars[0] != ' ') {
        // For matrix font, map ASCII characters to Private Use Area glyphs (U+E900-U+E91A)
        uint32_t char_to_render = r->is_matrix_font ? matrix_char_map(cell.chars[0]) : cell.chars[0];
        FT_UInt gi = FT_Get_Char_Index(r->ft_face, char_to_render);

        if (gi) {
          if (FT_Load_Glyph(r->ft_face, gi, FT_LOAD_RENDER) == 0) {
            FT_GlyphSlot g = r->ft_face->glyph;

            // Blit if we have content
            if (g->bitmap.width > 0 && g->bitmap.rows > 0) {
              blit_glyph(r, &g->bitmap, px + g->bitmap_left, py + r->baseline - g->bitmap_top, fr, fg, fb, br, bg, bb);
            }
          }
        }
      }
    }
  }

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

int term_renderer_get_cols(terminal_renderer_t *r) {
  if (!r)
    return 0;
  return r->cols;
}

int term_renderer_get_rows(terminal_renderer_t *r) {
  if (!r)
    return 0;
  return r->rows;
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
