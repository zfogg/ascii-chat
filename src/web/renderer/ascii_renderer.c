/**
 * @file src/web/renderer/ascii_renderer.c
 * @brief WASM ASCII renderer using libvterm, FreeType, and raylib
 *
 * This module provides real-time ASCII art rendering for the web client.
 * It uses:
 * - libvterm for ANSI escape code parsing and terminal emulation
 * - FreeType for glyph rasterization (using embedded DejaVu Sans Mono)
 * - raylib for single-texture WebGL rendering
 *
 * The render pipeline: ANSI string → libvterm cell grid → FreeType glyphs →
 * RGB framebuffer → raylib texture → single WebGL draw call
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <vterm.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <raylib.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/log.h>

/* Forward declarations for embedded font (generated at build time) */
extern const unsigned char g_font_default[];
extern const unsigned int g_font_default_size;

/* ============================================================================
 * Renderer State
 * ============================================================================ */

typedef struct {
  VTerm *vt;
  VTermScreen *vts;
  int cols, rows;

  FT_Library ft_lib;
  FT_Face ft_face;
  int cell_w, cell_h, baseline;

  uint8_t *framebuffer;  /* RGB24 buffer: framebuffer[y*pitch + x*3] = {R, G, B} */
  int width_px, height_px, pitch;

  Texture2D texture;
  bool texture_initialized;
} ascii_renderer_t;

static ascii_renderer_t renderer = {0};

/* ============================================================================
 * Utilities
 * ============================================================================ */

/* Encode 8-bit RGB to 24-bit RGB at pixel offset */
static inline void set_pixel(uint8_t *buf, int pitch, int x, int y, uint8_t r,
                              uint8_t g, uint8_t b) {
  int offset = y * pitch + x * 3;
  buf[offset + 0] = r;
  buf[offset + 1] = g;
  buf[offset + 2] = b;
}

/* Fill a rectangle with a color */
static void fill_rect(uint8_t *buf, int pitch, int x, int y, int w, int h,
                      uint8_t r, uint8_t g, uint8_t b) {
  for (int row = y; row < y + h && row < renderer.height_px; row++) {
    for (int col = x; col < x + w && col < renderer.width_px; col++) {
      set_pixel(buf, pitch, col, row, r, g, b);
    }
  }
}

/* Alpha-composite a FreeType glyph bitmap onto framebuffer */
static void blit_glyph(FT_Bitmap *bm, int px, int py, uint8_t fr,
                        uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg,
                        uint8_t bb) {
  if (!bm || bm->width == 0 || bm->rows == 0) return;

  for (int row = 0; row < (int)bm->rows && py + row < renderer.height_px;
       row++) {
    for (int col = 0; col < (int)bm->width && px + col < renderer.width_px;
         col++) {
      uint8_t alpha = bm->buffer[row * bm->pitch + col];
      if (alpha == 0) continue;

      float a = alpha / 255.0f;
      uint8_t r = (uint8_t)(fr * a + br * (1.0f - a));
      uint8_t g = (uint8_t)(fg * a + bg * (1.0f - a));
      uint8_t b = (uint8_t)(fb * a + bb * (1.0f - a));

      set_pixel(renderer.framebuffer, renderer.pitch, px + col, py + row, r,
                g, b);
    }
  }
}

/* ============================================================================
 * Exported WASM Functions
 * ============================================================================ */

/**
 * Initialize the ASCII renderer with pixel dimensions
 * @param pixel_width  Canvas width in pixels
 * @param pixel_height Canvas height in pixels
 */
