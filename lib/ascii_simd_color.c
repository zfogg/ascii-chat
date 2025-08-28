#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "ascii_simd.h"
#include "options.h"
#include "common.h"
#include "image.h"

/* ============================================================================
 * SIMD-Optimized Colored ASCII Generation
 *
 * This extends the basic SIMD luminance conversion to include full
 * ANSI color code generation for maximum performance.
 * ============================================================================
 */

// Definitions are in ascii_simd.h - just use them
#define luminance_palette g_ascii_cache.luminance_palette

// Background ASCII luminance threshold - same as NEON version
#ifndef BGASCII_LUMA_THRESHOLD
#define BGASCII_LUMA_THRESHOLD 128 // Y >= 128 -> black text; else white text
#endif

/* ============================================================================
 * OPTIMIZATION #4: 256-Color Mode with Precomputed FG+BG Strings (~1.5MB cache)
 * ============================================================================
 */

// Precomputed 256-color SGR strings - ~1.5-2MB total but HUGE speed win
typedef struct {
  char str[32]; // Max: "\e[38;5;255;48;5;255m" = 20 chars
  uint8_t len;
} sgr256_t;

static sgr256_t g_sgr256_fgbg[256][256]; // 65,536 combinations
static bool g_sgr256_initialized = false;

static void init_sgr256_cache(void) {
  if (g_sgr256_initialized)
    return;

  // Precompute all 65,536 FG+BG combinations
  for (int fg = 0; fg < 256; fg++) {
    for (int bg = 0; bg < 256; bg++) {
      char *p = g_sgr256_fgbg[fg][bg].str;
      *p++ = '\033';
      *p++ = '[';
      *p++ = '3';
      *p++ = '8';
      *p++ = ';';
      *p++ = '5';
      *p++ = ';';

      // FG color
      if (fg < 10) {
        *p++ = '0' + fg;
      } else if (fg < 100) {
        *p++ = '0' + (fg / 10);
        *p++ = '0' + (fg % 10);
      } else {
        *p++ = '0' + (fg / 100);
        *p++ = '0' + ((fg / 10) % 10);
        *p++ = '0' + (fg % 10);
      }

      *p++ = ';';
      *p++ = '4';
      *p++ = '8';
      *p++ = ';';
      *p++ = '5';
      *p++ = ';';

      // BG color
      if (bg < 10) {
        *p++ = '0' + bg;
      } else if (bg < 100) {
        *p++ = '0' + (bg / 10);
        *p++ = '0' + (bg % 10);
      } else {
        *p++ = '0' + (bg / 100);
        *p++ = '0' + ((bg / 10) % 10);
        *p++ = '0' + (bg % 10);
      }

      *p++ = 'm';
      g_sgr256_fgbg[fg][bg].len = (uint8_t)(p - g_sgr256_fgbg[fg][bg].str);
    }
  }

  g_sgr256_initialized = true;
}

// Fast 256-color FG-only sequences
static sgr256_t g_sgr256_fg[256];
static bool g_sgr256_fg_initialized = false;

static void init_sgr256_fg_cache(void) {
  if (g_sgr256_fg_initialized)
    return;

  for (int fg = 0; fg < 256; fg++) {
    char *p = g_sgr256_fg[fg].str;
    *p++ = '\033';
    *p++ = '[';
    *p++ = '3';
    *p++ = '8';
    *p++ = ';';
    *p++ = '5';
    *p++ = ';';

    if (fg < 10) {
      *p++ = '0' + fg;
    } else if (fg < 100) {
      *p++ = '0' + (fg / 10);
      *p++ = '0' + (fg % 10);
    } else {
      *p++ = '0' + (fg / 100);
      *p++ = '0' + ((fg / 10) % 10);
      *p++ = '0' + (fg % 10);
    }

    *p++ = 'm';
    g_sgr256_fg[fg].len = (uint8_t)(p - g_sgr256_fg[fg].str);
  }

  g_sgr256_fg_initialized = true;
}

// FIX #5: Public cache prewarming functions for benchmarks
void prewarm_sgr256_fg_cache(void) {
  init_sgr256_fg_cache();
}

void prewarm_sgr256_cache(void) {
  init_sgr256_cache();
}

