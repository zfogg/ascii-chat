/**
 * @file media/render/terminal.c
 * @ingroup media
 * @brief Pixel renderer for render-file: libvterm + FreeType2 software compositor
 *
 * Cross-platform implementation using:
 * - libvterm: Terminal emulation without any display backend
 * - FreeType2: Glyph rasterization with uthash-based glyph cache
 * - fontconfig: Font resolution (Linux/macOS)
 * - uthash: Hash table for caching pre-rendered glyphs
 *
 * No platform-specific code (#if guards) needed — this code compiles on all platforms.
 */

#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/uthash.h>
#include <vterm.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include <string.h>
#include <stddef.h>

// ============================================================================
// Glyph Cache
// ============================================================================

/**
 * Cached glyph bitmap with metadata.
 * Stores pre-rendered FreeType bitmap to avoid rasterization on every frame.
 */
typedef struct {
  uint32_t codepoint;  // Hash key (character code)
  FT_Bitmap bitmap;    // FreeType bitmap structure with buffer pointer set to bitmap_buf
  uint8_t *bitmap_buf; // Owned bitmap data
  int bitmap_left;     // Glyph positioning offsets
  int bitmap_top;
  UT_hash_handle hh;   // uthash handle
} glyph_cache_entry_t;

/**
 * Free all entries in glyph cache.
 */
static void glyph_cache_destroy(glyph_cache_entry_t **cache) {
  glyph_cache_entry_t *curr, *tmp;
  HASH_ITER(hh, *cache, curr, tmp) {
    HASH_DEL(*cache, curr);
    SAFE_FREE(curr->bitmap_buf);
    SAFE_FREE(curr);
  }
}

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
  glyph_cache_entry_t *glyph_cache; // Per-renderer glyph cache (per-font)
};

/**
 * Lookup or create glyph cache entry.
 * If glyph is already cached in the renderer's font-specific cache, returns the cached entry.
 * Otherwise, loads and renders glyph, caches it, returns the cached entry.
 */
static glyph_cache_entry_t *glyph_cache_get(terminal_renderer_t *r, FT_Face face, uint32_t codepoint) {
  glyph_cache_entry_t *entry = NULL;

  // Lookup in renderer's per-font cache
  HASH_FIND_INT(r->glyph_cache, &codepoint, entry);
  if (entry)
    return entry;

  // Not in cache — load and render glyph
  FT_UInt gi = FT_Get_Char_Index(face, codepoint);
  if (!gi || FT_Load_Glyph(face, gi, FT_LOAD_RENDER) != 0) {
    return NULL; // Glyph not found or load failed
  }

  FT_Bitmap *src_bitmap = &face->glyph->bitmap;
  size_t bitmap_size = (size_t)src_bitmap->pitch * src_bitmap->rows;

  // Create and populate cache entry
  entry = SAFE_MALLOC(sizeof(glyph_cache_entry_t), glyph_cache_entry_t *);
  if (!entry) {
    return NULL;
  }

  FT_Bitmap cached_bitmap = *src_bitmap;
  // The copied FT_Bitmap initially points at FreeType-owned memory.
  // Reset the pointer now and repoint it to bitmap_buf after we copy the data.
  cached_bitmap.buffer = NULL;

  entry->codepoint = codepoint;
  entry->bitmap = cached_bitmap;
  entry->bitmap_buf = NULL;
  if (bitmap_size > 0 && src_bitmap->buffer) {
    entry->bitmap_buf = SAFE_MALLOC(bitmap_size, uint8_t *);
    if (!entry->bitmap_buf) {
      SAFE_FREE(entry);
      return NULL;
    }
    memcpy(entry->bitmap_buf, src_bitmap->buffer, bitmap_size);
    entry->bitmap.buffer = entry->bitmap_buf;
  }
  entry->bitmap_left = face->glyph->bitmap_left;
  entry->bitmap_top = face->glyph->bitmap_top;

  // Add to renderer's per-font hash table
  HASH_ADD_INT(r->glyph_cache, codepoint, entry);

  return entry;
}

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

      uint8_t *dst = r->framebuffer + dy * r->pitch + dx * 3; // RGB24: 3 bytes per pixel
      uint8_t r_val = (uint8_t)((fr * a + br * (255 - a)) / 255);
      uint8_t g_val = (uint8_t)((fg * a + bg * (255 - a)) / 255);
      uint8_t b_val = (uint8_t)((fb * a + bb * (255 - a)) / 255);

      dst[0] = r_val;
      dst[1] = g_val;
      dst[2] = b_val;
    }
  }
}

