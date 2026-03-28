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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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
  bool window_initialized;
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
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
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

  /* Initialize raylib window on first render (lazy init)
   * This avoids blocking on InitWindow during initialization */

}

/**
 * Render an ANSI escape sequence frame
 * @param ansi_data Pointer to ANSI string data
 * @param len       Length of ANSI string in bytes
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ascii_renderer_render_frame(const char *ansi_data, int len) {
  log_debug("[ascii_renderer_render_frame] Called with ansi_data=%p, len=%d", ansi_data, len);

  if (!renderer.vt || !renderer.framebuffer) {
    log_warn("ascii_renderer_render_frame: renderer not initialized");
    return;
  }

  if (!ansi_data || len <= 0) {
    log_warn("ascii_renderer_render_frame: empty ANSI input");
    return;
  }

  /* Feed ANSI data to vterm for parsing and cell state updates */
  vterm_input_write(renderer.vt, ansi_data, len);

  /* Clear framebuffer to black background */
  memset(renderer.framebuffer, 0, renderer.pitch * renderer.height_px);

  /* Render each cell from the vterm screen state */
  for (int row = 0; row < renderer.rows; row++) {
    for (int col = 0; col < renderer.cols; col++) {
      VTermPos pos = {row, col};
      VTermScreenCell cell;
      vterm_screen_get_cell(renderer.vts, pos, &cell);

      /* Skip empty cells */
      if (cell.chars[0] == 0) {
        continue;
      }

      /* Calculate pixel coordinates for this cell */
      int cell_x = col * renderer.cell_w;
      int cell_y = row * renderer.cell_h;

      /* Extract background color */
      uint8_t bg_r = 0, bg_g = 0, bg_b = 0;
      if (VTERM_COLOR_IS_RGB(&cell.bg)) {
        bg_r = cell.bg.rgb.red;
        bg_g = cell.bg.rgb.green;
        bg_b = cell.bg.rgb.blue;
      }

      /* Fill cell background */
      fill_rect(renderer.framebuffer, renderer.pitch, cell_x, cell_y,
                renderer.cell_w, renderer.cell_h, bg_r, bg_g, bg_b);

      /* Extract foreground color (default white) */
      uint8_t fg_r = 255, fg_g = 255, fg_b = 255;
      if (VTERM_COLOR_IS_RGB(&cell.fg)) {
        fg_r = cell.fg.rgb.red;
        fg_g = cell.fg.rgb.green;
        fg_b = cell.fg.rgb.blue;
      }

      /* Load and render glyph */
      uint32_t codepoint = cell.chars[0];
      if (FT_Load_Char(renderer.ft_face, (unsigned long)codepoint, 4)) {  /* FT_LOAD_RENDER = 4 */
        continue;
      }

      /* Get glyph bitmap */
      FT_Glyph glyph;
      if (FT_Get_Glyph(renderer.ft_face->glyph, &glyph)) {
        continue;
      }

      FT_BitmapGlyph bitmap_glyph = (FT_BitmapGlyph)glyph;

      /* Calculate glyph position with baseline adjustment */
      int glyph_x = cell_x + bitmap_glyph->left;
      int glyph_y = cell_y + renderer.baseline - bitmap_glyph->top;

      /* Composite glyph onto framebuffer */
      blit_glyph(&bitmap_glyph->bitmap, glyph_x, glyph_y,
                  fg_r, fg_g, fg_b, bg_r, bg_g, bg_b);

      FT_Done_Glyph(glyph);
    }
  }

  /* Note: In WASM, texture update would happen here but raylib
   * requires a window context which isn't available in browser.
   * The framebuffer is maintained for potential future use.
   * The actual rendering on screen happens via xterm.js on the JS side. */
}

/**
 * Resize the renderer to new pixel dimensions
 * @param pixel_width  New canvas width in pixels
 * @param pixel_height New canvas height in pixels
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
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

  /* Note: Texture management removed for WASM - no window context in browser */

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

  /* Note: Texture creation removed for WASM - no window context in browser.
   * Framebuffer is maintained for JavaScript to read via getter functions. */

  log_info("ascii_renderer_resize complete: %dx%d cells", renderer.cols,
           renderer.rows);
}

/**
 * Get the current number of columns
 * @return Number of columns (terminal width)
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int ascii_renderer_get_cols(void) {
  return renderer.cols;
}

/**
 * Get the current number of rows
 * @return Number of rows (terminal height)
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int ascii_renderer_get_rows(void) {
  return renderer.rows;
}

/**
 * Get pointer to the framebuffer for JavaScript access
 * @return Pointer to RGB24 framebuffer data
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
const uint8_t* ascii_renderer_get_framebuffer(void) {
  return renderer.framebuffer;
}

/**
 * Get framebuffer width in pixels
 * @return Width in pixels
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int ascii_renderer_get_framebuffer_width(void) {
  return renderer.width_px;
}

/**
 * Get framebuffer height in pixels
 * @return Height in pixels
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int ascii_renderer_get_framebuffer_height(void) {
  return renderer.height_px;
}

/**
 * Get framebuffer stride (pitch) in bytes
 * @return Stride in bytes (width * 3, padded to 4-byte alignment)
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int ascii_renderer_get_framebuffer_stride(void) {
  return renderer.pitch;
}

/**
 * Shutdown the ASCII renderer and free resources
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ascii_renderer_shutdown(void) {
  /* Note: Texture cleanup removed for WASM - no window context in browser */

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