// Fast SGR cache wrappers for SIMD implementations
char *get_sgr256_fg_string(uint8_t fg, uint8_t *len_out) {
  init_sgr256_fg_cache();
  const sgr256_t *sgr = &g_sgr256_fg[fg];
  *len_out = sgr->len;
  // DEBUG: Log what string we're returning
  if (sgr->len > 0) {
    log_debug("DEBUG get_sgr256_fg_string: fg=%d len=%d string=[%.*s]", fg, sgr->len, sgr->len, sgr->str);
  }
  return (char *)sgr->str;
}

char *get_sgr256_fg_bg_string(uint8_t fg, uint8_t bg, uint8_t *len_out) {
  init_sgr256_cache();
  const sgr256_t *sgr = &g_sgr256_fgbg[fg][bg];
  *len_out = sgr->len;
  // DEBUG: Log what string we're returning
  if (sgr->len > 0) {
    log_debug("DEBUG get_sgr256_fg_bg_string: fg=%d bg=%d len=%d string=[%.*s]", fg, bg, sgr->len, sgr->len, sgr->str);
  }
  return (char *)sgr->str;
}

// Forward declarations for fast path functions (defined after append_sgr_reset)
size_t render_row_256color_ascii_runlength(const rgb_pixel_t *row, int width, char *dst, size_t cap,
                                           bool background_mode);

// -------- ultra-fast SGR builders with size calculation --------

// Calculate exact size needed for SGR sequences (for security)
// static inline size_t calculate_sgr_truecolor_fg_size(uint8_t r, uint8_t g, uint8_t b) {
//  return 7 + g_dec3[r].len + 1 + g_dec3[g].len + 1 + g_dec3[b].len + 1; // "\033[38;2;" + R + ";" + G + ";" + B + "m"
//}
//
//__attribute__((unused)) static inline size_t calculate_sgr_truecolor_bg_size(uint8_t r, uint8_t g, uint8_t b) {
//  return 7 + g_dec3[r].len + 1 + g_dec3[g].len + 1 + g_dec3[b].len + 1; // "\033[48;2;" + R + ";" + G + ";" + B + "m"
//}
//
// static inline size_t calculate_sgr_truecolor_fg_bg_size(uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg,
//                                                        uint8_t bb) {
//  return 7 + g_dec3[fr].len + 1 + g_dec3[fg].len + 1 + g_dec3[fb].len + 6 + g_dec3[br].len + 1 + g_dec3[bg].len + 1 +
//         g_dec3[bb].len + 1; // Combined FG+BG
//}

inline char *append_sgr_reset(char *dst) {
  // "\x1b[0m"
  static const char RESET[] = "\033[0m";
  memcpy(dst, RESET, sizeof(RESET) - 1);
  return dst + (sizeof(RESET) - 1);
}

// OPTIMIZATION 9: Direct writes instead of memcpy - \x1b[38;2;R;G;Bm
inline char *append_sgr_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  // Constructor ensures initialization

  // Direct character writes (compiler will optimize to word operations)
  *dst++ = '\033';
  *dst++ = '[';
  *dst++ = '3';
  *dst++ = '8';
  *dst++ = ';';
  *dst++ = '2';
  *dst++ = ';';

  // Fast digit copying for 1-3 digit numbers (avoid memcpy overhead)
  const dec3_t *rd = &g_ascii_cache.dec3_table[r];
  if (rd->len == 1) {
    *dst++ = rd->s[0];
  } else if (rd->len == 2) {
    dst[0] = rd->s[0];
    dst[1] = rd->s[1];
    dst += 2;
  } else {
    dst[0] = rd->s[0];
    dst[1] = rd->s[1];
    dst[2] = rd->s[2];
    dst += 3;
  }
  *dst++ = ';';

  const dec3_t *gd = &g_ascii_cache.dec3_table[g];
  if (gd->len == 1) {
    *dst++ = gd->s[0];
  } else if (gd->len == 2) {
    dst[0] = gd->s[0];
    dst[1] = gd->s[1];
    dst += 2;
  } else {
    dst[0] = gd->s[0];
    dst[1] = gd->s[1];
    dst[2] = gd->s[2];
    dst += 3;
  }
  *dst++ = ';';

  const dec3_t *bd = &g_ascii_cache.dec3_table[b];
  if (bd->len == 1) {
    *dst++ = bd->s[0];
  } else if (bd->len == 2) {
    dst[0] = bd->s[0];
    dst[1] = bd->s[1];
    dst += 2;
  } else {
    dst[0] = bd->s[0];
    dst[1] = bd->s[1];
    dst[2] = bd->s[2];
    dst += 3;
  }
  *dst++ = 'm';
  return dst;
}

