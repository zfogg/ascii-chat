#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "platform/abstraction.h"
#include <stdint.h>
#include <stdbool.h>
#include "ascii_simd.h"

#include "common.h"
#include "../image.h"
#include "palette.h"

/* ============================================================================
 * SIMD-Optimized Colored ASCII Generation
 *
 * This extends the basic SIMD luminance conversion to include full
 * ANSI color code generation for maximum performance.
 * ============================================================================
 */

// Background ASCII luminance threshold - same as NEON version
#ifndef BGASCII_LUMA_THRESHOLD
#define BGASCII_LUMA_THRESHOLD 128 // Y >= 128 -> black text; else white text
#endif

#ifndef CUBE_GRAY_THRESHOLD
#define CUBE_GRAY_THRESHOLD 10
#endif

/* ============================================================================
 * 256-Color ANSI Escape Sequence Generation (inline, no cache)
 * ============================================================================
 * Generates ANSI sequences on-demand. Modern CPUs execute this in ~10-20ns,
 * which is negligible compared to terminal I/O (microseconds).
 */

// Helper: write uint8 to string (1-3 digits)
static inline char *write_u8(char *p, uint8_t n) {
  if (n < 10) {
    *p++ = '0' + n;
  } else if (n < 100) {
    *p++ = '0' + (n / 10);
    *p++ = '0' + (n % 10);
  } else {
    *p++ = '0' + (n / 100);
    *p++ = '0' + ((n / 10) % 10);
    *p++ = '0' + (n % 10);
  }
  return p;
}

// Generate "\e[38;5;NNNm" (foreground only)
static inline char *build_sgr256_fg(char *buf, uint8_t fg, uint8_t *len_out) {
  char *p = buf;
  *p++ = '\033';
  *p++ = '[';
  *p++ = '3';
  *p++ = '8';
  *p++ = ';';
  *p++ = '5';
  *p++ = ';';
  p = write_u8(p, fg);
  *p++ = 'm';
  *len_out = (uint8_t)(p - buf);
  return buf;
}

// Generate "\e[38;5;NNN;48;5;NNNm" (foreground + background)
static inline char *build_sgr256_fgbg(char *buf, uint8_t fg, uint8_t bg, uint8_t *len_out) {
  char *p = buf;
  *p++ = '\033';
  *p++ = '[';
  *p++ = '3';
  *p++ = '8';
  *p++ = ';';
  *p++ = '5';
  *p++ = ';';
  p = write_u8(p, fg);
  *p++ = ';';
  *p++ = '4';
  *p++ = '8';
  *p++ = ';';
  *p++ = '5';
  *p++ = ';';
  p = write_u8(p, bg);
  *p++ = 'm';
  *len_out = (uint8_t)(p - buf);
  return buf;
}

// Public API wrappers
void prewarm_sgr256_fg_cache(void) {
  // No-op: cache removed
}

void prewarm_sgr256_cache(void) {
  // No-op: cache removed
}

// Fast SGR generation for SIMD implementations
char *get_sgr256_fg_string(uint8_t fg, uint8_t *len_out) {
  static __thread char buf[16]; // Thread-local buffer
  return build_sgr256_fg(buf, fg, len_out);
}

char *get_sgr256_fg_bg_string(uint8_t fg, uint8_t bg, uint8_t *len_out) {
  static __thread char buf[32]; // Thread-local buffer
  return build_sgr256_fgbg(buf, fg, bg, len_out);
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
  const dec3_t *rd = &g_dec3_cache.dec3_table[r];
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

  const dec3_t *gd = &g_dec3_cache.dec3_table[g];
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

  const dec3_t *bd = &g_dec3_cache.dec3_table[b];
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
  const dec3_t *rd = &g_dec3_cache.dec3_table[r];
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

  const dec3_t *gd = &g_dec3_cache.dec3_table[g];
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

  const dec3_t *bd = &g_dec3_cache.dec3_table[b];
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
  const dec3_t *d = &g_dec3_cache.dec3_table[fr];
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

  d = &g_dec3_cache.dec3_table[fg];
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

  d = &g_dec3_cache.dec3_table[fb];
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
  d = &g_dec3_cache.dec3_table[br];
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

  d = &g_dec3_cache.dec3_table[bg];
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

  d = &g_dec3_cache.dec3_table[bb];
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
static inline int __attribute__((unused)) generate_ansi_fg(uint8_t r, uint8_t g, uint8_t b, char *dst) {
  char *result = append_sgr_truecolor_fg(dst, r, g, b);
  return (int)(result - dst);
}

static inline int __attribute__((unused)) generate_ansi_bg(uint8_t r, uint8_t g, uint8_t b, char *dst) {
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

char *image_print_color_simd(image_t *image, bool use_background_mode, bool use_256color, const char *ascii_chars) {
  (void)use_256color; // Suppress unused parameter warning when SIMD not available
#ifdef SIMD_SUPPORT_AVX2
  return render_ascii_avx2_unified_optimized(image, use_background_mode, use_256color, ascii_chars);
#elif defined(SIMD_SUPPORT_SSSE3)
  return render_ascii_ssse3_unified_optimized(image, use_background_mode, use_256color, ascii_chars);
#elif defined(SIMD_SUPPORT_SSE2)
  return render_ascii_sse2_unified_optimized(image, use_background_mode, use_256color, ascii_chars);
#elif defined(SIMD_SUPPORT_NEON)
  return render_ascii_neon_unified_optimized(image, use_background_mode, use_256color, ascii_chars);
#else
  // Fallback implementation for non-NEON platforms
  // Calculate exact maximum buffer size with precise per-pixel bounds
  const int h = image->h;
  const int w = image->w;

  // Use scalar image function for fallback path - no SIMD allocation needed
  return image_print_color(image, ascii_chars);
#endif
}
