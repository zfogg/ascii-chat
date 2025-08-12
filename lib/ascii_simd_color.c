#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "ascii_simd.h"
#include "options.h"
#include "common.h"
#include "image.h"
#include "buffer_pool.h"

/* ============================================================================
 * SIMD-Optimized Colored ASCII Generation
 *
 * This extends the basic SIMD luminance conversion to include full
 * ANSI color code generation for maximum performance.
 * ============================================================================
 */

// Luminance calculation constants (matches ascii_simd.c)
#define LUMA_RED 77    // 0.299 * 256
#define LUMA_GREEN 150 // 0.587 * 256
#define LUMA_BLUE 29   // 0.114 * 256

// ASCII palette and luminance palette (duplicated from ascii_simd.c for now)
static const char ascii_palette_color[] = "   ...',;:clodxkO0KXNWM";
static const int palette_len_color = sizeof(ascii_palette_color) - 2;

static char luminance_palette_color[256];
static bool palette_initialized_color = false;

static void init_palette(void) {
  if (palette_initialized_color)
    return;

  for (int i = 0; i < 256; i++) {
    int palette_index = (i * palette_len_color) / 255;
    if (palette_index > palette_len_color)
      palette_index = palette_len_color;
    luminance_palette_color[i] = ascii_palette_color[palette_index];
  }
  palette_initialized_color = true;
}

#define luminance_palette luminance_palette_color

// Pre-computed ANSI escape code templates
// static const char ANSI_FG_PREFIX[] = "\033[38;2;";  // Unused, replaced by inline constants
// static const char ANSI_BG_PREFIX[] = "\033[48;2;";  // Unused, replaced by inline constants
// static const char ANSI_SUFFIX[] = "m";
static const char ANSI_RESET[] = "\033[0m";

// -------- precomputed decimal strings for 0..255 --------

typedef struct {
  uint8_t len; // 1..3
  char s[3];   // digits only, no terminator
} dec3_t;

static dec3_t g_dec3[256];
static bool g_dec3_init = false;

static void init_dec3(void) {
  if (g_dec3_init)
    return;
  for (int v = 0; v < 256; ++v) {
    int d2 = v / 100;     // 0..2
    int r = v - d2 * 100; // 0..99
    int d1 = r / 10;      // 0..9
    int d0 = r - d1 * 10; // 0..9

    if (d2) {
      g_dec3[v].len = 3;
      g_dec3[v].s[0] = '0' + d2;
      g_dec3[v].s[1] = '0' + d1;
      g_dec3[v].s[2] = '0' + d0;
    } else if (d1) {
      g_dec3[v].len = 2;
      g_dec3[v].s[0] = '0' + d1;
      g_dec3[v].s[1] = '0' + d0;
    } else {
      g_dec3[v].len = 1;
      g_dec3[v].s[0] = '0' + d0;
    }
  }
  g_dec3_init = true;
}

// -------- ultra-fast SGR builders with size calculation --------

// Calculate exact size needed for SGR sequences (for security)
static inline size_t calculate_sgr_truecolor_fg_size(uint8_t r, uint8_t g, uint8_t b) {
  init_dec3();
  return 7 + g_dec3[r].len + 1 + g_dec3[g].len + 1 + g_dec3[b].len + 1; // "\033[38;2;" + R + ";" + G + ";" + B + "m"
}

static inline size_t calculate_sgr_truecolor_bg_size(uint8_t r, uint8_t g, uint8_t b) __attribute__((unused)) {
  init_dec3();
  return 7 + g_dec3[r].len + 1 + g_dec3[g].len + 1 + g_dec3[b].len + 1; // "\033[48;2;" + R + ";" + G + ";" + B + "m"
}

static inline size_t calculate_sgr_truecolor_fg_bg_size(uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg,
                                                        uint8_t bb) {
  init_dec3();
  return 7 + g_dec3[fr].len + 1 + g_dec3[fg].len + 1 + g_dec3[fb].len + 6 + g_dec3[br].len + 1 + g_dec3[bg].len + 1 +
         g_dec3[bb].len + 1; // Combined FG+BG
}

static inline char *append_sgr_reset(char *dst) {
  // "\x1b[0m"
  static const char RESET[] = "\033[0m";
  memcpy(dst, RESET, sizeof(RESET) - 1);
  return dst + (sizeof(RESET) - 1);
}