// OPTIMIZATION 9: Direct writes - \x1b[48;2;R;G;Bm
inline char *append_sgr_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  // Constructor ensures initialization

  // Direct character writes for "\033[48;2;"
  *dst++ = '\033';
  *dst++ = '[';
  *dst++ = '4';
  *dst++ = '8';
  *dst++ = ';';
  *dst++ = '2';
  *dst++ = ';';

  // Optimized digit copying
  const dec3_t *rd = &g_ascii_cache.dec3_table[r];
  if (rd->len == 1) {
    *dst++ = rd->s[0];
  } else if (rd->len == 2) {
    dst[0] = rd->s[0];
    dst[1] = rd->s[1];
    dst += 2;
  } else {
    dst[0] = rd->s[0];
    dst[1] = rd->s[1];
    dst[2] = rd->s[2];
    dst += 3;
  }
  *dst++ = ';';

  const dec3_t *gd = &g_ascii_cache.dec3_table[g];
  if (gd->len == 1) {
    *dst++ = gd->s[0];
  } else if (gd->len == 2) {
    dst[0] = gd->s[0];
    dst[1] = gd->s[1];
    dst += 2;
  } else {
    dst[0] = gd->s[0];
    dst[1] = gd->s[1];
    dst[2] = gd->s[2];
    dst += 3;
  }
  *dst++ = ';';

  const dec3_t *bd = &g_ascii_cache.dec3_table[b];
  if (bd->len == 1) {
    *dst++ = bd->s[0];
  } else if (bd->len == 2) {
    dst[0] = bd->s[0];
    dst[1] = bd->s[1];
    dst += 2;
  } else {
    dst[0] = bd->s[0];
    dst[1] = bd->s[1];
    dst[2] = bd->s[2];
    dst += 3;
  }
  *dst++ = 'm';
  return dst;
}