asciichat_error_t term_renderer_create(const term_renderer_config_t *cfg, terminal_renderer_t **out) {
  terminal_renderer_t *r = SAFE_CALLOC(1, sizeof(*r), terminal_renderer_t *);
  r->cols = cfg->cols;
  r->rows = cfg->rows;
  r->theme = cfg->theme;

  if (FT_Init_FreeType(&r->ft_lib)) {
    SAFE_FREE(r);
    return SET_ERRNO(ERROR_INIT, "FreeType init failed");
  }

  if (cfg->font_data && cfg->font_data_size > 0) {
    if (FT_New_Memory_Face(r->ft_lib, cfg->font_data, (FT_Long)cfg->font_data_size, 0, &r->ft_face)) {
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

  // Detect if this is the matrix font (needs character mapping to Private Use Area)
  r->is_matrix_font = (strstr(cfg->font_spec, "matrix") != NULL || strstr(cfg->font_spec, "Matrix") != NULL) ||
                      (cfg->font_data != NULL && GET_OPTION(matrix_rain));
  if (r->is_matrix_font) {
    log_debug("term_renderer_create: Detected matrix font - will use Private Use Area character mapping");
  }

  // Handle scalable vs bitmap fonts differently
  // Bitmap fonts (like matrix) don't respond to FT_Set_Char_Size
  // Instead, use FT_Set_Pixel_Sizes to request a specific pixel size
  if (r->ft_face->num_fixed_sizes > 0) {
    // Bitmap font: use FT_Set_Pixel_Sizes to select appropriate size (12px is reasonable for ASCII)
    int err = FT_Set_Pixel_Sizes(r->ft_face, 12, 12);
    log_debug("term_renderer_create: [BITMAP] FT_Set_Pixel_Sizes(12, 12) returned %d", err);

    // If that fails, try FT_Select_Size as fallback
    if (err != 0) {
      err = FT_Select_Size(r->ft_face, 0); // Use first available strike
      log_debug("term_renderer_create: [BITMAP FALLBACK] FT_Select_Size(0) returned %d", err);
    }
  } else {
    // Scalable font: use FT_Set_Char_Size
    // FT_Set_Char_Size takes 1/64pt units and DPI — supports fractional point sizes.
    // 96 DPI is the standard screen DPI used here; the 64 factor is the 26.6 fixed-point scale.
    int err = FT_Set_Char_Size(r->ft_face, 0, (FT_F26Dot6)(cfg->font_size_pt * 64.0), 96, 96);
    log_debug("term_renderer_create: [SCALABLE] FT_Set_Char_Size(size_pt=%.1f → %ld 1/64pt) returned %d",
              cfg->font_size_pt, (long)(cfg->font_size_pt * 64.0), err);
  }

  // For matrix fonts, try PUA glyph first (U+E900) since ASCII chars might not have bitmap data
  // For other fonts, start with 'M'
  int load_err;
  if (r->is_matrix_font) {
    log_debug("Matrix font detected: trying PUA glyph U+E900 first");
    load_err = FT_Load_Char(r->ft_face, 0xE900, FT_LOAD_RENDER);
    log_debug("FT_Load_Char(U+E900) returned %d", load_err);

    // If PUA glyph fails, try ASCII fallback
    if (load_err != 0) {
      log_debug("U+E900 failed, trying fallback character 'M'");
      load_err = FT_Load_Char(r->ft_face, 'M', FT_LOAD_RENDER);
      log_debug("FT_Load_Char('M') returned %d", load_err);
    }
  } else {
    // For non-matrix fonts, standard approach
    load_err = FT_Load_Char(r->ft_face, 'M', FT_LOAD_RENDER);
    log_debug("DEBUG: FT_Load_Char('M', FT_LOAD_RENDER) returned %d", load_err);

    // For bitmap fonts (like matrix), FT_Load_Char may fail if the character doesn't exist.
    // Try a fallback character if 'M' fails.
    if (load_err != 0 && r->ft_face->num_fixed_sizes > 0) {
      log_debug("FT_Load_Char('M') failed for bitmap font, trying fallback character 'A'");
      load_err = FT_Load_Char(r->ft_face, 'A', FT_LOAD_RENDER);
      log_debug("FT_Load_Char('A') returned %d", load_err);

      if (load_err != 0) {
        // Last resort: try a PUA glyph for unknown bitmap fonts
        log_debug("FT_Load_Char('A') also failed, trying PUA glyph U+E900");
        load_err = FT_Load_Char(r->ft_face, 0xE900, FT_LOAD_RENDER);
        log_debug("FT_Load_Char(U+E900) returned %d", load_err);
      }
    }
  }

  // For monospace ASCII grid: use advance.x (proper character spacing) and rendered height
  FT_Pos advance_x_26_6 = r->ft_face->glyph->advance.x;
  log_debug("DEBUG: advance.x (26.6pt)=%ld", advance_x_26_6);

  // Calculate cell width from font metrics
  r->cell_w = (int)(advance_x_26_6 >> 6);

  // Safeguard: for bitmap fonts, cell_w might be wrong if the font metrics are off
  // Ensure we have a reasonable minimum width
  if (r->cell_w <= 0) {
    log_warn("term_renderer_create: cell_w is %d (invalid), using default 10px", r->cell_w);
    r->cell_w = 10;
  }

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

  // Fallback: if cell_h is 0 (bitmap failed to load), use aspect ratio calculation
  if (r->cell_h <= 0) {
    log_warn("term_renderer_create: cell_h is 0 (FT_Load_Char failed or no bitmap), using aspect ratio fallback");
    r->cell_h = r->cell_w * 2;
    log_debug("FALLBACK: cell_h set to %d (2x width=%d)", r->cell_h, r->cell_w);
  }

  // Apply aspect ratio correction unless --stretch is specified
  // Terminal characters are typically 2:1 (height:width) to appear normal
  // BUT: only correct if the loaded glyph height is reasonably close (±30%)
  // This prevents huge jumps from small glyphs (e.g., 8px → 32px)
  if (!stretch_mode && r->cell_h > 0) {
    int corrected_h = r->cell_w * 2;
    int min_acceptable = (corrected_h * 70) / 100;  // 70% of target
    int max_acceptable = (corrected_h * 130) / 100; // 130% of target

    if (r->cell_h < min_acceptable || r->cell_h > max_acceptable) {
      // Loaded height is far from expected, apply correction
      log_debug("ASPECT_RATIO: Correcting cell_h from %d to %d (out of range [%d,%d], 2x width=%d)",
                r->cell_h, corrected_h, min_acceptable, max_acceptable, r->cell_w);
      r->cell_h = corrected_h;
    } else if (corrected_h != r->cell_h) {
      // Loaded height is close to expected, fine-tune by small amount only
      int delta = corrected_h - r->cell_h;
      if (delta > 4 || delta < -4) {
        // Only apply if difference is more than 4px
        log_debug("ASPECT_RATIO: Fine-tuning cell_h from %d to %d (within acceptable range, 2x width=%d)",
                  r->cell_h, corrected_h, r->cell_w);
        r->cell_h = corrected_h;
      } else {
        log_debug("ASPECT_RATIO: Keeping cell_h=%d (close to target %d, delta=%d)", r->cell_h, corrected_h, delta);
      }
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
  // Pitch in bytes: each pixel is 3 bytes (RGB24)
  // Align to 4-byte boundary for better performance
  int base_pitch = r->width_px * 3;
  r->pitch = ((base_pitch + 3) / 4) * 4; // Round up to nearest multiple of 4
  log_debug("PITCH_CALC: width_px=%d, base_pitch=%d (unaligned), final_pitch=%d (RGB24, aligned to 4-byte)", r->width_px,
            base_pitch, r->pitch);
  log_debug("DEBUG: Allocating framebuffer: %d bytes total (%d pitch * %d height)",
            (int)((size_t)r->pitch * r->height_px), r->pitch, r->height_px);
  r->framebuffer = SAFE_MALLOC((size_t)r->pitch * r->height_px, uint8_t *);

  r->vt = vterm_new(r->rows, r->cols);
  r->vts = vterm_obtain_screen(r->vt);

  // Enable UTF-8 encoding so vterm correctly parses multi-byte characters
  vterm_set_utf8(r->vt, 1);

  // Set size BEFORE reset to ensure screen is allocated with correct dimensions
  vterm_set_size(r->vt, r->rows, r->cols);
  log_debug("DEBUG: vterm_set_size called with rows=%d cols=%d", r->rows, r->cols);

  vterm_screen_set_callbacks(r->vts, &g_vterm_cbs, r);
  // Reset MUST come before any input writing to initialize state correctly
  vterm_screen_reset(r->vts, 1);

  // Verify the size was set correctly after reset
  int vt_rows, vt_cols;
  vterm_get_size(r->vt, &vt_rows, &vt_cols);
  log_debug("DEBUG: vterm_get_size returned rows=%d cols=%d (expected %d,%d)", vt_rows, vt_cols, r->rows, r->cols);

  // CRITICAL: Set size AGAIN after reset, as reset may have affected it
  vterm_set_size(r->vt, r->rows, r->cols);
  log_debug("DEBUG: vterm_set_size called AGAIN after reset");

  // Initialize per-renderer glyph cache (empty, will populate on demand)
  r->glyph_cache = NULL;

  *out = r;
  return ASCIICHAT_OK;
}

asciichat_error_t term_renderer_feed(terminal_renderer_t *r, const char *ansi_frame, size_t len) {
  // Clear framebuffer to ensure no leftover pixels from previous frames
  uint8_t def_bg = (r->theme == TERM_RENDERER_THEME_LIGHT) ? 255 : 0;
  memset(r->framebuffer, def_bg, (size_t)r->pitch * r->height_px);

  // Reset scrolling region and cursor position to prevent vertical shifts
  static const char reset_sequence[] = "\033[2J\033[H\033[r";
  vterm_input_write(r->vt, reset_sequence, sizeof(reset_sequence) - 1);

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

  static int frame_count = 0;
  static char prev_row_sigs[52][5] = {0}; // Store signatures of previous frame's rows
  frame_count++;

  // Log if any row signature changed to detect vertical shifts
  if (frame_count % 10 == 0) {
    char curr_row_sigs[52][5];
    for (int row = 0; row < r->rows && row < 52; row++) {
      // Get first 4 chars as signature
      for (int col = 0; col < 4 && col < r->cols; col++) {
        VTermScreenCell cell;
        vterm_screen_get_cell(r->vts, (VTermPos){row, col}, &cell);
        curr_row_sigs[row][col] = (cell.chars[0] >= 32 && cell.chars[0] <= 126) ? cell.chars[0] : '.';
      }
      curr_row_sigs[row][4] = 0;
    }
    memcpy(prev_row_sigs, curr_row_sigs, sizeof(curr_row_sigs));
  }

  int glyph_rendered_count = 0;
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
        uint8_t *line = r->framebuffer + y * r->pitch + px * 3; // RGB24: 3 bytes per pixel
        for (int dx = 0; dx < r->cell_w; dx++) {
          int x = px + dx;
          if (x < 0 || x >= r->width_px)
            continue;
          line[dx * 3] = br;
          line[dx * 3 + 1] = bg;
          line[dx * 3 + 2] = bb;
        }
      }

      if (cell.chars[0] && cell.chars[0] != ' ') {
        // For matrix font, map ASCII characters to Private Use Area glyphs (U+E900-U+E91A)
        uint32_t char_to_render = r->is_matrix_font ? matrix_char_map(cell.chars[0]) : cell.chars[0];

        // Use per-renderer glyph cache to avoid rasterizing every glyph every frame
        glyph_cache_entry_t *cache_entry = glyph_cache_get(r, r->ft_face, char_to_render);
        if (cache_entry && cache_entry->bitmap.width > 0 && cache_entry->bitmap.rows > 0) {
          blit_glyph(r, &cache_entry->bitmap, px + cache_entry->bitmap_left, py + r->baseline - cache_entry->bitmap_top,
                      fr, fg, fb, br, bg, bb);
          glyph_rendered_count++;
        }
      }
    }
  }
  log_info("term_renderer_feed: Rendered %d glyphs out of %d cells", glyph_rendered_count, r->rows * r->cols);

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
  glyph_cache_destroy(&r->glyph_cache);
  SAFE_FREE(r);
}
