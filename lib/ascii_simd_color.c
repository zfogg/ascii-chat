#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "ascii_simd.h"
#include "ascii_simd_neon.h"
#include "options.h"
#include "common.h"
#include "image.h"

#if defined(SIMD_SUPPORT_AVX512) && defined(__AVX512F__) && defined(__AVX512BW__)
#include <immintrin.h>
#endif

#if defined(SIMD_SUPPORT_SVE) && defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#endif

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#ifndef SIMD_SUPPORT_NEON
#define SIMD_SUPPORT_NEON 1
#endif
#endif

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

#ifdef SIMD_SUPPORT_NEON
// CHATGPT OPTIMIZATION: Vectorized ASCII lookup using vqtbl2q_u8
// This processes 16 pixels at once instead of scalar loop
static inline uint8x16_t luma_to_ascii_vectorized(uint8x16_t luma_vec) {
  // Create 32-entry ASCII lookup table split across two vectors
  // Our palette: "   ...',;:clodxkO0KXNWM" (22 chars) -> spread across 32 slots
  static const uint8_t ascii_lut_0[16] = {
      ' ', ' ', ' ', '.', '.', '.', '\'', '\'', // Entries 0-7
      ',', ';', ':', 'c', 'l', 'o', 'd',  'x'   // Entries 8-15
  };
  static const uint8_t ascii_lut_1[16] = {
      'k', 'O', '0', 'K', 'X', 'N', 'W', 'M', // Entries 16-23
      'M', 'M', 'M', 'M', 'M', 'M', 'M', 'M'  // Entries 24-31 (repeat densest)
  };

  const uint8x16_t lut0 = vld1q_u8(ascii_lut_0);
  const uint8x16_t lut1 = vld1q_u8(ascii_lut_1);

  // Map luma 0-255 to index 0-31 (top 5 bits)
  uint8x16_t indices = vshrq_n_u8(luma_vec, 3); // Divide by 8: 256→32 bins

  // Vector table lookup: 16 ASCII characters computed in parallel!
  uint8x16x2_t combined_lut = {lut0, lut1};
  return vqtbl2q_u8(combined_lut, indices);
}
#endif

/* ============================================================================
 * OPTIMIZATION #4: 256-Color Mode with Precomputed FG+BG Strings (~1.5MB cache)
 * ============================================================================
 */

// RGB to ANSI 256-color conversion (6×6×6 cube + grays)
static inline uint8_t rgb_to_ansi256(uint8_t r, uint8_t g, uint8_t b) {
  // Convert to 6-level cube coordinates (0-5)
  int cr = (r * 5 + 127) / 255; // Round to nearest
  int cg = (g * 5 + 127) / 255;
  int cb = (b * 5 + 127) / 255;

  // Check if it's closer to a gray level (colors 232-255: 24 grays)
  int gray = (r + g + b) / 3;
  int closest_gray_idx = 232 + (gray * 23) / 255; // 232-255 range

  // Calculate actual gray value for this index
  int gray_level = 8 + (closest_gray_idx - 232) * 10; // 8 to 238
  int gray_dist = abs(gray - gray_level);

  // Calculate 6x6x6 cube color distance
  int cube_r = (cr * 255) / 5;
  int cube_g = (cg * 255) / 5;
  int cube_b = (cb * 255) / 5;
  int cube_dist = abs(r - cube_r) + abs(g - cube_g) + abs(b - cube_b);

  if (gray_dist < cube_dist) {
    return (uint8_t)closest_gray_idx;
  } else {
    return (uint8_t)(16 + cr * 36 + cg * 6 + cb); // 6x6x6 cube
  }
}

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