// OPTIMIZATION 9: Optimized FG+BG - \x1b[38;2;R;G;B;48;2;r;g;bm (eliminate all memcpy calls)
inline char *append_sgr_truecolor_fg_bg(char *dst, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg,
                                        uint8_t bb) {
  // Constructor ensures initialization

  // Write "\033[38;2;" directly (7 chars)
  *dst++ = '\033';
  *dst++ = '[';
  *dst++ = '3';
  *dst++ = '8';
  *dst++ = ';';
  *dst++ = '2';
  *dst++ = ';';

  // Foreground RGB digits
  const dec3_t *d = &g_ascii_cache.dec3_table[fr];
  if (d->len == 1) {
    *dst++ = d->s[0];
  } else if (d->len == 2) {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst += 2;
  } else {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst[2] = d->s[2];
    dst += 3;
  }
  *dst++ = ';';

  d = &g_ascii_cache.dec3_table[fg];
  if (d->len == 1) {
    *dst++ = d->s[0];
  } else if (d->len == 2) {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst += 2;
  } else {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst[2] = d->s[2];
    dst += 3;
  }
  *dst++ = ';';

  d = &g_ascii_cache.dec3_table[fb];
  if (d->len == 1) {
    *dst++ = d->s[0];
  } else if (d->len == 2) {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst += 2;
  } else {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst[2] = d->s[2];
    dst += 3;
  }

  // Write ";48;2;" directly (6 chars)
  *dst++ = ';';
  *dst++ = '4';
  *dst++ = '8';
  *dst++ = ';';
  *dst++ = '2';
  *dst++ = ';';

  // Background RGB digits
  d = &g_ascii_cache.dec3_table[br];
  if (d->len == 1) {
    *dst++ = d->s[0];
  } else if (d->len == 2) {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst += 2;
  } else {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst[2] = d->s[2];
    dst += 3;
  }
  *dst++ = ';';

  d = &g_ascii_cache.dec3_table[bg];
  if (d->len == 1) {
    *dst++ = d->s[0];
  } else if (d->len == 2) {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst += 2;
  } else {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst[2] = d->s[2];
    dst += 3;
  }
  *dst++ = ';';

  d = &g_ascii_cache.dec3_table[bb];
  if (d->len == 1) {
    *dst++ = d->s[0];
  } else if (d->len == 2) {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst += 2;
  } else {
    dst[0] = d->s[0];
    dst[1] = d->s[1];
    dst[2] = d->s[2];
    dst += 3;
  }

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

/* ============================================================================
 * OPTIMIZATION #4: Fast 256-color implementations (defined after SGR functions)
 * ============================================================================
 */

// Optimized row renderer with run-length encoding and FG+BG combined sequences
size_t render_row_truecolor_ascii_runlength(const rgb_pixel_t *row, int width, char *dst, size_t cap,
                                            bool background_mode);

// ----------------
char *image_print_color_simd(image_t *image, bool use_background_mode, bool use_fast_path) {
  // Calculate exact maximum buffer size with precise per-pixel bounds
  const int h = image->h;
  const int w = image->w;

  // Exact per-pixel maximums (with run-length encoding this will be much smaller in practice)
  const size_t per_px = use_background_mode ? 39 : 20; // Worst case per pixel
  const size_t reset_len = 4;                          // \033[0m

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

  // Serial processing fallback (for small images or if threading setup failed)
  size_t total_len = 0;
  for (int y = 0; y < h; y++) {
    // Debug assertion: ensure we have enough space
    const size_t row_max __attribute__((unused)) = w * per_px + reset_len;
    assert(total_len + row_max <= lines_size);

    // Use the NEW optimized run-length encoding function with combined SGR sequences
    bool use_background = use_background_mode;
    size_t row_len = convert_row_with_color_optimized((const rgb_pixel_t *)&image->pixels[y * w], ascii + total_len,
                                                      lines_size - total_len, w, use_background, use_fast_path);
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
 * All platform-specific implementations moved to lib/image2ascii/simd/
 * ============================================================================
 */

// Scalar Unified REP Implementations (for NEON dispatcher fallback)
// ============================================================================
#ifdef SIMD_SUPPORT_NEON
static int unified_neon_color(const rgb_pixel_t *row, int width, int height, char *dst, size_t cap,
                              bool background_mode, bool use_256color) {
  image_t image = {.pixels = (rgb_pixel_t *)row, .w = width, .h = height};
  char *result = render_ascii_neon_unified_optimized(&image, background_mode, use_256color);
  size_t result_len = strlen(result);
  size_t bytes_to_copy = (result_len < cap - 1) ? result_len : cap - 1;
  memcpy(dst, result, bytes_to_copy);
  dst[bytes_to_copy] = '\0'; // Null terminate
  free(result);
  return bytes_to_copy; // Return actual bytes written
}
#endif

size_t render_row_256color_background_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap) {
#ifdef SIMD_SUPPORT_NEON
  return unified_neon_color(row, width, 1, dst, cap, true, true);
#else
  return convert_row_with_color_scalar(row, dst, cap, width, true);
#endif
}

size_t render_row_truecolor_background_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap) {
#ifdef SIMD_SUPPORT_NEON
  return unified_neon_color(row, width, 1, dst, cap, true, false);
#else
  return convert_row_with_color_scalar(row, dst, cap, width, true);
#endif
}

size_t render_row_256color_foreground_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap) {
#ifdef SIMD_SUPPORT_NEON
  return unified_neon_color(row, width, 1, dst, cap, false, true);
#else
  return convert_row_with_color_scalar(row, dst, cap, width, false);
#endif
}

size_t render_row_truecolor_foreground_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap) {
#ifdef SIMD_SUPPORT_NEON
  return unified_neon_color(row, width, 1, dst, cap, false, false);
#else
  return convert_row_with_color_scalar(row, dst, cap, width, false);
#endif
}