// \x1b[38;2;R;G;Bm
static inline char *append_sgr_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  init_dec3();
  static const char PFX[] = "\033[38;2;";
  memcpy(dst, PFX, sizeof(PFX) - 1);
  dst += sizeof(PFX) - 1;

  memcpy(dst, g_dec3[r].s, g_dec3[r].len);
  dst += g_dec3[r].len;
  *dst++ = ';';
  memcpy(dst, g_dec3[g].s, g_dec3[g].len);
  dst += g_dec3[g].len;
  *dst++ = ';';
  memcpy(dst, g_dec3[b].s, g_dec3[b].len);
  dst += g_dec3[b].len;
  *dst++ = 'm';
  return dst;
}

// \x1b[48;2;R;G;Bm
static inline char *append_sgr_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  init_dec3();
  static const char PFX[] = "\033[48;2;";
  memcpy(dst, PFX, sizeof(PFX) - 1);
  dst += sizeof(PFX) - 1;

  memcpy(dst, g_dec3[r].s, g_dec3[r].len);
  dst += g_dec3[r].len;
  *dst++ = ';';
  memcpy(dst, g_dec3[g].s, g_dec3[g].len);
  dst += g_dec3[g].len;
  *dst++ = ';';
  memcpy(dst, g_dec3[b].s, g_dec3[b].len);
  dst += g_dec3[b].len;
  *dst++ = 'm';
  return dst;
}

// \x1b[38;2;R;G;B;48;2;r;g;bm   (one sequence for both FG and BG)
static inline char *append_sgr_truecolor_fg_bg(char *dst, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg,
                                               uint8_t bb) {
  init_dec3();
  static const char FG[] = "\033[38;2;";
  static const char BG[] = ";48;2;";
  memcpy(dst, FG, sizeof(FG) - 1);
  dst += sizeof(FG) - 1;

  memcpy(dst, g_dec3[fr].s, g_dec3[fr].len);
  dst += g_dec3[fr].len;
  *dst++ = ';';
  memcpy(dst, g_dec3[fg].s, g_dec3[fg].len);
  dst += g_dec3[fg].len;
  *dst++ = ';';
  memcpy(dst, g_dec3[fb].s, g_dec3[fb].len);
  dst += g_dec3[fb].len;

  memcpy(dst, BG, sizeof(BG) - 1);
  dst += sizeof(BG) - 1;
  memcpy(dst, g_dec3[br].s, g_dec3[br].len);
  dst += g_dec3[br].len;
  *dst++ = ';';
  memcpy(dst, g_dec3[bg].s, g_dec3[bg].len);
  dst += g_dec3[bg].len;
  *dst++ = ';';
  memcpy(dst, g_dec3[bb].s, g_dec3[bb].len);
  dst += g_dec3[bb].len;

  *dst++ = 'm';
  return dst;
}

// Legacy wrapper functions for backward compatibility
static inline int generate_ansi_fg(uint8_t r, uint8_t g, uint8_t b, char *dst) {
  char *result = append_sgr_truecolor_fg(dst, r, g, b);
  return (int)(result - dst);
}

static inline int generate_ansi_bg(uint8_t r, uint8_t g, uint8_t b, char *dst) {
  char *result = append_sgr_truecolor_bg(dst, r, g, b);
  return (int)(result - dst);
}

// Optimized row renderer with run-length encoding and FG+BG combined sequences
size_t render_row_truecolor_ascii_runlength(const rgb_pixel_t *row, int width, char *dst, size_t cap,
                                            bool background_mode);