// Fast 256-color FG+BG sequence emission (single memcpy!)
static inline char *append_sgr256_fg_bg(char *dst, uint8_t fg, uint8_t bg) {
  init_sgr256_cache();
  const sgr256_t *sgr = &g_sgr256_fgbg[fg][bg];
  memcpy(dst, sgr->str, sgr->len);
  return dst + sgr->len;
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

static inline char *append_sgr256_fg(char *dst, uint8_t fg) {
  init_sgr256_fg_cache();
  const sgr256_t *sgr = &g_sgr256_fg[fg];
  memcpy(dst, sgr->str, sgr->len);
  return dst + sgr->len;
}

// Quality vs speed trade-off flag (will move fast path functions after append_sgr_reset)
static bool g_use_256_color_fast_path = false; // DEBUGGING: Use truecolor path that was working

// API to control quality vs speed
void set_color_quality_mode(bool high_quality) {
  g_use_256_color_fast_path = !high_quality;
}

// API to query current quality mode
bool get_256_color_fast_path(void) {
  return g_use_256_color_fast_path;
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
  return (char *)sgr->str;
}

char *get_sgr256_fg_bg_string(uint8_t fg, uint8_t bg, uint8_t *len_out) {
  init_sgr256_cache();
  const sgr256_t *sgr = &g_sgr256_fgbg[fg][bg];
  *len_out = sgr->len;
  return (char *)sgr->str;
}

// Forward declarations for fast path functions (defined after append_sgr_reset)
size_t render_row_256color_ascii_runlength(const rgb_pixel_t *row, int width, char *dst, size_t cap,
                                           bool background_mode);
size_t render_row_upper_half_block_256color(const rgb_pixel_t *top_row, const rgb_pixel_t *bottom_row, int width,
                                            char *dst, size_t cap);

// -------- ultra-fast SGR builders with size calculation --------

// Calculate exact size needed for SGR sequences (for security)
// static inline size_t calculate_sgr_truecolor_fg_size(uint8_t r, uint8_t g, uint8_t b) {
//  init_dec3();
//  return 7 + g_dec3[r].len + 1 + g_dec3[g].len + 1 + g_dec3[b].len + 1; // "\033[38;2;" + R + ";" + G + ";" + B + "m"
//}
//
//__attribute__((unused)) static inline size_t calculate_sgr_truecolor_bg_size(uint8_t r, uint8_t g, uint8_t b) {
//  init_dec3();
//  return 7 + g_dec3[r].len + 1 + g_dec3[g].len + 1 + g_dec3[b].len + 1; // "\033[48;2;" + R + ";" + G + ";" + B + "m"
//}
//
// static inline size_t calculate_sgr_truecolor_fg_bg_size(uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg,
//                                                        uint8_t bb) {
//  init_dec3();
//  return 7 + g_dec3[fr].len + 1 + g_dec3[fg].len + 1 + g_dec3[fb].len + 6 + g_dec3[br].len + 1 + g_dec3[bg].len + 1 +
//         g_dec3[bb].len + 1; // Combined FG+BG
//}

static inline char *append_sgr_reset(char *dst) {
  // "\x1b[0m"
  static const char RESET[] = "\033[0m";
  memcpy(dst, RESET, sizeof(RESET) - 1);
  return dst + (sizeof(RESET) - 1);
}

// OPTIMIZATION 9: Direct writes instead of memcpy - \x1b[38;2;R;G;Bm
static inline char *append_sgr_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
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
static inline char *append_sgr_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
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
static inline char *append_sgr_truecolor_fg_bg(char *dst, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg,
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

// Fast 256-color upper half block renderer with OPTIMIZATION #5: REP compression
size_t render_row_upper_half_block_256color(const rgb_pixel_t *top_row, const rgb_pixel_t *bottom_row, int width,
                                            char *dst, size_t cap) {
  char *p = dst;

  // OPTIMIZATION #3: Compute per-row sentinel once
  const size_t max_per_pixel = 22 + 3;                   // Max 256-color FG+BG SGR + UTF-8 ▀
  const size_t row_max_size = width * max_per_pixel + 4; // +4 for reset
  char *row_end = dst + ((cap < row_max_size) ? cap : row_max_size);

  // Current colors for run-length encoding
  bool have_color = false;
  uint8_t fg_idx = 0, bg_idx = 0; // 256-color indices

  int x = 0;
  while (x < width) {
    const rgb_pixel_t *top_px = &top_row[x];
    const rgb_pixel_t *bot_px = &bottom_row[x];

    // Convert to 256-color indices
    uint8_t run_fg_idx = rgb_to_ansi256(top_px->r, top_px->g, top_px->b);
    uint8_t run_bg_idx = rgb_to_ansi256(bot_px->r, bot_px->g, bot_px->b);

    // OPTIMIZATION #5: Look ahead for identical consecutive ▀ blocks
    int run_length = 1;

    // Count consecutive ▀ blocks with same FG+BG color combination
    while (x + run_length < width) {
      const rgb_pixel_t *next_top_px = &top_row[x + run_length];
      const rgb_pixel_t *next_bot_px = &bottom_row[x + run_length];

      uint8_t next_fg_idx = rgb_to_ansi256(next_top_px->r, next_top_px->g, next_top_px->b);
      uint8_t next_bg_idx = rgb_to_ansi256(next_bot_px->r, next_bot_px->g, next_bot_px->b);

      if (next_fg_idx != run_fg_idx || next_bg_idx != run_bg_idx)
        break; // Different colors

      run_length++;
    }

    // Set color if it changed
    if (!have_color || run_fg_idx != fg_idx || run_bg_idx != bg_idx) {
      // OPTIMIZATION #4: Single memcpy instead of expensive snprintf!
      p = append_sgr256_fg_bg(p, run_fg_idx, run_bg_idx);
      fg_idx = run_fg_idx;
      bg_idx = run_bg_idx;
      have_color = true;
    }

    // Simple bounds check only when approaching row boundary
    if (p >= row_end - 30) {
      if (p >= row_end)
        break;
    }

    // OPTIMIZATION #5: Use REP compression for runs >= 3 ▀ blocks (saves more due to 3-byte UTF-8)
    if (run_length >= 3) {
      // Print one ▀, then use REP to repeat the preceding glyph (run_length - 1) times
      *p++ = 0xE2;
      *p++ = 0x96;
      *p++ = 0x80;
      int rep = run_length - 1;
      *p++ = '\x1b';
      *p++ = '[';

      if (rep >= 100) {
        *p++ = '0' + (rep / 100);
        rep %= 100;
        *p++ = '0' + (rep / 10);
        *p++ = '0' + (rep % 10);
      } else if (rep >= 10) {
        *p++ = '0' + (rep / 10);
        *p++ = '0' + (rep % 10);
      } else {
        *p++ = '0' + rep;
      }

      *p++ = 'b'; // REP command
    } else {
      // Short run: write ▀ characters normally (more efficient than REP overhead)
      for (int i = 0; i < run_length; i++) {
        *p++ = 0xE2;
        *p++ = 0x96;
        *p++ = 0x80;
      }
    }

    x += run_length;
  }

  // Add reset sequence
  if (row_end - p >= 4)
    p = append_sgr_reset(p);

  return (size_t)(p - dst);
}

// Optimized row renderer with run-length encoding and FG+BG combined sequences
size_t render_row_truecolor_ascii_runlength(const rgb_pixel_t *row, int width, char *dst, size_t cap,
                                            bool background_mode);

// Thread worker data structures for OPTIMIZATION #8
typedef struct {
  const rgb_pixel_t *row_pixels;
  char *row_output;
  size_t row_capacity;
  int width;
  bool background_mode;
  size_t row_length; // Output from thread
} row_task_t;

typedef struct {
  row_task_t *tasks;
  int start_row;
  int end_row;
} thread_data_t;

// Thread worker function for parallel row processing
static void *row_worker(void *arg) {
  thread_data_t *data = (thread_data_t *)arg;
  for (int y = data->start_row; y < data->end_row; y++) {
    data->tasks[y].row_length =
        render_row_truecolor_ascii_runlength(data->tasks[y].row_pixels, data->tasks[y].width, data->tasks[y].row_output,
                                             data->tasks[y].row_capacity, data->tasks[y].background_mode);
  }
  return NULL;
}

// ----------------
char *image_print_colored_simd(image_t *image) {
  // Ensure all caches are initialized before any processing
  init_palette();
  init_dec3();

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

  // OPTIMIZATION #8: Parallelize across rows for memory bandwidth
  // For large images, use threading to process multiple rows simultaneously
  bool use_threading = false; // DEBUGGING: Disable threading temporarily

  if (use_threading) {
    // Parallel processing for large images - improves memory bandwidth utilization
    // We'll pre-compute row pointers and sizes, then assemble in order

    // Allocate task array
    row_task_t *tasks;
    SAFE_MALLOC(tasks, h * sizeof(row_task_t), row_task_t *);

    // Pre-calculate row output positions
    size_t current_pos = 0;
    for (int y = 0; y < h; y++) {
      tasks[y].row_pixels = (const rgb_pixel_t *)&image->pixels[y * w];
      tasks[y].row_output = ascii + current_pos;
      tasks[y].row_capacity = lines_size - current_pos;
      tasks[y].width = w;
      tasks[y].background_mode = opt_background_color;
      tasks[y].row_length = 0;

      // Reserve space for this row + newline
      const size_t row_max = w * per_px + reset_len;
      current_pos += row_max + 1; // +1 for newline

      if (current_pos >= lines_size) {
        // Safety fallback
        free(tasks);
        use_threading = false;
        break;
      }
    }

    if (use_threading) {
      // Process rows in parallel using pthread worker threads
      // For memory-bound workloads, use number of cores to maximize memory bandwidth
      const int num_threads = 4; // Conservative for Apple Silicon (4-8 performance cores)
      const int rows_per_thread = (h + num_threads - 1) / num_threads;

      pthread_t threads[num_threads];
      thread_data_t thread_data[num_threads];

      // Launch worker threads
      for (int t = 0; t < num_threads; t++) {
        thread_data[t].tasks = tasks;
        thread_data[t].start_row = t * rows_per_thread;
        thread_data[t].end_row = (t + 1) * rows_per_thread;
        if (thread_data[t].end_row > h)
          thread_data[t].end_row = h;

        if (thread_data[t].start_row < h) {
          pthread_create(&threads[t], NULL, row_worker, &thread_data[t]);
        }
      }

      // Wait for all threads to complete
      for (int t = 0; t < num_threads; t++) {
        if (thread_data[t].start_row < h) {
          pthread_join(threads[t], NULL);
        }
      }

      // Assemble final output by shifting rows to correct positions
      size_t total_len = 0;
      for (int y = 0; y < h; y++) {
        // Move row data to correct position if needed
        if (tasks[y].row_output != ascii + total_len) {
          memmove(ascii + total_len, tasks[y].row_output, tasks[y].row_length);
        }
        total_len += tasks[y].row_length;

        // Add newline after each row (except the last row)
        if (y != h - 1 && total_len < lines_size - 1) {
          ascii[total_len++] = '\n';
        }
      }

      free(tasks);
      ascii[total_len] = '\0';
      return ascii;
    }
  }

  // Serial processing fallback (for small images or if threading setup failed)
  size_t total_len = 0;
  for (int y = 0; y < h; y++) {
    // Debug assertion: ensure we have enough space
    const size_t row_max __attribute__((unused)) = w * per_px + reset_len;
    assert(total_len + row_max <= lines_size);

    // Use the NEW optimized run-length encoding function with combined SGR sequences
#ifdef SIMD_SUPPORT_NEON_DISABLED_FOR_TESTING
    size_t row_len =
        render_row_ascii_rep_dispatch_neon((const rgb_pixel_t *)&image->pixels[y * w], w, ascii + total_len,
                                           lines_size - total_len, opt_background_color, true);
#else
    size_t row_len = render_row_truecolor_ascii_runlength(
        (const rgb_pixel_t *)&image->pixels[y * w], w, ascii + total_len, lines_size - total_len, opt_background_color);
#endif
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
  // OPTIMIZATION 15: Pre-allocated static buffer eliminates malloc/free in hot path
  // 8K characters handles up to 8K horizontal resolution
  static char large_ascii_buffer[8192] __attribute__((aligned(64)));
  char stack_ascii_chars[2048]; // Stack buffer for typical terminal widths
  char *ascii_chars = (width > 2048) ? large_ascii_buffer : stack_ascii_chars;
  bool heap_allocated = false; // Never needed now!

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
      uint8_t fg_color = (luminance < 127) ? 15 : 0; // FIX #6: use 15 not 255!

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

  // OPTIMIZATION 15: No heap cleanup needed - using static/stack buffers only!
  (void)heap_allocated; // Suppress unused variable warning

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
      uint8_t fg_color = (luminance < 127) ? 15 : 0; // FIX #6: use 15 not 255!

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

#ifdef SIMD_SUPPORT_SSSE3
#include <tmmintrin.h>
// SSSE3 version with 32-pixel processing for maximum performance
size_t convert_row_with_color_ssse3(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                    bool background_mode) {

  // Use stack allocation for small widths, heap for large
  // OPTIMIZATION 15: Pre-allocated static buffer eliminates malloc/free in hot path
  // 8K characters handles up to 8K horizontal resolution
  static char large_ascii_buffer[8192] __attribute__((aligned(64)));
  char stack_ascii_chars[2048]; // Stack buffer for typical terminal widths
  char *ascii_chars = (width > 2048) ? large_ascii_buffer : stack_ascii_chars;
  bool heap_allocated = false; // Never needed now!

  // Step 1: SIMD luminance conversion with 32-pixel processing
  convert_pixels_ssse3(pixels, ascii_chars, width);

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
      uint8_t fg_color = (luminance < 127) ? 15 : 0; // FIX #6: use 15 not 255!

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

      // Add ASCII character
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  const int reset_len = 4;
  if (buffer_end - current_pos >= reset_len) {
    memcpy(current_pos, "\033[0m", reset_len);
    current_pos += reset_len;
  }

  if (heap_allocated) {
    free(ascii_chars);
  }

  return (size_t)(current_pos - output_buffer);
}
#endif

#ifdef SIMD_SUPPORT_SSE2
// SSE2 version for older Intel/AMD systems - OPTIMIZED (no buffer pool)
size_t convert_row_with_color_sse2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {

  // Use stack allocation for small widths, heap for large
  // OPTIMIZATION 15: Pre-allocated static buffer eliminates malloc/free in hot path
  // 8K characters handles up to 8K horizontal resolution
  static char large_ascii_buffer[8192] __attribute__((aligned(64)));
  char stack_ascii_chars[2048]; // Stack buffer for typical terminal widths
  char *ascii_chars = (width > 2048) ? large_ascii_buffer : stack_ascii_chars;
  bool heap_allocated = false; // Never needed now!

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
      uint8_t fg_color = (luminance < 127) ? 15 : 0; // FIX #6: use 15 not 255!

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

  // OPTIMIZATION 15: No heap cleanup needed - using static/stack buffers only!
  (void)heap_allocated; // Suppress unused variable warning

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
      uint8_t fg_color = (luminance < 127) ? 15 : 0; // FIX #6: use 15 not 255!

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

// CHATGPT OPTIMIZATION 1: Specialized functions eliminate per-pixel branching
static size_t convert_row_mono_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width);

// OPTIMIZED MONOCHROME NEON: No color code at all - pure ASCII generation
static size_t convert_row_mono_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width) {

  char *p = output_buffer;
  char *end = output_buffer + buffer_size;
  int x = 0;

  // CHATGPT OPTIMIZATION 2: Vectorized stores - process 16 pixels at once with st1
  for (; x + 15 < width && (end - p) >= 16; x += 16) {
    const uint8_t *rgb_data = (const uint8_t *)&pixels[x];

    // Load 16 RGB pixels
    uint8x16x3_t rgb_batch = vld3q_u8(rgb_data);

    // Vectorized luminance computation (16 pixels)
    uint16x8_t r_lo = vmovl_u8(vget_low_u8(rgb_batch.val[0]));
    uint16x8_t r_hi = vmovl_u8(vget_high_u8(rgb_batch.val[0]));
    uint16x8_t g_lo = vmovl_u8(vget_low_u8(rgb_batch.val[1]));
    uint16x8_t g_hi = vmovl_u8(vget_high_u8(rgb_batch.val[1]));
    uint16x8_t b_lo = vmovl_u8(vget_low_u8(rgb_batch.val[2]));
    uint16x8_t b_hi = vmovl_u8(vget_high_u8(rgb_batch.val[2]));

    uint16x8_t luma_lo = vmulq_n_u16(r_lo, LUMA_RED);
    luma_lo = vmlaq_n_u16(luma_lo, g_lo, LUMA_GREEN);
    luma_lo = vmlaq_n_u16(luma_lo, b_lo, LUMA_BLUE);
    luma_lo = vshrq_n_u16(luma_lo, 8);

    uint16x8_t luma_hi = vmulq_n_u16(r_hi, LUMA_RED);
    luma_hi = vmlaq_n_u16(luma_hi, g_hi, LUMA_GREEN);
    luma_hi = vmlaq_n_u16(luma_hi, b_hi, LUMA_BLUE);
    luma_hi = vshrq_n_u16(luma_hi, 8);

    // Pack luminance and convert to ASCII (vectorized)
    uint8x16_t luma_vec = vcombine_u8(vqmovn_u16(luma_lo), vqmovn_u16(luma_hi));
    uint8x16_t ascii_chars = luma_to_ascii_vectorized(luma_vec);

    // CHATGPT OPTIMIZATION 2: Vector store - 16 bytes in one shot!
    vst1q_u8((uint8_t *)p, ascii_chars);
    p += 16;
  }

  // Handle remaining pixels with scalar processing
  for (; x < width && (end - p) >= 1; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    uint8_t luma = (LUMA_RED * pixel->r + LUMA_GREEN * pixel->g + LUMA_BLUE * pixel->b) >> 8;
    init_palette(); // Ensure palette is initialized
    *p++ = luminance_palette[luma];
  }

  return p - output_buffer;
}

// Forward declaration for colored NEON
static size_t convert_row_colored_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                       bool background_mode);

// Top-level dispatch - no per-pixel color branches inside hot loops
size_t convert_row_with_color_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {
  // CHATGPT INSIGHT: Duplicate 150-250 bytes of code is cheap compared to branch mispredicts
  if (opt_color_output || background_mode) {
    return convert_row_colored_neon(pixels, output_buffer, buffer_size, width, background_mode);
  } else {
    return convert_row_mono_neon(pixels, output_buffer, buffer_size, width);
  }
}

// OPTIMIZED COLORED NEON: Specialized for color output only
static size_t convert_row_colored_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                       bool background_mode) {

  char *p = output_buffer;
  const char *buffer_end = output_buffer + buffer_size - 1;

  // NEON weight constants (same as mono code)
  const uint8x8_t wR = vdup_n_u8(LUMA_RED), wG = vdup_n_u8(LUMA_GREEN), wB = vdup_n_u8(LUMA_BLUE);

  // ASCII lookup tables (same as mono code)
  const uint8x16_t lut0 = vld1q_u8((const uint8_t *)" ...',;:clodxk");
  const uint8x16_t lut1 = vld1q_u8((const uint8_t *)"O0KXNWMMMMMMMMM");

  // Process 16 pixels at a time (same as mono code)
  int processed = 0;
  while (processed + 16 <= width && p < buffer_end - 1000) { // Leave room for ANSI sequences

    // Load interleaved RGB (same as mono code)
    uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + processed));

    // Compute luminance: (77*R + 150*G + 29*B) >> 8 (same formula as mono code)
    uint16x8_t y0 = vshrq_n_u16(vmlaq_u16(vmlaq_u16(vmulq_u16(vmovl_u8(vget_low_u8(rgb.val[0])), vmovl_u8(wR)),
                                                    vmovl_u8(vget_low_u8(rgb.val[1])), vmovl_u8(wG)),
                                          vmovl_u8(vget_low_u8(rgb.val[2])), vmovl_u8(wB)),
                                8);
    uint16x8_t y1 = vshrq_n_u16(vmlaq_u16(vmlaq_u16(vmulq_u16(vmovl_u8(vget_high_u8(rgb.val[0])), vmovl_u8(wR)),
                                                    vmovl_u8(vget_high_u8(rgb.val[1])), vmovl_u8(wG)),
                                          vmovl_u8(vget_high_u8(rgb.val[2])), vmovl_u8(wB)),
                                8);
    uint8x16_t y = vcombine_u8(vqmovn_u16(y0), vqmovn_u16(y1));

    // ASCII lookup (same as mono code)
    uint8x16_t idx = vshrq_n_u8(y, 3); // 0..31
    uint8x16x2_t L = {lut0, lut1};
    uint8x16_t ascii_chars = vqtbl2q_u8(L, idx);

    // PERFORMANCE FIX: Store SIMD results and emit per-pixel (simplest approach)
    uint8_t ascii_batch[16] __attribute__((aligned(16)));
    uint8_t r_batch[16] __attribute__((aligned(16)));
    uint8_t g_batch[16] __attribute__((aligned(16)));
    uint8_t b_batch[16] __attribute__((aligned(16)));
    uint8_t luma_batch[16] __attribute__((aligned(16)));

    vst1q_u8(ascii_batch, ascii_chars);
    vst1q_u8(r_batch, rgb.val[0]);
    vst1q_u8(g_batch, rgb.val[1]);
    vst1q_u8(b_batch, rgb.val[2]);
    vst1q_u8(luma_batch, y);

    // Simple per-pixel emission - no complex quantization or hysteresis for now
    for (int k = 0; k < 16; k++) {
      uint8_t r = r_batch[k], g = g_batch[k], b = b_batch[k];
      if (background_mode) {
        uint8_t luma = luma_batch[k];              // Use SIMD-computed luminance
        uint8_t fg_color = (luma < 127) ? 255 : 0; // Simple threshold
        p = append_sgr_truecolor_fg_bg(p, fg_color, fg_color, fg_color, r, g, b);
      } else {
        p = append_sgr_truecolor_fg(p, r, g, b);
      }
      *p++ = ascii_batch[k];
      if (p >= buffer_end - 100)
        break;
    }
    processed += 16;
  }

  // Handle remaining pixels (< 16) with scalar processing
  for (int x = processed; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    // Scalar luminance calculation (same formula as NEON)
    uint16_t luma_16 = (LUMA_RED * pixel->r + LUMA_GREEN * pixel->g + LUMA_BLUE * pixel->b);
    uint8_t luma = (uint8_t)(luma_16 >> 8);
    uint8_t idx = luma >> 3; // 0..31
    char ascii_char = (idx < 16) ? " ...',;:clodxk"[idx] : "O0KXNWMMMMMMMMM"[idx - 16];

    // Color sequences (same optimization as NEON loop)
    if (background_mode) {
      // FIX D: Scalar fallback also needs proper contrasting FG with hysteresis
      uint8_t luma = (LUMA_RED * pixel->r + LUMA_GREEN * pixel->g + LUMA_BLUE * pixel->b) >> 8;
      static uint8_t last_scalar_fg = 255;
      uint8_t fg_color = last_scalar_fg;
      if (luma < 110)
        fg_color = 255; // White on dark
      else if (luma > 145)
        fg_color = 0; // Black on light
      // else keep last_scalar_fg (hysteresis prevents flickering)
      last_scalar_fg = fg_color;
      p = append_sgr_truecolor_fg_bg(p, fg_color, fg_color, fg_color, pixel->r, pixel->g, pixel->b);
    } else {
      p = append_sgr_truecolor_fg(p, pixel->r, pixel->g, pixel->b);
    }
    *p++ = ascii_char;
    if (p >= buffer_end - 100)
      break;
  }

  return p - output_buffer;
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

    // Get ASCII character - FIXED: Use same palette as SIMD implementation
    static const char palette[] = "   ...',;:clodxkO0KXNWM";
    char ascii_char = palette[luminance * (sizeof(palette) - 2) / 255];

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

    // Get ASCII character - FIXED: Use same palette as SIMD implementation
    static const char palette[] = "   ...',;:clodxkO0KXNWM";
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
 * 256-Color Quantization Functions
 * ============================================================================
 */