// Unified SIMD + scalar REP dispatcher
// Auto-dispatch optimized color function - calls best available SIMD implementation
size_t convert_row_with_color_optimized(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                        bool background_mode, bool use_fast_path) {

#ifdef SIMD_SUPPORT_NEON
  // Use NEON optimized renderer
  return render_row_ascii_rep_dispatch_neon_color(pixels, width, output_buffer, buffer_size, background_mode,
                                                  use_fast_path);
#else
  // Scalar fallback (scalar REP code)
  if (background_mode) {
    if (use_fast_path) {
      return render_row_256color_background_rep_unified(pixels, width, output_buffer, buffer_size);
    } else {
      return render_row_truecolor_background_rep_unified(pixels, width, output_buffer, buffer_size);
    }
  } else {
    if (use_fast_path) {
      return render_row_256color_foreground_rep_unified(pixels, width, output_buffer, buffer_size);
    } else {
      return render_row_truecolor_foreground_rep_unified(pixels, width, output_buffer, buffer_size);
    }
  }
#endif
}

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

    // Get ASCII character - Use global luminance palette like all other functions
    char ascii_char = luminance_palette[luminance];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break;

    if (background_mode) {
      uint8_t fg_color = (luminance < 127) ? 15 : 0; // FIX #6: use 15 not 255!
      // FIXED: Use combined FG+BG sequence like SIMD implementation
      current_pos = append_sgr_truecolor_fg_bg(current_pos, fg_color, fg_color, fg_color, pixel->r, pixel->g, pixel->b);
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

    // Get ASCII character - Use global luminance palette like all other functions
    ascii_chars[x] = luminance_palette[luminance];
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
      uint8_t fg_color = (luminance < 127) ? 15 : 0; // FIX #6: use 15 not 255!
      // FIXED: Use combined FG+BG sequence like SIMD implementation
      current_pos = append_sgr_truecolor_fg_bg(current_pos, fg_color, fg_color, fg_color, pixel->r, pixel->g, pixel->b);
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

/* ============================================================================
 * Run-Length Encoding Color Optimization
 * ============================================================================
 */

// Build one colored ASCII row with FG only or FG+BG, run-length colors.
size_t render_row_truecolor_ascii_runlength(const rgb_pixel_t *row, int width, char *dst, size_t cap,
                                            bool background_mode) {
  char *p = dst;

  // OPTIMIZATION #3: Compute per-row sentinel once (kill per-pixel capacity checks)
  // Worst case: every pixel changes color + has 1 ASCII char + reset at end
  const size_t max_per_pixel = background_mode ? 39 + 1 : 20 + 1; // SGR + ASCII char
  const size_t row_max_size = width * max_per_pixel + 4;          // +4 for reset
  char *row_end = dst + ((cap < row_max_size) ? cap : row_max_size);

  bool have_color = false;
  uint8_t cr = 0, cg = 0, cb = 0; // current foreground color
  // uint8_t br = 0;                 // current fg brightness (for background mode) - removed unused

  // USER'S FIXES: Quantized change detection + FG hysteresis
  uint8_t last_qbg = 255; // Keep outside loop (fix A)
  uint8_t last_fg = 15;   // Track FG with hysteresis (fix B) - Issue #2: use 15 not 255!

  for (int x = 0; x < width; ++x) {
    const rgb_pixel_t *px = &row[x];

    // Calculate luminance for ASCII character selection
    int y = (LUMA_RED * px->r + LUMA_GREEN * px->g + LUMA_BLUE * px->b) >> 8;
    char ch = luminance_palette[y];

    if (background_mode) {
      // FIX A: Quantize BG to 6x6x6 cube for *comparison only*
      int qcr = (px->r * 5) / 255, qcg = (px->g * 5) / 255, qcb = (px->b * 5) / 255;
      uint8_t qbg = (uint8_t)(qcr * 36 + qcg * 6 + qcb); // 0..215

      // FIX B: Hysteresis FG (Schmitt trigger 110/145 instead of 127) - Issue #2: use 15 not 255
      uint8_t fg_val = last_fg;
      if (y < 110)
        fg_val = 15; // White on dark (256-color index 15, not 255!)
      else if (y > 145)
        fg_val = 0; // Black on light

      if (!have_color || qbg != last_qbg || fg_val != last_fg) {
        // Color changed - emit combined FG+BG sequence using EXACT colors
        // NO MORE per-pixel capacity check - we computed row_end sentinel once!
        p = append_sgr_truecolor_fg_bg(p, fg_val, fg_val, fg_val, px->r, px->g, px->b);
        last_qbg = qbg;   // Track quantized state for change detection
        last_fg = fg_val; // Track FG for hysteresis
        cr = px->r;
        cg = px->g;
        cb = px->b;
        // br = fg_val; // removed unused assignment
        have_color = true;
      }
    } else {
      // Foreground mode: Use pixel color as foreground
      if (!have_color || px->r != cr || px->g != cg || px->b != cb) {
        // Color changed - emit foreground sequence
        // NO MORE per-pixel capacity check - we computed row_end sentinel once!
        p = append_sgr_truecolor_fg(p, px->r, px->g, px->b);
        cr = px->r;
        cg = px->g;
        cb = px->b;
        have_color = true;
      }
    }

    // Simple bounds check only when approaching row boundary
    if (p >= row_end - 50) { // 50 byte safety margin
      if (p >= row_end)
        break;
    }
    *p++ = ch;
  }

  // Reset sequence (but no newline - let the caller handle that)
  if (row_end - p >= 4)
    p = append_sgr_reset(p);
  return (size_t)(p - dst);
}