// ----------------
char *image_print_colored_simd(image_t *image) {
  // Calculate exact maximum buffer size with precise per-pixel bounds
  const int h = image->h;
  const int w = image->w;

  // Exact per-pixel maximums (with run-length encoding this will be much smaller in practice)
  const size_t per_px = opt_background_color ? 39 : 20; // Worst case per pixel
  const size_t reset_len = 4;                           // \033[0m

  const size_t h_sz = (size_t)h;
  const size_t w_sz = (size_t)w;
  const size_t total_resets = h_sz * reset_len;
  const size_t total_newlines = (h_sz > 0) ? (h_sz - 1) : 0;
  const size_t lines_size = w_sz * h_sz * per_px + total_resets + total_newlines + 1;

  // Single allocation - no buffer pool overhead, no copying!
  char *ascii;
  SAFE_MALLOC(ascii, lines_size, char *);
  if (!ascii) {
    log_error("Memory allocation failed: %zu bytes", lines_size);
    return NULL;
  }

  // Note: The new render_row_truecolor_ascii_runlength() function handles
  // everything internally and doesn't need separate ASCII character buffers

  // The run-length encoding implementation is now the primary optimized path
  // Future: streaming NEON can be added here for very large images

  // Process directly into final buffer using optimized run-length encoding
  size_t total_len = 0;
  for (int y = 0; y < h; y++) {
    // Debug assertion: ensure we have enough space
    const size_t row_max = w * per_px + reset_len;
    assert(total_len + row_max <= lines_size);

    // Use the NEW optimized run-length encoding function with combined SGR sequences
    size_t row_len = render_row_truecolor_ascii_runlength(
        (const rgb_pixel_t *)&image->pixels[y * w], w, ascii + total_len, lines_size - total_len, opt_background_color);
    total_len += row_len;

    // Add newline after each row (except the last row)
    if (y != h - 1 && total_len < lines_size - 1) {
      ascii[total_len++] = '\n';
    }
  }
  ascii[total_len] = '\0';

  return ascii;
}

/* ============================================================================
 * SIMD + Colored ASCII: Complete Row Processing
 * ============================================================================
 */

// Note: All the old slow convert_row_with_color_* functions have been removed.
// We now use the optimized render_row_truecolor_ascii_runlength() function instead.