// Duplicate function removed - already defined earlier

/* ============================================================================
 * Run-Length Encoding Color Optimization
 * ============================================================================
 */

// Build one colored ASCII row with FG only or FG+BG, run-length colors.
size_t render_row_truecolor_ascii_runlength(const rgb_pixel_t *row, int width, char *dst, size_t cap,
                                            bool background_mode) {

  // FIX C: Make 256-color path truly configurable (ChatGPT Issue #2)
  // CRITICAL: Force 256-color path for background mode (40x+ speedup!)
  bool use_fast_path = background_mode ? true : g_use_256_color_fast_path;

  if (use_fast_path) {
    // Use the NEON SIMD implementation instead of the broken scalar function
#ifdef SIMD_SUPPORT_NEON
    return render_row_ascii_rep_dispatch_neon(row, width, dst, cap, background_mode, true);
#else
    // Fallback for non-NEON platforms - use truecolor mode
    use_fast_path = false;
#endif
  }

  // Fallback to truecolor for high quality mode

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
    init_palette(); // Ensure palette is initialized
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

/* ============================================================================
 * Upper Half Block (▀) Renderer - 2x Vertical Density
 * ============================================================================
 */

// Render two image rows as one terminal row using ▀ character
// FG = top pixel color, BG = bottom pixel color = 2x vertical resolution!
size_t render_row_upper_half_block(const rgb_pixel_t *top_row, const rgb_pixel_t *bottom_row, int width, char *dst,
                                   size_t cap) {

  // OPTIMIZATION #4: Use 256-color fast path by default (huge speed boost!)
  if (g_use_256_color_fast_path) {
    return render_row_upper_half_block_256color(top_row, bottom_row, width, dst, cap);
  }

  // Fallback to truecolor for high quality mode

  char *p = dst;

  // OPTIMIZATION #3: Compute per-row sentinel once (kill per-pixel capacity checks)
  // Worst case: every pixel changes FG+BG color + has ▀ UTF-8 char (3 bytes)
  const size_t max_per_pixel = 39 + 3;                   // Max FG+BG SGR + UTF-8 ▀
  const size_t row_max_size = width * max_per_pixel + 4; // +4 for reset
  char *row_end = dst + ((cap < row_max_size) ? cap : row_max_size);

  // Current colors for run-length encoding
  bool have_color = false;
  uint8_t fg_r = 0, fg_g = 0, fg_b = 0; // Foreground (top pixel)
  uint8_t bg_r = 0, bg_g = 0, bg_b = 0; // Background (bottom pixel)

  for (int x = 0; x < width; ++x) {
    const rgb_pixel_t *top_px = &top_row[x];
    const rgb_pixel_t *bot_px = &bottom_row[x];

    // Check if colors changed (need new escape sequence)
    bool color_changed = !have_color || top_px->r != fg_r || top_px->g != fg_g || top_px->b != fg_b ||
                         bot_px->r != bg_r || bot_px->g != bg_g || bot_px->b != bg_b;

    if (color_changed) {
      // NO MORE per-pixel capacity check - we computed row_end sentinel once!
      // Emit combined FG+BG escape sequence
      p = append_sgr_truecolor_fg_bg(p, top_px->r, top_px->g, top_px->b, bot_px->r, bot_px->g, bot_px->b);

      // Update color state
      fg_r = top_px->r;
      fg_g = top_px->g;
      fg_b = top_px->b;
      bg_r = bot_px->r;
      bg_g = bot_px->g;
      bg_b = bot_px->b;
      have_color = true;
    }

    // Simple bounds check only when approaching row boundary
    if (p >= row_end - 50) { // 50 byte safety margin
      if (p >= row_end)
        break;
    }

    // Add ▀ character (UTF-8: 0xE2 0x96 0x80)
    *p++ = 0xE2;
    *p++ = 0x96;
    *p++ = 0x80;
  }

  // Add reset sequence
  if (row_end - p >= 4)
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
  const size_t max_per_char = 48; // Conservative estimate
  const size_t reset_len = 4;     // \033[0m
  const size_t total_newlines = (output_height > 0) ? (output_height - 1) : 0;
  const size_t buffer_size =
      (size_t)output_height * (size_t)w * max_per_char + (size_t)output_height * reset_len + total_newlines + 1;

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
      bottom_row = top_row; // Duplicate last row for odd heights
    } else {
      bottom_row = (const rgb_pixel_t *)&image->pixels[bottom_row_idx * w];
    }

    // Render this terminal row using ▀ blocks
    size_t row_len = render_row_upper_half_block(top_row, bottom_row, w, ascii + total_len, buffer_size - total_len);
    total_len += row_len;

    // Add newline after each row (except the last row)
    if (y != output_height - 1 && total_len < buffer_size - 1) {
      ascii[total_len++] = '\n';
    }
  }

  ascii[total_len] = '\0';
  return ascii;
}