void ascii_renderer_init(int pixel_width, int pixel_height) {
  if (renderer.vt) {
    log_warn("ascii_renderer_init called but renderer already initialized");
    return;
  }

  renderer.width_px = pixel_width;
  renderer.height_px = pixel_height;
  renderer.pitch = ((pixel_width * 3 + 3) / 4) * 4; /* Align to 4 bytes */

  /* Allocate framebuffer */
  renderer.framebuffer =
      SAFE_MALLOC(renderer.pitch * pixel_height, uint8_t *);
  if (!renderer.framebuffer) {
    log_error("Failed to allocate framebuffer");
    return;
  }
  memset(renderer.framebuffer, 0, renderer.pitch * pixel_height);

  /* Initialize FreeType */
  if (FT_Init_FreeType(&renderer.ft_lib)) {
    log_error("Failed to initialize FreeType");
    SAFE_FREE(renderer.framebuffer);
    return;
  }

  /* Load embedded DejaVu Sans Mono font */
  if (FT_New_Memory_Face(renderer.ft_lib, g_font_default,
                          (FT_Long)g_font_default_size, 0, &renderer.ft_face)) {
    log_error("Failed to load default font");
    FT_Done_FreeType(renderer.ft_lib);
    SAFE_FREE(renderer.framebuffer);
    return;
  }

  /* Set font size: 12pt at 96 DPI */
  FT_Set_Char_Size(renderer.ft_face, 0, 12 * 64, 96, 96);

  /* Measure cell dimensions using 'M' */
  FT_Load_Char(renderer.ft_face, 'M', FT_LOAD_RENDER);
  renderer.cell_w = renderer.ft_face->glyph->advance.x >> 6;
  renderer.cell_h = renderer.ft_face->glyph->bitmap.rows;
  renderer.baseline = (3 * renderer.cell_h) / 4;

  /* Adjust height for 2:1 aspect ratio */
  renderer.cell_h = renderer.cell_w * 2;

  renderer.cols = pixel_width / renderer.cell_w;
  renderer.rows = pixel_height / renderer.cell_h;

  log_info("ASCII renderer initialized: %dx%d pixels, %dx%d cells, font size %dx%d",
           pixel_width, pixel_height, renderer.cols, renderer.rows,
           renderer.cell_w, renderer.cell_h);

  /* Initialize libvterm */
  renderer.vt = vterm_new(renderer.rows, renderer.cols);
  if (!renderer.vt) {
    log_error("Failed to create VTerm");
    FT_Done_Face(renderer.ft_face);
    FT_Done_FreeType(renderer.ft_lib);
    SAFE_FREE(renderer.framebuffer);
    return;
  }

  renderer.vts = vterm_obtain_screen(renderer.vt);

  /* Note: raylib texture will be created on first render call (lazy init)
   * This avoids hanging on GPU operations before WebGL context is ready.
   * texture_initialized starts as false and is set after first LoadTextureFromImage. */

  log_info("ASCII renderer ready");
}

/**
 * Render an ANSI escape sequence frame
 * @param ansi_data Pointer to ANSI string data
 * @param len       Length of ANSI string in bytes
 */
void ascii_renderer_render_frame(const char *ansi_data, size_t len) {
  if (!renderer.vt || !renderer.framebuffer) {
    log_warn("ascii_renderer_render_frame: renderer not initialized");
    return;
  }

  /* Lazy-initialize raylib texture on first render
   * This ensures WebGL context is ready before GPU operations */
  if (!renderer.texture_initialized) {
    Image img = {
        .data = renderer.framebuffer,
        .width = renderer.width_px,
        .height = renderer.height_px,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
    };
    renderer.texture = LoadTextureFromImage(img);
    renderer.texture_initialized = true;
    log_debug("ascii_renderer: texture created on first render (lazy init)");
  }

  /* Clear framebuffer to black */
  memset(renderer.framebuffer, 0, renderer.pitch * renderer.height_px);

  /* Feed ANSI data to libvterm */
  vterm_input_write(renderer.vt, ansi_data, len);

  /* Rasterize each cell */
  for (int row = 0; row < renderer.rows; row++) {
    for (int col = 0; col < renderer.cols; col++) {
      VTermScreenCell cell;
      vterm_screen_get_cell(renderer.vts, (VTermPos){.row = row, .col = col},
                            &cell);

      int px = col * renderer.cell_w;
      int py = row * renderer.cell_h;

      /* Extract colors from cell */
      uint8_t fg_r = cell.fg.rgb.red;
      uint8_t fg_g = cell.fg.rgb.green;
      uint8_t fg_b = cell.fg.rgb.blue;
      uint8_t bg_r = cell.bg.rgb.red;
      uint8_t bg_g = cell.bg.rgb.green;
      uint8_t bg_b = cell.bg.rgb.blue;

      /* Fill background */
      fill_rect(renderer.framebuffer, renderer.pitch, px, py,
                renderer.cell_w, renderer.cell_h, bg_r, bg_g, bg_b);

      /* Render glyph - cell.chars[0] contains the Unicode codepoint */
      uint32_t codepoint = cell.chars[0];
      if (codepoint != 0 && codepoint != ' ') {
        FT_UInt glyph_idx = FT_Get_Char_Index(renderer.ft_face, codepoint);
        if (glyph_idx && !FT_Load_Glyph(renderer.ft_face, glyph_idx,
                                        FT_LOAD_RENDER)) {
          FT_GlyphSlot slot = renderer.ft_face->glyph;
          int glyph_x = px + slot->bitmap_left;
          int glyph_y = py + (renderer.baseline - slot->bitmap_top);

          blit_glyph(&slot->bitmap, glyph_x, glyph_y, fg_r, fg_g, fg_b,
                      bg_r, bg_g, bg_b);
        }
      }
    }
  }

  /* Update raylib texture */
  if (renderer.texture_initialized) {
    UpdateTexture(renderer.texture, renderer.framebuffer);
  }

  /* Render to canvas */
  BeginDrawing();
  ClearBackground(BLACK);
  DrawTexture(renderer.texture, 0, 0, WHITE);
  EndDrawing();
}

