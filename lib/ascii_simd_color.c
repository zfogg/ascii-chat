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
  return (char *)sgr->str;
}

char *get_sgr256_fg_bg_string(uint8_t fg, uint8_t bg, uint8_t *len_out) {
  init_sgr256_cache();
  const sgr256_t *sgr = &g_sgr256_fgbg[fg][bg];
  *len_out = sgr->len;
  // DEBUG: Log what string we're returning
  return (char *)sgr->str;
}

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
 * All platform-specific implementations moved to lib/image2ascii/simd/
 * ============================================================================
 */

// Row-based scalar function removed - use image_print_color() instead

/* ============================================================================
 * OPTIMIZATION #4: Fast 256-color implementations (defined after SGR functions)
 * ============================================================================
 */

char *image_print_color_simd(image_t *image, bool use_background_mode, bool use_fast_path) {
#ifdef SIMD_SUPPORT_NEON
  return render_ascii_neon_unified_optimized(image, use_background_mode, use_fast_path);
#else
  // Fallback implementation for non-NEON platforms
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
  const size_t lines_size = (size_t)w_sz * (size_t)h_sz * (size_t)per_px + total_resets + total_newlines + 1;

  // Single allocation - no buffer pool overhead, no copying!
  char *ascii;
  SAFE_MALLOC(ascii, lines_size, char *);
  if (!ascii) {
    log_error("Memory allocation failed: %zu bytes", lines_size);
    return NULL;
  }

  // Use scalar image function instead of row-based processing
  free(ascii); // Free the allocated buffer since we're using image function's output
  return image_print_color(image);
#endif
}