/* ============================================================================
 * AVX-512 Implementation (64-pixel parallel processing)
 * ============================================================================
 */

#if defined(SIMD_SUPPORT_AVX512) && defined(__AVX512F__) && defined(__AVX512BW__)

// Forward declarations for AVX-512 functions
static size_t convert_row_colored_avx512(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                         bool background_mode);
static size_t convert_row_mono_avx512(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width);

// AVX-512 dispatch function
size_t convert_row_with_color_avx512(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                     bool background_mode) {
  if (opt_color_output || background_mode) {
    return convert_row_colored_avx512(pixels, output_buffer, buffer_size, width, background_mode);
  } else {
    return convert_row_mono_avx512(pixels, output_buffer, buffer_size, width);
  }
}

// TODO: Implement AVX-512 64-pixel parallel monochrome ASCII conversion
static size_t convert_row_mono_avx512(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width) {
  // FUTURE IMPLEMENTATION:
  // - Process 64 pixels per iteration using __m512i vectors
  // - Use _mm512_load_si512 for aligned loads
  // - Implement luminance calculation with _mm512_maddubs_epi16
  // - Use gather/scatter operations for ASCII lookup
  // - Expected performance: 2-4x faster than AVX2 implementation

  // Fallback to scalar implementation for now
  return convert_row_with_color_scalar(pixels, output_buffer, buffer_size, width, false);
}