/**
 * Resize the renderer to new pixel dimensions
 * @param pixel_width  New canvas width in pixels
 * @param pixel_height New canvas height in pixels
 */
void ascii_renderer_resize(int pixel_width, int pixel_height) {
  if (!renderer.vt || !renderer.framebuffer) {
    log_warn("ascii_renderer_resize: renderer not initialized");
    return;
  }

  if (pixel_width == renderer.width_px && pixel_height == renderer.height_px) {
    return;
  }

  log_info("ascii_renderer_resize: %dx%d -> %dx%d", renderer.width_px,
           renderer.height_px, pixel_width, pixel_height);

  /* Free old texture */
  if (renderer.texture_initialized) {
    UnloadTexture(renderer.texture);
    renderer.texture_initialized = false;
  }

  /* Reallocate framebuffer */
  SAFE_FREE(renderer.framebuffer);
  renderer.width_px = pixel_width;
  renderer.height_px = pixel_height;
  renderer.pitch = ((pixel_width * 3 + 3) / 4) * 4;

  renderer.framebuffer =
      SAFE_MALLOC(renderer.pitch * pixel_height, uint8_t *);
  if (!renderer.framebuffer) {
    log_error("Failed to reallocate framebuffer");
    return;
  }
  memset(renderer.framebuffer, 0, renderer.pitch * pixel_height);

  /* Update terminal dimensions */
  int new_cols = pixel_width / renderer.cell_w;
  int new_rows = pixel_height / renderer.cell_h;

  if (new_cols != renderer.cols || new_rows != renderer.rows) {
    renderer.cols = new_cols;
    renderer.rows = new_rows;
    vterm_set_size(renderer.vt, new_rows, new_cols);
  }

  /* Create new texture */
  Image img = {
      .data = renderer.framebuffer,
      .width = pixel_width,
      .height = pixel_height,
      .mipmaps = 1,
      .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
  };
  renderer.texture = LoadTextureFromImage(img);
  renderer.texture_initialized = true;

  log_info("ascii_renderer_resize complete: %dx%d cells", renderer.cols,
           renderer.rows);
}

/**
 * Get the current number of columns
 * @return Number of columns (terminal width)
 */
int ascii_renderer_get_cols(void) { return renderer.cols; }

/**
 * Get the current number of rows
 * @return Number of rows (terminal height)
 */
int ascii_renderer_get_rows(void) { return renderer.rows; }

/**
 * Shutdown the ASCII renderer and free resources
 */
void ascii_renderer_shutdown(void) {
  if (renderer.texture_initialized) {
    UnloadTexture(renderer.texture);
    renderer.texture_initialized = false;
  }

  if (renderer.vt) {
    vterm_free(renderer.vt);
    renderer.vt = NULL;
  }

  if (renderer.ft_face) {
    FT_Done_Face(renderer.ft_face);
  }

  if (renderer.ft_lib) {
    FT_Done_FreeType(renderer.ft_lib);
  }

  if (renderer.framebuffer) {
    SAFE_FREE(renderer.framebuffer);
  }

  memset(&renderer, 0, sizeof(renderer));
  log_info("ASCII renderer shut down");
}
