/**
 * @file video/ascii/file/terminal.c
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

#include <ascii-chat/video/ascii/file/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
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
    log_info("ADVANCE_X: value=%ld (26.6pt) → cell_w=%d (advance + 1 for stretch), cols=%d → width_px=%d",
             advance_x_26_6, r->cell_w, r->cols, r->cols * r->cell_w);
  } else {
    log_info("ADVANCE_X: value=%ld (26.6pt) → cell_w=%d (preserve aspect ratio), cols=%d → width_px=%d", advance_x_26_6,
             r->cell_w, r->cols, r->cols * r->cell_w);
  }

  // For both bitmap and scalable fonts, use the actual glyph bitmap height
  // This ensures text doesn't overflow cells. Using size->metrics.height (line spacing)
  // instead of bitmap.rows causes cells to be too large and text to overflow.
  r->cell_h = r->ft_face->glyph->bitmap.rows;
  r->baseline = r->ft_face->glyph->bitmap_top;
  log_debug("DEBUG: cell_h=%d (from bitmap.rows), baseline=%d", r->cell_h, r->baseline);

  // Apply aspect ratio correction unless --stretch is specified
  // Terminal characters are typically 2:1 (height:width) to appear normal
  if (!stretch_mode) {
    int corrected_h = r->cell_w * 2;
    if (corrected_h != r->cell_h && r->cell_h > 0) {
      log_info("ASPECT_RATIO: Correcting cell_h from %d to %d (2x width=%d), adjusting baseline", r->cell_h,
               corrected_h, r->cell_w);
      // When we change cell height, scale baseline proportionally
      r->baseline = (r->baseline * corrected_h) / r->cell_h;
      r->cell_h = corrected_h;
    }
  }

  r->width_px = r->cols * r->cell_w;
  r->height_px = r->rows * r->cell_h;
  log_debug("DEBUG: calculated dimensions: width_px=%d (cols=%d * cell_w=%d), height_px=%d (rows=%d * cell_h=%d)",
            r->width_px, r->cols, r->cell_w, r->height_px, r->rows, r->cell_h);
  // Pitch in bytes: align to 4-byte boundary for proper row alignment
  // Each pixel is 3 bytes (RGB), so we calculate base pitch and round up
  int base_pitch = r->width_px * 3;
  r->pitch = (base_pitch + 3) & ~3; // Round up to next multiple of 4
  log_info("PITCH_CALC: width_px=%d, base_pitch=%d, final_pitch=%d (padded by %d bytes)", r->width_px, base_pitch,
           r->pitch, r->pitch - base_pitch);
  log_debug("DEBUG: Allocating framebuffer: %d bytes total (%d pitch * %d height)",
            (int)((size_t)r->pitch * r->height_px), r->pitch, r->height_px);
  r->framebuffer = SAFE_MALLOC((size_t)r->pitch * r->height_px, uint8_t *);

  r->vt = vterm_new(r->rows, r->cols);
  r->vts = vterm_obtain_screen(r->vt);

  // Set size BEFORE reset to ensure screen is allocated with correct dimensions
  vterm_set_size(r->vt, r->rows, r->cols);
  log_debug("DEBUG: vterm_set_size called with rows=%d cols=%d", r->rows, r->cols);

  vterm_screen_set_callbacks(r->vts, &g_vterm_cbs, r);
  // Reset MUST come before any input writing to initialize state correctly
  vterm_screen_reset(r->vts, 1);

  // Verify the size was set correctly after reset
  int vt_rows, vt_cols;
  vterm_get_size(r->vt, &vt_rows, &vt_cols);
  log_info("DEBUG: vterm_get_size returned rows=%d cols=%d (expected %d,%d)", vt_rows, vt_cols, r->rows, r->cols);

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
  log_debug("DEBUG: fed ANSI frame to vterm with CRLF line endings (original_len=%zu, fixed_len=%zu)", len, fixed_pos);
  SAFE_FREE(fixed_frame);

  // Check ALL line lengths to see if there's an alternating pattern
  size_t line_num = 0;
  size_t pos = 0;
  size_t visible_chars = 0;
  bool in_ansi = false;

  while (pos < len && line_num < 45) { // Check all 42 lines
    char c = ansi_frame[pos];

    if (c == '\033') {
      in_ansi = true;
    } else if (in_ansi && c == 'm') {
      in_ansi = false;
    } else if (!in_ansi && c == '\n') {
      if (line_num < 10 || line_num >= 40) {
        log_debug("DEBUG: ANSI line %zu has %zu visible chars", line_num, visible_chars);
      }
      visible_chars = 0;
      line_num++;
    } else if (!in_ansi) {
      visible_chars++;
    }
    pos++;
  }

  int cells_with_chars = 0, cells_rendered = 0;
  log_debug("DEBUG: RENDER_LOOP starting - grid is %d rows × %d cols, cell dimensions w=%d h=%d", r->rows, r->cols,
            r->cell_w, r->cell_h);

  for (int row = 0; row < r->rows; row++) {
    for (int col = 0; col < r->cols; col++) {
      VTermScreenCell cell;
      vterm_screen_get_cell(r->vts, (VTermPos){row, col}, &cell);

      // Debug sample: first row, every 50th column
      if (row == 0 && col % 50 == 0) {
        log_debug("DEBUG: ROW0 COL%d: char=0x%02x ('%c'), has_fg=%d has_bg=%d", col, cell.chars[0],
                  (cell.chars[0] >= 32 && cell.chars[0] < 127) ? cell.chars[0] : '?', VTERM_COLOR_IS_RGB(&cell.fg),
                  VTERM_COLOR_IS_RGB(&cell.bg));
      }

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
        int y = py + dy;
        if (y < 0 || y >= r->height_px)
          continue;
        uint8_t *line = r->framebuffer + y * r->pitch + px * 3;
        for (int dx = 0; dx < r->cell_w; dx++) {
          int x = px + dx;
          if (x < 0 || x >= r->width_px)
            continue;
          line[dx * 3] = br;
          line[dx * 3 + 1] = bg;
          line[dx * 3 + 2] = bb;
        }
      }

      // Debug: log bounds for rightmost column
      if (col == r->cols - 1 && row == 0) {
        log_debug("DEBUG: CELL_BG_FILL: col=%d px=%d cell_w=%d extends to px=%d (width_px=%d) cell would extend to "
                  "pixel %d%s",
                  col, px, r->cell_w, px + r->cell_w - 1, r->width_px, px + r->cell_w,
                  (px + r->cell_w > r->width_px) ? " [OVERFLOW!]" : "");
      }

      // Debug: log all characters in rightmost column
      if (col == r->cols - 1) {
        int char_code = (int)(unsigned char)cell.chars[0];
        char char_display = (char_code >= 32 && char_code < 127) ? (char)char_code : '?';
        log_debug("DEBUG: RIGHTMOST_CHAR: row=%d col=%d char=0x%02x (dec %d '%c') space=%d", row, col,
                  (unsigned char)cell.chars[0], char_code, char_display, (cell.chars[0] == ' ' ? 1 : 0));
      }

      if (cell.chars[0] && cell.chars[0] != ' ') {
        cells_with_chars++;
        // For matrix font, map ASCII characters to Private Use Area glyphs (U+E900-U+E91A)
        uint32_t char_to_render = r->is_matrix_font ? matrix_char_map(cell.chars[0]) : cell.chars[0];
        FT_UInt gi = FT_Get_Char_Index(r->ft_face, char_to_render);

        // Debug: sample cells (every 50 cols, every 5 rows) AND all cells in rightmost column
        int is_rightmost = (col == r->cols - 1);
        int is_sample = (col % 50 == 0 && row % 5 == 0);
        if (is_sample || is_rightmost) {
          log_debug("DEBUG: GLYPH_LOOKUP: row=%d col=%d char=0x%02x ('%c') → gi=%u%s", row, col, cell.chars[0],
                    (cell.chars[0] >= 32 && cell.chars[0] < 127) ? cell.chars[0] : '?', gi,
                    is_rightmost ? " [RIGHTMOST]" : "");
        }

        if (gi) {
          if (FT_Load_Glyph(r->ft_face, gi, FT_LOAD_RENDER) == 0) {
            FT_GlyphSlot g = r->ft_face->glyph;

            // Blit if we have content
            if (g->bitmap.width > 0 && g->bitmap.rows > 0) {
              // Debug: sample rendered glyphs AND all rightmost column glyphs
              if (is_sample || is_rightmost) {
                log_debug("DEBUG: GLYPH_RENDER: row=%d col=%d bitmap(%dx%d) → px=%d py=%d blit_pos(x=%d,y=%d) "
                          "offset(bmp_left=%d, baseline=%d, bmp_top=%d)%s",
                          row, col, g->bitmap.width, g->bitmap.rows, px, py, px + g->bitmap_left,
                          py + r->baseline - g->bitmap_top, g->bitmap_left, r->baseline, g->bitmap_top,
                          is_rightmost ? " [RIGHTMOST]" : "");
              }
              blit_glyph(r, &g->bitmap, px + g->bitmap_left, py + r->baseline - g->bitmap_top, fr, fg, fb, br, bg, bb);
              cells_rendered++;
            } else if (is_sample || is_rightmost) {
              log_debug("DEBUG: GLYPH_EMPTY: row=%d col=%d gi=%u has empty bitmap (%dx%d)%s", row, col, gi,
                        g->bitmap.width, g->bitmap.rows, is_rightmost ? " [RIGHTMOST]" : "");
            }
          } else if (is_sample || is_rightmost) {
            log_debug("DEBUG: GLYPH_LOAD_FAIL: row=%d col=%d gi=%u FT_Load_Glyph failed%s", row, col, gi,
                      is_rightmost ? " [RIGHTMOST]" : "");
          }
        } else if (is_sample || is_rightmost) {
          log_debug("DEBUG: GLYPH_NOT_FOUND: row=%d col=%d char=0x%02x no glyph index%s", row, col, cell.chars[0],
                    is_rightmost ? " [RIGHTMOST]" : "");
        }
      }
    }
  }

  log_info("DEBUG: RENDER_COMPLETE: cells_with_chars=%d cells_rendered=%d (grid capacity=%d)", cells_with_chars,
           cells_rendered, r->rows * r->cols);
  log_debug("DEBUG: render loop finished - processed %d cells total", r->rows * r->cols);

  // Sample pixels from framebuffer to verify content
  uint8_t *sample_top = r->framebuffer;
  uint8_t *sample_mid = r->framebuffer + (r->height_px / 2) * r->pitch;
  uint8_t *sample_bot = r->framebuffer + (r->height_px - 1) * r->pitch;

  // Enhanced pixel sampling
  log_debug("DEBUG: pixel samples:");
  log_debug("  top_left RGB(%d,%d,%d), top_mid RGB(%d,%d,%d), top_right RGB(%d,%d,%d)", sample_top[0], sample_top[1],
            sample_top[2], sample_top[r->pitch / 2], sample_top[r->pitch / 2 + 1], sample_top[r->pitch / 2 + 2],
            sample_top[(r->width_px - 1) * 3], sample_top[(r->width_px - 1) * 3 + 1],
            sample_top[(r->width_px - 1) * 3 + 2]);
  log_debug("  mid_left RGB(%d,%d,%d), mid_mid RGB(%d,%d,%d), mid_right RGB(%d,%d,%d)", sample_mid[0], sample_mid[1],
            sample_mid[2], sample_mid[r->pitch / 2], sample_mid[r->pitch / 2 + 1], sample_mid[r->pitch / 2 + 2],
            sample_mid[(r->width_px - 1) * 3], sample_mid[(r->width_px - 1) * 3 + 1],
            sample_mid[(r->width_px - 1) * 3 + 2]);
  log_debug("  bot_left RGB(%d,%d,%d), bot_mid RGB(%d,%d,%d), bot_right RGB(%d,%d,%d)", sample_bot[0], sample_bot[1],
            sample_bot[2], sample_bot[r->pitch / 2], sample_bot[r->pitch / 2 + 1], sample_bot[r->pitch / 2 + 2],
            sample_bot[(r->width_px - 1) * 3], sample_bot[(r->width_px - 1) * 3 + 1],
            sample_bot[(r->width_px - 1) * 3 + 2]);

  log_debug("term_renderer_feed: cells_with_chars=%d, cells_rendered=%d", cells_with_chars, cells_rendered);

  // Write ANSI frame size and sample bottom row characters to debug file
  FILE *dbg_dims = fopen("/tmp/render-dims.txt", "a");
  if (dbg_dims) {
    fprintf(dbg_dims, "[TERM_FEED] len=%zu, grid=%dx%d, pixels=%dx%d, cells_with_chars=%d, cells_rendered=%d\n", len,
            r->cols, r->rows, r->width_px, r->height_px, cells_with_chars, cells_rendered);

    // Sample bottom row to see what characters are there
    fprintf(dbg_dims, "  Bottom row (41): ");
    for (int col = 0; col < 20; col++) { // First 20 chars
      VTermScreenCell cell;
      vterm_screen_get_cell(r->vts, (VTermPos){r->rows - 1, col}, &cell);
      if (cell.chars[0]) {
        fprintf(dbg_dims, "%c", (cell.chars[0] >= 32 && cell.chars[0] < 127) ? cell.chars[0] : '?');
      } else {
        fprintf(dbg_dims, " ");
      }
    }
    fprintf(dbg_dims, "\n");

    // Check color of first char in bottom row
    for (int col = 0; col < r->cols; col++) {
      VTermScreenCell cell;
      vterm_screen_get_cell(r->vts, (VTermPos){r->rows - 1, col}, &cell);
      if (cell.chars[0] && cell.chars[0] != ' ') {
        int has_rgb_fg = VTERM_COLOR_IS_RGB(&cell.fg);
        int has_rgb_bg = VTERM_COLOR_IS_RGB(&cell.bg);
        fprintf(dbg_dims, "  Colors - has_rgb_fg=%d has_rgb_bg=%d\n", has_rgb_fg, has_rgb_bg);
        if (has_rgb_fg) {
          fprintf(dbg_dims, "    fg=RGB(%d,%d,%d)\n", cell.fg.rgb.red, cell.fg.rgb.green, cell.fg.rgb.blue);
        }
        if (has_rgb_bg) {
          fprintf(dbg_dims, "    bg=RGB(%d,%d,%d)\n", cell.bg.rgb.red, cell.bg.rgb.green, cell.bg.rgb.blue);
        }
        break;
      }
    }
    fflush(dbg_dims);
    fclose(dbg_dims);
  }

  log_debug("term_renderer_feed: Grid dimensions: %d cols x %d rows, Pixel dimensions: %d x %d px", r->cols, r->rows,
            r->width_px, r->height_px);

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