// TODO: Implement AVX-512 64-pixel parallel colored ASCII conversion
static size_t convert_row_colored_avx512(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                         bool background_mode) {
  // FUTURE IMPLEMENTATION:
  // - Process 64 RGB pixels per iteration
  // - Use __m512i for 64x 8-bit RGB values
  // - Vectorized luminance: (77*R + 150*G + 29*B) >> 8 for 64 pixels
  // - Parallel ASCII character lookup with _mm512_shuffle_epi8
  // - Vectorized ANSI color code generation
  // - Use masked stores to handle variable-width output

  // Fallback to scalar implementation for now
  return convert_row_with_color_scalar(pixels, output_buffer, buffer_size, width, background_mode);
}

#endif // AVX-512 support

/* ============================================================================
 * ARM SVE Implementation (Scalable vector lengths)
 * ============================================================================
 */

#if defined(SIMD_SUPPORT_SVE) && defined(__ARM_FEATURE_SVE)

// Forward declarations for SVE functions
static size_t convert_row_colored_sve(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                      bool background_mode);
static size_t convert_row_mono_sve(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width);

// ARM SVE dispatch function
size_t convert_row_with_color_sve(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                  bool background_mode) {
  if (opt_color_output || background_mode) {
    return convert_row_colored_sve(pixels, output_buffer, buffer_size, width, background_mode);
  } else {
    return convert_row_mono_sve(pixels, output_buffer, buffer_size, width);
  }
}

