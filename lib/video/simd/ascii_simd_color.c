/**
 * @file video/simd/ascii_simd_color.c
 * @ingroup video
 * @brief 🎨 SIMD-accelerated color matching and palette lookup for ASCII rendering
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <ascii-chat/platform/abstraction.h>
#include <stdint.h>
#include <stdbool.h>
#include <ascii-chat/video/simd/ascii_simd.h>

#include <ascii-chat/common.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/util/number.h> // For write_u8
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/logging.h>

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
 * 256-Color ANSI Escape Sequence Generation (cached)
 * ============================================================================
 * Pre-generates all 256 color sequences at startup and caches them.
 * This avoids repeated generation during per-pixel rendering.
 */

/* write_u8() is now in util/number.h */

/**
 * @brief Pre-computed 256-color ANSI SGR sequence
 */
typedef struct {
  char seq[12]; ///< ANSI sequence string (max 11 bytes for "\e[38;5;NNNm")
  uint8_t len;  ///< Length of sequence string
} sgr256_seq_t;

static sgr256_seq_t sgr256_fg_cache[256];
static sgr256_seq_t sgr256_bg_cache[256];
static bool sgr256_cache_initialized = false;

// Build and cache all 256 foreground and background color sequences
static void init_sgr256_cache(void) {
  if (sgr256_cache_initialized)
    return;

  // Cache foreground colors: \e[38;5;NNNm
  for (int i = 0; i < 256; i++) {
    char *p = sgr256_fg_cache[i].seq;
    *p++ = '\033';
    *p++ = '[';
    *p++ = '3';
    *p++ = '8';
    *p++ = ';';
    *p++ = '5';
    *p++ = ';';
    p = write_u8(p, (uint8_t)i);
    *p++ = 'm';
    sgr256_fg_cache[i].len = (uint8_t)(p - sgr256_fg_cache[i].seq);
  }

  // Cache background colors: \e[48;5;NNNm
  for (int i = 0; i < 256; i++) {
    char *p = sgr256_bg_cache[i].seq;
    *p++ = '\033';
    *p++ = '[';
    *p++ = '4';
    *p++ = '8';
    *p++ = ';';
    *p++ = '5';
    *p++ = ';';
    p = write_u8(p, (uint8_t)i);
    *p++ = 'm';
    sgr256_bg_cache[i].len = (uint8_t)(p - sgr256_bg_cache[i].seq);
  }

  sgr256_cache_initialized = true;
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
  init_sgr256_cache();
}

void prewarm_sgr256_cache(void) {
  init_sgr256_cache();
}

// Fast SGR generation for SIMD implementations - uses cached sequences
char *get_sgr256_fg_string(uint8_t fg, uint8_t *len_out) {
  if (!sgr256_cache_initialized) {
    init_sgr256_cache();
  }
  *len_out = sgr256_fg_cache[fg].len;
  return sgr256_fg_cache[fg].seq;
}

char *get_sgr256_bg_string(uint8_t bg, uint8_t *len_out) {
  if (!sgr256_cache_initialized) {
    init_sgr256_cache();
  }
  *len_out = sgr256_bg_cache[bg].len;
  return sgr256_bg_cache[bg].seq;
}

char *get_sgr256_fg_bg_string(uint8_t fg, uint8_t bg, uint8_t *len_out) {
  // For FG+BG, still build on-demand since we'd need 256*256 cache
  static __thread char buf[32];
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

/* ============================================================================
 * All platform-specific implementations moved to lib/video/simd/
 * ============================================================================
 */

// Row-based scalar function removed - use image_print_color() instead

/* ============================================================================
 * OPTIMIZATION #4: Fast 256-color implementations (defined after SGR functions)
 * ============================================================================
 */

char *image_print_color_simd(image_t *image, bool use_background_mode, bool use_256color, const char *ascii_chars) {
  log_dev_every(4500000, "image_print_color_simd called: width=%d, height=%d, use_256color=%d", image ? image->w : -1,
                image ? image->h : -1, use_256color);

#if SIMD_SUPPORT_AVX2
  log_debug_every(10000000, "Taking AVX2 path: width=%d, height=%d", image->w, image->h);
  START_TIMER("render_avx2");
  char *result = render_ascii_avx2_unified_optimized(image, use_background_mode, use_256color, ascii_chars);
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "render_avx2", "RENDER_AVX2: Complete");
  return result;
#elif SIMD_SUPPORT_SSSE3
  log_info_every(10000000, "WASM: Taking SSSE3 path with use_256color=%d", use_256color);
  START_TIMER("render_ssse3");
  char *result = render_ascii_ssse3_unified_optimized(image, use_background_mode, use_256color, ascii_chars);
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "render_ssse3", "RENDER_SSSE3: Complete");
  return result;
#elif SIMD_SUPPORT_SSE2
  log_info_every(10000000, "WASM: Taking SSE2 path with use_256color=%d", use_256color);
  START_TIMER("render_sse2");
  char *result = render_ascii_sse2_unified_optimized(image, use_background_mode, use_256color, ascii_chars);
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "render_sse2", "RENDER_SSE2: Complete");
  return result;
#elif SIMD_SUPPORT_NEON
  log_info_every(10000000, "WASM: Taking NEON path with use_256color=%d", use_256color);
  START_TIMER("render_neon");
  char *result = render_ascii_neon_unified_optimized(image, use_background_mode, use_256color, ascii_chars);
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "render_neon", "RENDER_NEON: Complete");
  return result;
#else
  log_info_every(10000000, "WASM: Taking FALLBACK path (no SIMD), use_256color=%d is IGNORED", use_256color);
  // Fallback implementation for non-SIMD platforms
  // Use scalar image function for fallback path - no SIMD allocation needed
  (void)use_256color;        // Suppress unused parameter warning
  (void)use_background_mode; // Suppress unused parameter warning
  START_TIMER("render_color_fallback");
  char *result = image_print_color(image, ascii_chars);
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "render_color_fallback",
                           "RENDER_COLOR_FALLBACK: Complete");
  return result;
#endif
}