#ifdef SIMD_SUPPORT_AVX2
// Process entire row with SIMD luminance + optimized color generation - OPTIMIZED (no buffer pool)
size_t convert_row_with_color_avx2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {

  // Use stack allocation for small widths, heap for large
  char stack_ascii_chars[2048]; // Stack buffer for typical terminal widths
  char *ascii_chars = stack_ascii_chars;
  bool heap_allocated = false;

  if (width > 2048) {
    // Only use heap allocation for very wide images
    SAFE_MALLOC(ascii_chars, width, char *);
    heap_allocated = true;
  }

  // Step 1: SIMD luminance conversion - NO BUFFER POOL!
  convert_pixels_avx2(pixels, ascii_chars, width);

  // Step 2: Generate colored output
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining = buffer_end - current_pos;
  if (remaining >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // Clean up heap allocation if used
  if (heap_allocated) {
    SAFE_FREE(ascii_chars);
  }

  return current_pos - output_buffer;
}

// AVX2 version with pre-allocated buffer to reduce buffer pool contention
size_t convert_row_with_color_avx2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars) {
  // Step 1: Use provided buffer for SIMD luminance conversion
  convert_pixels_avx2(pixels, ascii_chars, width);

  // Step 2: Generate colored output (same as regular AVX2 version)
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining2 = buffer_end - current_pos;
  if (remaining2 >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // No buffer pool free - using pre-allocated buffer
  return current_pos - output_buffer;
}
#endif

#ifdef SIMD_SUPPORT_SSE2
// SSE2 version for older Intel/AMD systems - OPTIMIZED (no buffer pool)
size_t convert_row_with_color_sse2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {

  // Use stack allocation for small widths, heap for large
  char stack_ascii_chars[2048]; // Stack buffer for typical terminal widths
  char *ascii_chars = stack_ascii_chars;
  bool heap_allocated = false;

  if (width > 2048) {
    // Only use heap allocation for very wide images
    SAFE_MALLOC(ascii_chars, width, char *);
    heap_allocated = true;
  }

  // Step 1: SIMD luminance conversion - NO BUFFER POOL!
  convert_pixels_sse2(pixels, ascii_chars, width);

  // Step 2: Generate colored output
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining = buffer_end - current_pos;
  if (remaining >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // Clean up heap allocation if used
  if (heap_allocated) {
    SAFE_FREE(ascii_chars);
  }

  return current_pos - output_buffer;
}

// SSE2 version with pre-allocated buffer to reduce buffer pool contention
size_t convert_row_with_color_sse2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars) {
  // Step 1: Use provided buffer for SIMD luminance conversion
  convert_pixels_sse2(pixels, ascii_chars, width);

  // Step 2: Generate colored output (same as regular SSE2 version)
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining2 = buffer_end - current_pos;
  if (remaining2 >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // No buffer pool free - using pre-allocated buffer
  return current_pos - output_buffer;
}
#endif

#ifdef SIMD_SUPPORT_NEON
#include <arm_neon.h>

// STREAMING NEON implementation - TRUE SIMD with single-pass processing
size_t convert_row_with_color_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {
  init_palette();
  init_dec3();

  char *p = output_buffer;
  char *end = output_buffer + buffer_size;
  int x = 0;

  // Process 16-pixel chunks with TRUE NEON SIMD
  for (; x + 15 < width; x += 16) {
    const uint8_t *base = (const uint8_t *)(pixels + x);

    // Load 16 interleaved RGB pixels → 3 x 16-byte vectors (TRUE NEON!)
    uint8x16x3_t rgb = vld3q_u8(base);

    // Widen to 16-bit for accurate dot product
    uint16x8_t r_lo = vmovl_u8(vget_low_u8(rgb.val[0]));
    uint16x8_t r_hi = vmovl_u8(vget_high_u8(rgb.val[0]));
    uint16x8_t g_lo = vmovl_u8(vget_low_u8(rgb.val[1]));
    uint16x8_t g_hi = vmovl_u8(vget_high_u8(rgb.val[1]));
    uint16x8_t b_lo = vmovl_u8(vget_low_u8(rgb.val[2]));
    uint16x8_t b_hi = vmovl_u8(vget_high_u8(rgb.val[2]));

    // SIMD luminance: y = (77*r + 150*g + 29*b) >> 8 (fits in 16-bit)
    uint16x8_t y_lo = vmulq_n_u16(r_lo, LUMA_RED);
    y_lo = vmlaq_n_u16(y_lo, g_lo, LUMA_GREEN);
    y_lo = vmlaq_n_u16(y_lo, b_lo, LUMA_BLUE);
    y_lo = vshrq_n_u16(y_lo, 8);

    uint16x8_t y_hi = vmulq_n_u16(r_hi, LUMA_RED);
    y_hi = vmlaq_n_u16(y_hi, g_hi, LUMA_GREEN);
    y_hi = vmlaq_n_u16(y_hi, b_hi, LUMA_BLUE);
    y_hi = vshrq_n_u16(y_hi, 8);

    // Narrow to 8-bit luminance values
    uint8x16_t y8 = vcombine_u8(vqmovn_u16(y_lo), vqmovn_u16(y_hi));

    // Extract luminance values for immediate use
    uint8_t lum[16];
    vst1q_u8(lum, y8);

    // STREAMING: Emit 16 pixels immediately (no staging buffers!)
    for (int k = 0; k < 16; ++k) {
      const rgb_pixel_t *px = &pixels[x + k];

      // Exact space check prevents buffer overflow
      if ((size_t)(end - p) < (background_mode ? 40 : 24))
        goto done;

      if (background_mode) {
        uint8_t fg = (lum[k] < 127) ? 255 : 0;
        p = append_sgr_truecolor_fg_bg(p, fg, fg, fg, px->r, px->g, px->b);
      } else {
        p = append_sgr_truecolor_fg(p, px->r, px->g, px->b);
      }
      *p++ = luminance_palette[lum[k]];
    }
  }

  // Process remaining pixels (< 16) with scalar fallback
  for (; x < width; ++x) {
    const rgb_pixel_t *px = &pixels[x];
    int y = (LUMA_RED * px->r + LUMA_GREEN * px->g + LUMA_BLUE * px->b) >> 8;

    if ((size_t)(end - p) < (background_mode ? 40 : 24))
      break;

    if (background_mode) {
      uint8_t fg = (y < 127) ? 255 : 0;
      p = append_sgr_truecolor_fg_bg(p, fg, fg, fg, px->r, px->g, px->b);
    } else {
      p = append_sgr_truecolor_fg(p, px->r, px->g, px->b);
    }
    *p++ = luminance_palette[y];
  }

done:
  // Add reset sequence
  if ((size_t)(end - p) >= 4)
    p = append_sgr_reset(p);

  return (size_t)(p - output_buffer);
}
#endif

// Scalar version for comparison
size_t convert_row_with_color_scalar(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                     bool background_mode) {

  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];

    // Calculate luminance (scalar)
    int luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
    if (luminance > 255)
      luminance = 255;

    // Get ASCII character
    static const char palette[] = " .,:;ox%#@";
    char ascii_char = palette[luminance * (sizeof(palette) - 2) / 255];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break;

    if (background_mode) {
      uint8_t fg_color = (luminance < 127) ? 255 : 0;
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;
      *current_pos++ = ascii_char;
    } else {
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Reset sequence
  size_t remaining = buffer_end - current_pos;
  if (remaining >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  return current_pos - output_buffer;
}

// Scalar version with pre-allocated buffer to reduce buffer pool contention
size_t convert_row_with_color_scalar_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                                 int width, bool background_mode, char *ascii_chars) {
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  // Step 1: Generate ASCII characters using provided buffer
  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];

    // Calculate luminance (scalar)
    int luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
    if (luminance > 255)
      luminance = 255;

    // Get ASCII character
    static const char palette[] = " .,:;ox%#@";
    ascii_chars[x] = palette[luminance * (sizeof(palette) - 2) / 255];
  }

  // Step 2: Generate colored output
  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break;

    if (background_mode) {
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;
      *current_pos++ = ascii_char;
    } else {
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Reset sequence
  size_t remaining2 = buffer_end - current_pos;
  if (remaining2 >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // No buffer pool free - using pre-allocated buffer
  return current_pos - output_buffer;
}

// ACTUAL SIMD-optimized color conversion - uses SIMD for luminance, then fast ANSI generation
size_t convert_row_with_color_optimized(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                        bool background_mode) {
#ifdef SIMD_SUPPORT_NEON
  return convert_row_with_color_neon(pixels, output_buffer, buffer_size, width, background_mode);
#elif defined(SIMD_SUPPORT_AVX2)
  return convert_row_with_color_avx2(pixels, output_buffer, buffer_size, width, background_mode);
#elif defined(SIMD_SUPPORT_SSE2)
  return convert_row_with_color_sse2(pixels, output_buffer, buffer_size, width, background_mode);
#else
  return convert_row_with_color_scalar(pixels, output_buffer, buffer_size, width, background_mode);
#endif
}

/* ============================================================================
 * Run-Length Encoding Color Optimization
 * ============================================================================
 */

// Build one colored ASCII row with FG only or FG+BG, run-length colors.
size_t render_row_truecolor_ascii_runlength(const rgb_pixel_t *row, int width, char *dst, size_t cap,
                                            bool background_mode) {
  init_palette();
  init_dec3();

  char *p = dst;
  char *end = dst + cap;

  bool have_color = false;
  uint8_t cr = 0, cg = 0, cb = 0; // current foreground color
  uint8_t br = 0;                 // current fg brightness (for background mode)

  for (int x = 0; x < width; ++x) {
    const rgb_pixel_t *px = &row[x];

    // Calculate luminance for ASCII character selection
    int y = (LUMA_RED * px->r + LUMA_GREEN * px->g + LUMA_BLUE * px->b) >> 8;
    char ch = luminance_palette[y];

    if (background_mode) {
      // Background mode: Use pixel color as background, contrasting foreground
      uint8_t fg_val = (y < 127) ? 255 : 0; // White text on dark bg, black on light bg

      if (!have_color || px->r != cr || px->g != cg || px->b != cb || fg_val != br) {
        // Color changed - emit combined FG+BG sequence
        size_t sgr_size = calculate_sgr_truecolor_fg_bg_size(fg_val, fg_val, fg_val, px->r, px->g, px->b);
        if ((size_t)(end - p) < sgr_size + 1) // +1 for ASCII character
          break;                              // Exact space check prevents buffer overflow
        p = append_sgr_truecolor_fg_bg(p, fg_val, fg_val, fg_val, px->r, px->g, px->b);
        cr = px->r;
        cg = px->g;
        cb = px->b;
        br = fg_val;
        have_color = true;
      }
    } else {
      // Foreground mode: Use pixel color as foreground
      if (!have_color || px->r != cr || px->g != cg || px->b != cb) {
        // Color changed - emit foreground sequence
        size_t sgr_size = calculate_sgr_truecolor_fg_size(px->r, px->g, px->b);
        if ((size_t)(end - p) < sgr_size + 1) // +1 for ASCII character
          break;                              // Exact space check prevents buffer overflow
        p = append_sgr_truecolor_fg(p, px->r, px->g, px->b);
        cr = px->r;
        cg = px->g;
        cb = px->b;
        have_color = true;
      }
    }

    if (p == end)
      break;
    *p++ = ch;
  }

  // Reset sequence (but no newline - let the caller handle that)
  if (end - p >= 4)
    p = append_sgr_reset(p);
  return (size_t)(p - dst);
}

/* ============================================================================
 * Upper Half Block (▀) Renderer - 2x Vertical Density
 * ============================================================================
 */

// Render two image rows as one terminal row using ▀ character
// FG = top pixel color, BG = bottom pixel color = 2x vertical resolution!
size_t render_row_upper_half_block(const rgb_pixel_t *top_row, const rgb_pixel_t *bottom_row, int width, 
                                   char *dst, size_t cap) {
  init_dec3();
  
  char *p = dst;
  char *end = dst + cap;
  
  // Current colors for run-length encoding
  bool have_color = false;
  uint8_t fg_r = 0, fg_g = 0, fg_b = 0;  // Foreground (top pixel)
  uint8_t bg_r = 0, bg_g = 0, bg_b = 0;  // Background (bottom pixel)
  
  for (int x = 0; x < width; ++x) {
    const rgb_pixel_t *top_px = &top_row[x];
    const rgb_pixel_t *bot_px = &bottom_row[x];
    
    // Check if colors changed (need new escape sequence)
    bool color_changed = !have_color || 
                         top_px->r != fg_r || top_px->g != fg_g || top_px->b != fg_b ||
                         bot_px->r != bg_r || bot_px->g != bg_g || bot_px->b != bg_b;
    
    if (color_changed) {
      // Calculate exact space needed for combined FG+BG sequence + UTF-8 ▀ (3 bytes) 
      size_t sgr_size = calculate_sgr_truecolor_fg_bg_size(top_px->r, top_px->g, top_px->b,
                                                           bot_px->r, bot_px->g, bot_px->b);
      if ((size_t)(end - p) < sgr_size + 3) // +3 for UTF-8 ▀ character
        break;
      
      // Emit combined FG+BG escape sequence
      p = append_sgr_truecolor_fg_bg(p, top_px->r, top_px->g, top_px->b,
                                        bot_px->r, bot_px->g, bot_px->b);
      
      // Update color state
      fg_r = top_px->r; fg_g = top_px->g; fg_b = top_px->b;
      bg_r = bot_px->r; bg_g = bot_px->g; bg_b = bot_px->b;
      have_color = true;
    }
    
    // Add ▀ character (UTF-8: 0xE2 0x96 0x80)
    if (p + 3 <= end) {
      *p++ = 0xE2;
      *p++ = 0x96; 
      *p++ = 0x80;
    } else {
      break;
    }
  }
  
  // Add reset sequence
  if ((size_t)(end - p) >= 4)
    p = append_sgr_reset(p);
    
  return (size_t)(p - dst);
}

// Full image renderer using ▀ blocks (half-height output)
char *image_print_half_height_blocks(image_t *image) {
  const int h = image->h;
  const int w = image->w;
  
  // Output height is half the input height (rounded up)
  const int output_height = (h + 1) / 2;
  
  // Calculate buffer size: each ▀ needs combined FG+BG escape (up to ~45 bytes) + UTF-8 ▀ (3 bytes)
  const size_t max_per_char = 48;  // Conservative estimate
  const size_t reset_len = 4;      // \033[0m
  const size_t total_newlines = (output_height > 0) ? (output_height - 1) : 0;
  const size_t buffer_size = output_height * w * max_per_char + output_height * reset_len + total_newlines + 1;
  
  // Single allocation
  char *ascii;
  SAFE_MALLOC(ascii, buffer_size, char *);
  if (!ascii) {
    log_error("Memory allocation failed: %zu bytes", buffer_size);
    return NULL;
  }
  
  size_t total_len = 0;
  
  // Process pairs of rows
  for (int y = 0; y < output_height; y++) {
    int top_row_idx = y * 2;
    int bottom_row_idx = top_row_idx + 1;
    
    const rgb_pixel_t *top_row = (const rgb_pixel_t *)&image->pixels[top_row_idx * w];
    const rgb_pixel_t *bottom_row;
    
    // Handle odd heights - use the same row for both top and bottom
    if (bottom_row_idx >= h) {
      bottom_row = top_row;  // Duplicate last row for odd heights
    } else {
      bottom_row = (const rgb_pixel_t *)&image->pixels[bottom_row_idx * w];
    }
    
    // Render this terminal row using ▀ blocks
    size_t row_len = render_row_upper_half_block(top_row, bottom_row, w, 
                                                ascii + total_len, buffer_size - total_len);
    total_len += row_len;
    
    // Add newline after each row (except the last row)
    if (y != output_height - 1 && total_len < buffer_size - 1) {
      ascii[total_len++] = '\n';
    }
  }
  
  ascii[total_len] = '\0';
  return ascii;
}