// TODO: Implement ARM SVE scalable vector monochrome ASCII conversion
static size_t convert_row_mono_sve(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width) {
  // FUTURE IMPLEMENTATION:
  // - Query vector length with svcntb() (typically 256-512 bits)
  // - Process svcntb()/3 RGB pixels per iteration (variable width)
  // - Use svld3_u8 for interleaved RGB loads
  // - Implement luminance with svmla_u16 (multiply-accumulate)
  // - Use svtbl_u8 for ASCII character lookup
  // - Vectorized stores with svst1_u8
  // - Adaptive to different ARM implementations (256-2048 bit vectors)

  // Fallback to scalar implementation for now
  return convert_row_with_color_scalar(pixels, output_buffer, buffer_size, width, false);
}

// TODO: Implement ARM SVE scalable vector colored ASCII conversion
static size_t convert_row_colored_sve(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                      bool background_mode) {
  // FUTURE IMPLEMENTATION:
  // - Query vector length and process accordingly
  // - Use predicated operations for handling partial vectors
  // - Scalable luminance calculation: (77*R + 150*G + 29*B) >> 8
  // - Table lookups for ASCII characters with svtbl_u8
  // - Vectorized ANSI color sequence generation
  // - Use SVE gather-scatter for non-contiguous memory access
  // - Performance scales with vector width (future-proof)

  // Fallback to scalar implementation for now
  return convert_row_with_color_scalar(pixels, output_buffer, buffer_size, width, background_mode);
}

#endif // ARM SVE support

// ============================================================================
// Scalar Unified REP Implementations (for NEON dispatcher fallback)
// ============================================================================

// Scalar version for fallback use (palette256_index equivalent)
static inline uint8_t palette256_index_scalar(uint8_t r, uint8_t g, uint8_t b) {
  // Simple 6x6x6 cube quantization: (r/51)*36 + (g/51)*6 + (b/51) + 16
  // Use fast multiply-shift instead of division: /51 ≈ *5 >> 8
  uint8_t cr = (r * 5) >> 8; // 0..5
  uint8_t cg = (g * 5) >> 8; // 0..5
  uint8_t cb = (b * 5) >> 8; // 0..5

  // Clamp to valid range
  if (cr > 5)
    cr = 5;
  if (cg > 5)
    cg = 5;
  if (cb > 5)
    cb = 5;

  return 16 + cr * 36 + cg * 6 + cb; // 16..231 (216 colors)
}

size_t render_row_256color_background_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap) {
  init_dec3();

  uint8_t *bg_idx;
  SAFE_MALLOC(bg_idx, (size_t)width * sizeof(uint8_t), uint8_t *);

  // Convert RGB to 256-color indices using NEON where possible
#ifdef SIMD_SUPPORT_NEON
  int i = 0;
  // Process 16-pixel chunks with NEON
  for (; i + 16 <= width; i += 16) {
    uint8x16x3_t rgb = vld3q_u8((const uint8_t *)&row[i]);
    uint8x16_t indices = palette256_index_dithered(rgb.val[0], rgb.val[1], rgb.val[2], i);
    vst1q_u8(&bg_idx[i], indices);
  }
  // Handle remaining pixels with scalar fallback
  for (; i < width; i++) {
    bg_idx[i] = palette256_index_scalar(row[i].r, row[i].g, row[i].b);
  }
#endif

  // Use unified REP compression (UTF-8 block mode, 256-color)
  size_t result = write_row_rep_from_arrays_enhanced(
      NULL, NULL, NULL,                    // No FG RGB (background mode uses fixed FG)
      NULL, NULL, NULL,                    // No BG RGB (using indices)
      NULL, bg_idx,                        // 256-color BG indices
      NULL,                                // UTF-8 block mode (no ASCII chars)
      width, dst, cap, true, true, false); // utf8_block_mode=true, use_256color=true, is_truecolor=false

  SAFE_FREE(bg_idx);
  return result;
}

size_t render_row_truecolor_background_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap) {
  uint8_t *bg_r, *bg_g, *bg_b;
  uint8_t *fg_r, *fg_g, *fg_b;
  SAFE_MALLOC(bg_r, (size_t)width * sizeof(uint8_t), uint8_t *);
  SAFE_MALLOC(bg_g, (size_t)width * sizeof(uint8_t), uint8_t *);
  SAFE_MALLOC(bg_b, (size_t)width * sizeof(uint8_t), uint8_t *);
  SAFE_MALLOC(fg_r, (size_t)width * sizeof(uint8_t), uint8_t *);
  SAFE_MALLOC(fg_g, (size_t)width * sizeof(uint8_t), uint8_t *);
  SAFE_MALLOC(fg_b, (size_t)width * sizeof(uint8_t), uint8_t *);

  // Fill arrays with pixel data and determine FG text color based on luminance
  for (int i = 0; i < width; i++) {
    bg_r[i] = row[i].r;
    bg_g[i] = row[i].g;
    bg_b[i] = row[i].b;

    uint8_t luma = (uint8_t)((LUMA_RED * row[i].r + LUMA_GREEN * row[i].g + LUMA_BLUE * row[i].b) >> 8);
    bool use_black_text = (luma >= BGASCII_LUMA_THRESHOLD);
    uint8_t text_color = use_black_text ? 0 : 255;

    fg_r[i] = text_color;
    fg_g[i] = text_color;
    fg_b[i] = text_color;
  }

  // Use unified REP compression with truecolor RGB arrays
  size_t result = write_row_rep_from_arrays_enhanced(fg_r, fg_g, fg_b, bg_r, bg_g, bg_b, NULL, NULL, NULL, width, dst,
                                                     cap, true, false, true);

  SAFE_FREE(bg_r);
  SAFE_FREE(bg_g);
  SAFE_FREE(bg_b);
  SAFE_FREE(fg_r);
  SAFE_FREE(fg_g);
  SAFE_FREE(fg_b);

  return result;
}

size_t render_row_256color_foreground_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap) {
  init_palette();
  init_dec3();

  uint8_t *fg_idx;
  char *ascii_chars;
  SAFE_MALLOC(fg_idx, (size_t)width * sizeof(uint8_t), uint8_t *);
  SAFE_MALLOC(ascii_chars, (size_t)width * sizeof(char), char *);

  // Convert RGB to 256-color indices and ASCII characters using NEON where possible
#ifdef SIMD_SUPPORT_NEON
  int i = 0;
  // Process 16-pixel chunks with NEON
  for (; i + 16 <= width; i += 16) {
    uint8x16x3_t rgb = vld3q_u8((const uint8_t *)&row[i]);
    uint8x16_t indices = palette256_index_dithered(rgb.val[0], rgb.val[1], rgb.val[2], i);
    vst1q_u8(&fg_idx[i], indices);

    // Calculate luminance for ASCII characters: Y = (77R + 150G + 29B) >> 8
    uint16x8_t r_lo = vmovl_u8(vget_low_u8(rgb.val[0]));
    uint16x8_t r_hi = vmovl_u8(vget_high_u8(rgb.val[0]));
    uint16x8_t g_lo = vmovl_u8(vget_low_u8(rgb.val[1]));
    uint16x8_t g_hi = vmovl_u8(vget_high_u8(rgb.val[1]));
    uint16x8_t b_lo = vmovl_u8(vget_low_u8(rgb.val[2]));
    uint16x8_t b_hi = vmovl_u8(vget_high_u8(rgb.val[2]));

    uint16x8_t luma_lo = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_lo, LUMA_RED), g_lo, LUMA_GREEN), b_lo, LUMA_BLUE);
    uint16x8_t luma_hi = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_hi, LUMA_RED), g_hi, LUMA_GREEN), b_hi, LUMA_BLUE);

    uint8x16_t luma = vcombine_u8(vshrn_n_u16(luma_lo, 8), vshrn_n_u16(luma_hi, 8));

    // Store luminance values temporarily and convert to ASCII characters
    uint8_t luma_values[16];
    vst1q_u8(luma_values, luma);
    for (int j = 0; j < 16 && i + j < width; j++) {
      ascii_chars[i + j] = luminance_palette[luma_values[j]];
    }
  }
  // Handle remaining pixels with scalar fallback
  for (; i < width; i++) {
    fg_idx[i] = palette256_index_scalar(row[i].r, row[i].g, row[i].b);
    uint8_t luma = (uint8_t)((LUMA_RED * row[i].r + LUMA_GREEN * row[i].g + LUMA_BLUE * row[i].b) >> 8);
    ascii_chars[i] = luminance_palette[luma];
  }
#endif

  // Use unified REP compression (FG 256-color + ASCII chars)
  size_t result = write_row_rep_from_arrays_enhanced(
      NULL, NULL, NULL,                     // No truecolor FG RGB
      NULL, NULL, NULL,                     // No BG RGB
      fg_idx, NULL,                         // 256-color FG indices
      ascii_chars,                          // ASCII characters
      width, dst, cap, false, true, false); // utf8_block_mode=false, use_256color=true, is_truecolor=false

  SAFE_FREE(fg_idx);
  SAFE_FREE(ascii_chars);
  return result;
}

size_t render_row_truecolor_foreground_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap) {
  init_palette();

  uint8_t *fg_r, *fg_g, *fg_b;
  char *ascii_chars;
  SAFE_MALLOC(fg_r, (size_t)width * sizeof(uint8_t), uint8_t *);
  SAFE_MALLOC(fg_g, (size_t)width * sizeof(uint8_t), uint8_t *);
  SAFE_MALLOC(fg_b, (size_t)width * sizeof(uint8_t), uint8_t *);
  SAFE_MALLOC(ascii_chars, (size_t)width * sizeof(char), char *);

  // Fill arrays with pixel data and ASCII characters
  for (int i = 0; i < width; i++) {
    fg_r[i] = row[i].r;
    fg_g[i] = row[i].g;
    fg_b[i] = row[i].b;

    uint8_t luma = (uint8_t)((LUMA_RED * row[i].r + LUMA_GREEN * row[i].g + LUMA_BLUE * row[i].b) >> 8);
    ascii_chars[i] = luminance_palette[luma];
  }

  // Use unified REP compression with truecolor RGB arrays and ASCII chars
  size_t result = write_row_rep_from_arrays_enhanced(fg_r, fg_g, fg_b, NULL, NULL, NULL, NULL, NULL, ascii_chars, width,
                                                     dst, cap, false, false, true);

  SAFE_FREE(fg_r);
  SAFE_FREE(fg_g);
  SAFE_FREE(fg_b);
  SAFE_FREE(ascii_chars);

  return result;
}

// Auto-dispatch optimized color function - calls best available SIMD implementation. plan to support more than just
// NEON in the future.
size_t convert_row_with_color_optimized(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                        bool background_mode) {
#ifdef SIMD_SUPPORT_NEON
  // Use NEON optimized renderer
  bool use_fast_path = get_256_color_fast_path(); // Get quality setting
  return render_row_ascii_rep_dispatch_neon(pixels, width, output_buffer, buffer_size, background_mode, use_fast_path);
#else
  // Fallback to scalar implementation
  return convert_row_with_color_scalar(pixels, output_buffer, buffer_size, width, background_mode);
#endif
}

/*
 * Dispatcher
 */
// Unified SIMD + scalar REP dispatcher. will support more than just NEON in the future.
size_t render_row_color_ascii_dispatch(const rgb_pixel_t *row, int width, char *dst, size_t cap, bool background_mode,
                                       bool use_fast_path) {
#ifdef SIMD_SUPPORT_NEON
  // Use NEON SIMD REP versions when available
  if (background_mode) {
    if (use_fast_path) {
      // 256-color background (▀)
      return render_row_neon_256_bg_block_rep(row, width, dst, cap);
    } else {
      // Truecolor background (▀)
      return render_row_neon_truecolor_bg_block_rep(row, width, dst, cap);
    }
  } else {
    if (use_fast_path) {
      // 256-color foreground ASCII
      return render_row_neon_256_fg_rep(row, width, dst, cap);
    } else {
      // Truecolor foreground ASCII
      return render_row_neon_truecolor_fg_rep(row, width, dst, cap);
    }
  }
#endif

  // Scalar fallback (scalar REP code)
  if (background_mode) {
    if (use_fast_path) {
      return render_row_256color_background_rep_unified(row, width, dst, cap);
    } else {
      return render_row_truecolor_background_rep_unified(row, width, dst, cap);
    }
  } else {
    if (use_fast_path) {
      return render_row_256color_foreground_rep_unified(row, width, dst, cap);
    } else {
      return render_row_truecolor_foreground_rep_unified(row, width, dst, cap);
    }
  }
}
