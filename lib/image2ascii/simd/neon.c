#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#ifndef SIMD_SUPPORT_NEON
#define SIMD_SUPPORT_NEON 1
#endif
#endif

#include "common.h"
#include "neon.h"
#include "ascii_simd.h"
#include "image.h"

#ifdef SIMD_SUPPORT_NEON // main block of code ifdef

// 256-color palette mapping (RGB to ANSI 256 color index) - local implementation
static inline uint8_t rgb_to_256color(uint8_t r, uint8_t g, uint8_t b) {
  return (uint8_t)(16 + 36 * (r / 51) + 6 * (g / 51) + (b / 51));
}

// Definitions are in ascii_simd.h - just use them
// REMOVED: #define luminance_palette g_ascii_cache.luminance_palette (causes macro expansion issues)

// ------------------------------------------------------------
// Map luminance [0..255] → 4-bit index [0..15] using top nibble
static inline uint8x16_t luma_to_idx_nibble_neon(uint8x16_t y) {
  return vshrq_n_u8(y, 4);
}

// SIMD luma and helpers:

// SIMD luminance: Y = (77R + 150G + 29B) >> 8
static inline uint8x16_t simd_luma_neon(uint8x16_t r, uint8x16_t g, uint8x16_t b) {
  uint16x8_t rl = vmovl_u8(vget_low_u8(r));
  uint16x8_t rh = vmovl_u8(vget_high_u8(r));
  uint16x8_t gl = vmovl_u8(vget_low_u8(g));
  uint16x8_t gh = vmovl_u8(vget_high_u8(g));
  uint16x8_t bl = vmovl_u8(vget_low_u8(b));
  uint16x8_t bh = vmovl_u8(vget_high_u8(b));

  uint32x4_t l0 = vmull_n_u16(vget_low_u16(rl), LUMA_RED);
  uint32x4_t l1 = vmull_n_u16(vget_high_u16(rl), LUMA_RED);
  l0 = vmlal_n_u16(l0, vget_low_u16(gl), LUMA_GREEN);
  l1 = vmlal_n_u16(l1, vget_high_u16(gl), LUMA_GREEN);
  l0 = vmlal_n_u16(l0, vget_low_u16(bl), LUMA_BLUE);
  l1 = vmlal_n_u16(l1, vget_high_u16(bl), LUMA_BLUE);

  uint32x4_t h0 = vmull_n_u16(vget_low_u16(rh), LUMA_RED);
  uint32x4_t h1 = vmull_n_u16(vget_high_u16(rh), LUMA_RED);
  h0 = vmlal_n_u16(h0, vget_low_u16(gh), LUMA_GREEN);
  h1 = vmlal_n_u16(h1, vget_high_u16(gh), LUMA_GREEN);
  h0 = vmlal_n_u16(h0, vget_low_u16(bh), LUMA_BLUE);
  h1 = vmlal_n_u16(h1, vget_high_u16(bh), LUMA_BLUE);

  uint16x8_t l = vcombine_u16(vrshrn_n_u32(l0, 8), vrshrn_n_u32(l1, 8));
  uint16x8_t h = vcombine_u16(vrshrn_n_u32(h0, 8), vrshrn_n_u32(h1, 8));
  return vcombine_u8(vqmovn_u16(l), vqmovn_u16(h));
}

// ===== SIMD helpers for 256-color quantization =====

// NEON: cr=(r*5+127)/255  (nearest of 0..5)
static inline uint8x16_t quant6_neon(uint8x16_t x) {
  uint16x8_t xl = vmovl_u8(vget_low_u8(x));
  uint16x8_t xh = vmovl_u8(vget_high_u8(x));
  uint16x8_t tl = vaddq_u16(vmulq_n_u16(xl, 5), vdupq_n_u16(127));
  uint16x8_t th = vaddq_u16(vmulq_n_u16(xh, 5), vdupq_n_u16(127));
  uint32x4_t tl0 = vmull_n_u16(vget_low_u16(tl), 257);
  uint32x4_t tl1 = vmull_n_u16(vget_high_u16(tl), 257);
  uint32x4_t th0 = vmull_n_u16(vget_low_u16(th), 257);
  uint32x4_t th1 = vmull_n_u16(vget_high_u16(th), 257);
  uint16x8_t ql = vcombine_u16(vshrn_n_u32(tl0, 16), vshrn_n_u32(tl1, 16));
  uint16x8_t qh = vcombine_u16(vshrn_n_u32(th0, 16), vshrn_n_u32(th1, 16));
  return vcombine_u8(vqmovn_u16(ql), vqmovn_u16(qh)); // 0..5
}

// Build 6x6x6 index: cr*36 + cg*6 + cb  (0..215)
static inline uint8x16_t cube216_index_neon(uint8x16_t r6, uint8x16_t g6, uint8x16_t b6) {
  uint16x8_t rl = vmovl_u8(vget_low_u8(r6));
  uint16x8_t rh = vmovl_u8(vget_high_u8(r6));
  uint16x8_t gl = vmovl_u8(vget_low_u8(g6));
  uint16x8_t gh = vmovl_u8(vget_high_u8(g6));
  uint16x8_t bl = vmovl_u8(vget_low_u8(b6));
  uint16x8_t bh = vmovl_u8(vget_high_u8(b6));
  uint16x8_t il = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(rl, 36), gl, 6), bl, 1);
  uint16x8_t ih = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(rh, 36), gh, 6), bh, 1);
  return vcombine_u8(vqmovn_u16(il), vqmovn_u16(ih)); // 0..215
}

// Approximate quantize 0..255 -> 0..5 : q ≈ round(x*5/255) = (x*5 + 128)>>8
static inline uint8x16_t q6_from_u8(uint8x16_t x) {
  uint16x8_t xl = vmovl_u8(vget_low_u8(x));
  uint16x8_t xh = vmovl_u8(vget_high_u8(x));
  xl = vmlaq_n_u16(vdupq_n_u16(0), xl, 5);
  xh = vmlaq_n_u16(vdupq_n_u16(0), xh, 5);
  xl = vaddq_u16(xl, vdupq_n_u16(128));
  xh = vaddq_u16(xh, vdupq_n_u16(128));
  xl = vshrq_n_u16(xl, 8);
  xh = vshrq_n_u16(xh, 8);
  return vcombine_u8(vqmovn_u16(xl), vqmovn_u16(xh)); // 0..5
}

// Make 256-color index (cube vs gray). threshold: max-min < thr ⇒ gray
#ifndef CUBE_GRAY_THRESHOLD
#define CUBE_GRAY_THRESHOLD 10
#endif

// Apply ordered dithering to reduce color variations (creates longer runs)
static inline uint8x16_t apply_ordered_dither(uint8x16_t color, int pixel_offset, uint8_t dither_strength) {
  // Bayer 4x4 dithering matrix (classic ordered dithering pattern)
  static const uint8_t bayer4x4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

  // Load dithering matrix into NEON register
  const uint8x16_t dither_matrix = vld1q_u8(bayer4x4);

  // Create pixel position indices for 16 consecutive pixels
  uint8_t pos_indices[16];
  for (int i = 0; i < 16; i++) {
    pos_indices[i] = (pixel_offset + i) & 15; // Wrap to 4x4 matrix (0-15)
  }
  const uint8x16_t position_vec = vld1q_u8(pos_indices);

  // Lookup dither values for each pixel position using table lookup
  uint8x16_t dither_values = vqtbl1q_u8(dither_matrix, position_vec);

  // Scale dither values by strength (0-255 range)
  // dither_strength controls how much dithering to apply
  uint16x8_t dither_lo = vmulq_n_u16(vmovl_u8(vget_low_u8(dither_values)), dither_strength);
  uint16x8_t dither_hi = vmulq_n_u16(vmovl_u8(vget_high_u8(dither_values)), dither_strength);
  dither_lo = vshrq_n_u16(dither_lo, 4); // Scale down (/16)
  dither_hi = vshrq_n_u16(dither_hi, 4);
  uint8x16_t scaled_dither = vcombine_u8(vqmovn_u16(dither_lo), vqmovn_u16(dither_hi));

  // Apply dithering with saturation to prevent overflow
  return vqaddq_u8(color, scaled_dither);
}

uint8x16_t palette256_index_dithered_neon(uint8x16_t r, uint8x16_t g, uint8x16_t b, int pixel_offset) {
  // Dithering disabled in speed mode (no-op)
  r = apply_ordered_dither(r, pixel_offset, 0);
  g = apply_ordered_dither(g, pixel_offset + 1, 0);
  b = apply_ordered_dither(b, pixel_offset + 2, 0);

  // cube index
  uint8x16_t R6 = q6_from_u8(r);
  uint8x16_t G6 = q6_from_u8(g);
  uint8x16_t B6 = q6_from_u8(b);

  // idx_cube = 16 + R6*36 + G6*6 + B6  (do in 16-bit to avoid overflow)
  uint16x8_t R6l = vmovl_u8(vget_low_u8(R6));
  uint16x8_t R6h = vmovl_u8(vget_high_u8(R6));
  uint16x8_t G6l = vmovl_u8(vget_low_u8(G6));
  uint16x8_t G6h = vmovl_u8(vget_high_u8(G6));
  uint16x8_t B6l = vmovl_u8(vget_low_u8(B6));
  uint16x8_t B6h = vmovl_u8(vget_high_u8(B6));

  uint16x8_t idxl = vmlaq_n_u16(vmulq_n_u16(R6l, 36), G6l, 6);
  uint16x8_t idxh = vmlaq_n_u16(vmulq_n_u16(R6h, 36), G6h, 6);
  idxl = vaddq_u16(idxl, B6l);
  idxh = vaddq_u16(idxh, B6h);
  idxl = vaddq_u16(idxl, vdupq_n_u16(16));
  idxh = vaddq_u16(idxh, vdupq_n_u16(16));

  // gray decision: max-min < thr ?
  uint8x16_t maxrg = vmaxq_u8(r, g);
  uint8x16_t minrg = vminq_u8(r, g);
  uint8x16_t maxrgb = vmaxq_u8(maxrg, b);
  uint8x16_t minrgb = vminq_u8(minrg, b);
  uint8x16_t diff = vsubq_u8(maxrgb, minrgb);
  uint8x16_t thr = vdupq_n_u8((uint8_t)CUBE_GRAY_THRESHOLD);
  uint8x16_t is_gray = vcltq_u8(diff, thr);

  // gray idx = 232 + round(Y*23/255)
  uint8x16_t Y = simd_luma_neon(r, g, b);
  // q23 ≈ round(Y*23/255) = (Y*23 + 128)>>8
  uint16x8_t Yl = vmovl_u8(vget_low_u8(Y));
  uint16x8_t Yh = vmovl_u8(vget_high_u8(Y));
  Yl = vmlaq_n_u16(vdupq_n_u16(0), Yl, 23);
  Yh = vmlaq_n_u16(vdupq_n_u16(0), Yh, 23);
  Yl = vaddq_u16(Yl, vdupq_n_u16(128));
  Yh = vaddq_u16(Yh, vdupq_n_u16(128));
  Yl = vshrq_n_u16(Yl, 8);
  Yh = vshrq_n_u16(Yh, 8);
  uint16x8_t gidxl = vaddq_u16(Yl, vdupq_n_u16(232));
  uint16x8_t gidxh = vaddq_u16(Yh, vdupq_n_u16(232));

  // select gray or cube per lane
  uint8x16_t idx_cube = vcombine_u8(vqmovn_u16(idxl), vqmovn_u16(idxh));
  uint8x16_t idx_gray = vcombine_u8(vqmovn_u16(gidxl), vqmovn_u16(gidxh));
  return vbslq_u8(is_gray, idx_gray, idx_cube);
}

//=============================================================================
// Simple Monochrome ASCII Function (matches scalar image_print performance)
//=============================================================================

char *render_ascii_image_monochrome_neon(const image_t *image, const char *ascii_chars) {
  if (!image || !image->pixels || !ascii_chars) {
    return NULL;
  }

  const int h = image->h;
  const int w = image->w;

  if (h <= 0 || w <= 0) {
    return NULL;
  }

  // Build UTF-8 aware character cache (same as color renderer)
  typedef struct {
    char utf8_bytes[4];  // Up to 4 bytes for UTF-8 character
    uint8_t byte_len;    // Actual length (1-4 bytes)
  } utf8_char_t;
  
  utf8_char_t utf8_char_cache[256];  // 256-entry UTF-8 character cache for direct luminance lookup
  
  // Parse the ASCII characters into UTF-8 aware character boundaries
  int char_count = 0;
  const char *p = ascii_chars;
  
  // Character info for building luminance mapping
  typedef struct {
    const char *start;
    int byte_len;
  } char_info_t;
  
  char_info_t char_infos[256];
  
  while (*p && char_count < 255) {
    char_infos[char_count].start = p;
    
    // Determine UTF-8 character length
    if ((*p & 0x80) == 0) {
      char_infos[char_count].byte_len = 1;
      p++;
    } else if ((*p & 0xE0) == 0xC0) {
      char_infos[char_count].byte_len = 2;
      p += 2;
    } else if ((*p & 0xF0) == 0xE0) {
      char_infos[char_count].byte_len = 3;
      p += 3;
    } else if ((*p & 0xF8) == 0xF0) {
      char_infos[char_count].byte_len = 4;
      p += 4;
    } else {
      char_infos[char_count].byte_len = 1;
      p++;
    }
    char_count++;
  }
  
  // Build 256-entry UTF-8 cache for direct luminance->character mapping
  for (int i = 0; i < 256; i++) {
    // Map luminance 0-255 to character 0-(char_count-1)
    int char_idx = char_count > 1 ? (i * (char_count - 1) + 127) / 255 : 0;
    if (char_idx >= char_count) char_idx = char_count - 1;
    
    // Cache UTF-8 character
    utf8_char_cache[i].byte_len = char_infos[char_idx].byte_len;
    memcpy(utf8_char_cache[i].utf8_bytes, char_infos[char_idx].start, char_infos[char_idx].byte_len);
    if (utf8_char_cache[i].byte_len < 4) {
      utf8_char_cache[i].utf8_bytes[utf8_char_cache[i].byte_len] = '\0';
    }
  }

  // Estimate output buffer size for UTF-8 characters
  const size_t max_char_bytes = 4;  // Max UTF-8 character size
  const size_t len = (size_t)h * ((size_t)w * max_char_bytes + 1);

  char *output;
  SAFE_MALLOC(output, len, char *);

  char *pos = output;
  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  // Pure NEON processing - no scalar fallbacks
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];
    int x = 0;

    // Process 16 pixels at a time with NEON
    for (; x + 15 < w; x += 16) {
      // Load 16 RGB pixels (48 bytes)
      uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(row + x));

      // Calculate luminance for all 16 pixels: (77*R + 150*G + 29*B + 128) >> 8
      uint16x8_t luma_lo = vmull_u8(vget_low_u8(rgb.val[0]), vdup_n_u8(LUMA_RED)); // R * 77
      luma_lo = vmlal_u8(luma_lo, vget_low_u8(rgb.val[1]), vdup_n_u8(LUMA_GREEN)); // + G * 150
      luma_lo = vmlal_u8(luma_lo, vget_low_u8(rgb.val[2]), vdup_n_u8(LUMA_BLUE));  // + B * 29
      luma_lo = vaddq_u16(luma_lo, vdupq_n_u16(128));                              // + 128 (rounding)
      luma_lo = vshrq_n_u16(luma_lo, 8);                                           // >> 8

      uint16x8_t luma_hi = vmull_u8(vget_high_u8(rgb.val[0]), vdup_n_u8(LUMA_RED));
      luma_hi = vmlal_u8(luma_hi, vget_high_u8(rgb.val[1]), vdup_n_u8(LUMA_GREEN));
      luma_hi = vmlal_u8(luma_hi, vget_high_u8(rgb.val[2]), vdup_n_u8(LUMA_BLUE));
      luma_hi = vaddq_u16(luma_hi, vdupq_n_u16(128));
      luma_hi = vshrq_n_u16(luma_hi, 8);

      // Convert 16-bit luminance back to 8-bit
      uint8x16_t luminance = vcombine_u8(vmovn_u16(luma_lo), vmovn_u16(luma_hi));

      // Store luminance values and convert to ASCII characters
      uint8_t luma_array[16];
      vst1q_u8(luma_array, luminance);

      // Convert luminance to UTF-8 characters using UTF-8 cache
      for (int i = 0; i < 16; i++) {
        const utf8_char_t *char_info = &utf8_char_cache[luma_array[i]];
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;
      }
    }

    // Handle remaining pixels with scalar code
    for (; x < w; x++) {
      const rgb_pixel_t pixel = row[x];
      const int luminance = (LUMA_RED * pixel.r + LUMA_GREEN * pixel.g + LUMA_BLUE * pixel.b + 128) >> 8;
      const utf8_char_t *char_info = &utf8_char_cache[luminance];
      memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
      pos += char_info->byte_len;
    }

    // Add newline (except for last row)
    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  // Null terminate
  *pos = '\0';

  return output;
}

//=============================================================================
// Optimized NEON Color Converter (based on ChatGPT reference)
//=============================================================================

// Unified optimized NEON converter (foreground/background + 256-color/truecolor)
char *render_ascii_neon_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int width = image->w;
  const int height = image->h;

  if (width <= 0 || height <= 0) {
    char *empty;
    SAFE_MALLOC(empty, 1, char *);
    empty[0] = '\0';
    return empty;
  }

  outbuf_t ob = {0};
  // Estimate buffer size based on mode
  size_t bytes_per_pixel = use_256color ? 6u : 8u; // 256-color shorter than truecolor
  ob.cap = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 64u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf)
    return NULL;

  // Build UTF-8 aware character cache for fast SIMD lookup
  typedef struct {
    char utf8_bytes[4]; // Up to 4 bytes for UTF-8 character
    uint8_t byte_len;   // Actual length (1-4 bytes)
  } utf8_char_t;

  utf8_char_t utf8_char_cache[64]; // 64-entry UTF-8 character cache
  uint8_t char_index_ramp[64];     // Character indices for vqtbl4q_u8 lookup

  // Parse the ASCII characters into UTF-8 aware character boundaries
  int char_count = 0;
  const char *p = ascii_chars;

  // First pass: count characters and store their positions
  typedef struct {
    const char *start;
    int byte_len;
  } char_info_t;

  char_info_t char_infos[256]; // More than enough

  while (*p && char_count < 255) {
    char_infos[char_count].start = p;

    // Determine UTF-8 character length
    if ((*p & 0x80) == 0) {
      // ASCII character (1 byte)
      char_infos[char_count].byte_len = 1;
      p++;
    } else if ((*p & 0xE0) == 0xC0) {
      // 2-byte UTF-8 character
      char_infos[char_count].byte_len = 2;
      p += 2;
    } else if ((*p & 0xF0) == 0xE0) {
      // 3-byte UTF-8 character
      char_infos[char_count].byte_len = 3;
      p += 3;
    } else if ((*p & 0xF8) == 0xF0) {
      // 4-byte UTF-8 character
      char_infos[char_count].byte_len = 4;
      p += 4;
    } else {
      // Invalid UTF-8, treat as 1 byte
      char_infos[char_count].byte_len = 1;
      p++;
    }
    char_count++;
  }

  // Build character index ramp for vqtbl4q_u8 AND UTF-8 character cache
  for (int i = 0; i < 64; i++) {
    // Map 0-63 to 0-(char_count-1) with proper rounding
    int char_idx = char_count > 1 ? (i * (char_count - 1) + 31) / 63 : 0; // Add 31 for proper rounding
    if (char_idx >= char_count)
      char_idx = char_count - 1;

    // Store cache index for SIMD lookup (direct mapping i -> i)
    char_index_ramp[i] = (uint8_t)i;

    // Cache UTF-8 character for fast emission - store at index i, not char_idx!
    utf8_char_cache[i].byte_len = char_infos[char_idx].byte_len;
    memcpy(utf8_char_cache[i].utf8_bytes, char_infos[char_idx].start, char_infos[char_idx].byte_len);
    // Null terminate for safety
    if (utf8_char_cache[i].byte_len < 4) {
      utf8_char_cache[i].utf8_bytes[utf8_char_cache[i].byte_len] = '\0';
    }
  }

  // Build SIMD lookup table for vqtbl4q_u8 (uses character indices)
  uint8x16x4_t tbl; // 4 * 16 = 64 entries
  tbl.val[0] = vld1q_u8(&char_index_ramp[0]);
  tbl.val[1] = vld1q_u8(&char_index_ramp[16]);
  tbl.val[2] = vld1q_u8(&char_index_ramp[32]);
  tbl.val[3] = vld1q_u8(&char_index_ramp[48]);

  // Track current color state
  int curR = -1, curG = -1, curB = -1;
  int cur_color_idx = -1;

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row = &((const rgb_pixel_t *)image->pixels)[y * width];
    int x = 0;

    // Process 16-pixel chunks with NEON
    while (x + 16 <= width) {
      // Load 16 pixels: R,G,B interleaved
      const uint8_t *p = (const uint8_t *)(row + x);
      uint8x16x3_t pix = vld3q_u8(p); // 48 bytes

      // Vector luminance: Y ≈ (77*R + 150*G + 29*B + 128) >> 8
      uint16x8_t ylo = vmull_u8(vget_low_u8(pix.val[0]), vdup_n_u8(LUMA_RED));
      ylo = vmlal_u8(ylo, vget_low_u8(pix.val[1]), vdup_n_u8(LUMA_GREEN));
      ylo = vmlal_u8(ylo, vget_low_u8(pix.val[2]), vdup_n_u8(LUMA_BLUE));
      ylo = vaddq_u16(ylo, vdupq_n_u16(LUMA_THRESHOLD));
      ylo = vshrq_n_u16(ylo, 8);

      uint16x8_t yhi = vmull_u8(vget_high_u8(pix.val[0]), vdup_n_u8(LUMA_RED));
      yhi = vmlal_u8(yhi, vget_high_u8(pix.val[1]), vdup_n_u8(LUMA_GREEN));
      yhi = vmlal_u8(yhi, vget_high_u8(pix.val[2]), vdup_n_u8(LUMA_BLUE));
      yhi = vaddq_u16(yhi, vdupq_n_u16(LUMA_THRESHOLD));
      yhi = vshrq_n_u16(yhi, 8);

      uint8x16_t y8 = vcombine_u8(vmovn_u16(ylo), vmovn_u16(yhi));
      uint8x16_t idx = vshrq_n_u8(y8, 2); // 0..63

      // FAST: Use vqtbl4q_u8 to get character indices from the ramp
      uint8x16_t char_indices = vqtbl4q_u8(tbl, idx);

      // Spill to arrays for RLE + ANSI emission
      uint8_t char_idx_buf[16], rbuf[16], gbuf_green[16], bbuf[16];
      vst1q_u8(char_idx_buf, char_indices); // Character indices from SIMD lookup
      vst1q_u8(rbuf, pix.val[0]);
      vst1q_u8(gbuf_green, pix.val[1]);
      vst1q_u8(bbuf, pix.val[2]);

      if (use_256color) {
        // 256-color mode processing
        uint8_t color_indices[16];
        for (int i = 0; i < 16; i++) {
          color_indices[i] = rgb_to_256color(rbuf[i], gbuf_green[i], bbuf[i]);
        }

        // Emit with RLE on (UTF-8 character, color) runs using SIMD-derived indices
        for (int i = 0; i < 16;) {
          const uint8_t char_idx = char_idx_buf[i]; // From vqtbl4q_u8 lookup
          const utf8_char_t *char_info = &utf8_char_cache[char_idx];
          const uint8_t color_idx = color_indices[i];

          int j = i + 1;
          while (j < 16 && char_idx_buf[j] == char_idx && color_indices[j] == color_idx) {
            j++;
          }
          const uint32_t run = (uint32_t)(j - i);

          if (color_idx != cur_color_idx) {
            if (use_background) {
              emit_set_256_color_bg(&ob, color_idx);
            } else {
              emit_set_256_color_fg(&ob, color_idx);
            }
            cur_color_idx = color_idx;
          }

          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
            }
          }
          i = j;
        }
      } else {
        // Truecolor mode processing with UTF-8 characters using SIMD-derived indices
        for (int i = 0; i < 16;) {
          const uint8_t char_idx = char_idx_buf[i]; // From vqtbl4q_u8 lookup
          const utf8_char_t *char_info = &utf8_char_cache[char_idx];
          const uint8_t r = rbuf[i];
          const uint8_t g = gbuf_green[i];
          const uint8_t b = bbuf[i];

          int j = i + 1;
          while (j < 16 && char_idx_buf[j] == char_idx && rbuf[j] == r && gbuf_green[j] == g && bbuf[j] == b) {
            j++;
          }
          const uint32_t run = (uint32_t)(j - i);

          if (r != curR || g != curG || b != curB) {
            if (use_background) {
              emit_set_truecolor_bg(&ob, r, g, b);
            } else {
              emit_set_truecolor_fg(&ob, r, g, b);
            }
            curR = r;
            curG = g;
            curB = b;
          }

          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
            }
          }
          i = j;
        }
      }
      x += 16;
    }

    // Scalar tail for remaining pixels
    for (; x < width;) {
      const rgb_pixel_t *p = &row[x];
      uint32_t R = p->r, G = p->g, B = p->b;
      uint8_t Y = (uint8_t)((LUMA_RED * R + LUMA_GREEN * G + LUMA_BLUE * B + 128u) >> 8);
      uint8_t luma_idx = Y >> 2;                    // 0-63 index
      uint8_t char_idx = char_index_ramp[luma_idx]; // Map to character index
      const utf8_char_t *char_info = &utf8_char_cache[char_idx];

      if (use_256color) {
        // 256-color scalar tail
        uint8_t color_idx = rgb_to_256color((uint8_t)R, (uint8_t)G, (uint8_t)B);

        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((LUMA_RED * R2 + LUMA_GREEN * G2 + LUMA_BLUE * B2 + 128u) >> 8);
          uint8_t char_idx2 = char_index_ramp[Y2 >> 2];
          uint8_t color_idx2 = rgb_to_256color((uint8_t)R2, (uint8_t)G2, (uint8_t)B2);
          if (char_idx2 != char_idx || color_idx2 != color_idx)
            break;
          j++;
        }
        uint32_t run = (uint32_t)(j - x);

        if (color_idx != cur_color_idx) {
          if (use_background) {
            emit_set_256_color_bg(&ob, color_idx);
          } else {
            emit_set_256_color_fg(&ob, color_idx);
          }
          cur_color_idx = color_idx;
        }

        // Emit UTF-8 character from cache
        ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          }
        }
        x = j;
      } else {
        // Truecolor scalar tail with UTF-8 characters using cached lookups
        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((LUMA_RED * R2 + LUMA_GREEN * G2 + LUMA_BLUE * B2 + LUMA_THRESHOLD) >> 8);
          uint8_t char_idx2 = char_index_ramp[Y2 >> 2];
          if (char_idx2 != char_idx || R2 != R || G2 != G || B2 != B)
            break;
          j++;
        }
        uint32_t run = (uint32_t)(j - x);

        if ((int)R != curR || (int)G != curG || (int)B != curB) {
          if (use_background) {
            emit_set_truecolor_bg(&ob, (uint8_t)R, (uint8_t)G, (uint8_t)B);
          } else {
            emit_set_truecolor_fg(&ob, (uint8_t)R, (uint8_t)G, (uint8_t)B);
          }
          curR = (int)R;
          curG = (int)G;
          curB = (int)B;
        }

        // Emit UTF-8 character from cache
        ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
          }
        }
        x = j;
      }
    }

    // End row: reset SGR, add newline (except for last row)
    emit_reset(&ob);
    if (y < height - 1) { // Only add newline if not the last row
      ob_putc(&ob, '\n');
    }
    curR = curG = curB = -1;
    cur_color_idx = -1;
  }

  ob_term(&ob);
  return ob.buf;
}

//=============================================================================
// Optimized NEON Half-block renderer (based on ChatGPT reference)
//=============================================================================
char *rgb_to_truecolor_halfblocks_neon(const uint8_t *rgb, int width, int height, int stride_bytes) {
  /* Main: half-block renderer. Returns NUL-terminated malloc'd string; caller free(). */
  if (width <= 0 || height <= 0)
    return strdup("");
  if (stride_bytes <= 0)
    stride_bytes = width * 3;

  outbuf_t ob = {0};
  // generous guess: per cell ~ 10–14 bytes avg; half the rows + newlines
  size_t est_cells = (size_t)width * ((size_t)(height + 1) / 2);
  ob.cap = est_cells * 14u + (size_t)((height + 1) / 2) * 8u + 64u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf)
    return NULL;

  // current SGR state; -1 means unknown
  int cur_fr = -1, cur_fg = -1, cur_fb = -1;
  int cur_br = -1, cur_bg = -1, cur_bb = -1;

  // process two source rows per emitted line
  for (int y = 0; y < height; y += 2) {
    const uint8_t *rowT = rgb + (size_t)y * (size_t)stride_bytes;
    const uint8_t *rowB = (y + 1 < height) ? rowT + (size_t)stride_bytes : NULL;

    int x = 0;
    while (x + 16 <= width) {
      // Load 16 top and bottom pixels (RGB interleaved)
      const uint8_t *pT = rowT + (size_t)x * 3u;
      uint8x16x3_t top = vld3q_u8(pT);

      uint8x16x3_t bot;
      if (rowB) {
        const uint8_t *pB = rowB + (size_t)x * 3u;
        bot = vld3q_u8(pB);
      } else {
        // synthesize bottom = top for odd-height last row
        bot.val[0] = top.val[0];
        bot.val[1] = top.val[1];
        bot.val[2] = top.val[2];
      }

      // Spill to small arrays (cheap; enables simple scalar RLE over 16)
      uint8_t Rt[16], Gt[16], Bt[16], Rb[16], Gb[16], Bb[16];
      vst1q_u8(Rt, top.val[0]);
      vst1q_u8(Gt, top.val[1]);
      vst1q_u8(Bt, top.val[2]);
      vst1q_u8(Rb, bot.val[0]);
      vst1q_u8(Gb, bot.val[1]);
      vst1q_u8(Bb, bot.val[2]);

      // RLE over the 16 cells
      for (int i = 0; i < 16;) {
        uint8_t rT = Rt[i], gT = Gt[i], bT = Bt[i];
        uint8_t rB = Rb[i], gB = Gb[i], bB = Bb[i];

        // Always half-block: U+2580 "▀" (upper half)
        const uint8_t glyph_utf8[3] = {0xE2, 0x96, 0x80};

        // Extend run while next cell has same top+bottom colors
        int j = i + 1;
        for (; j < 16; ++j) {
          if (!(Rt[j] == rT && Gt[j] == gT && Bt[j] == bT && Rb[j] == rB && Gb[j] == gB && Bb[j] == bB))
            break;
        }
        uint32_t run = (uint32_t)(j - i);

        // Check if this is a transparent area (black pixels = padding/background)
        bool is_transparent = (rT == 0 && gT == 0 && bT == 0 && rB == 0 && gB == 0 && bB == 0);

        if (is_transparent) {
          // Reset colors before transparent areas to prevent color bleeding
          if (cur_fr != -1 || cur_fg != -1 || cur_fb != -1 || cur_br != -1 || cur_bg != -1 || cur_bb != -1) {
            emit_reset(&ob);
            cur_fr = cur_fg = cur_fb = -1;
            cur_br = cur_bg = cur_bb = -1;
          }
          // For transparent areas, emit space character with no color codes (terminal default)
          ob_write(&ob, " ", 1);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; ++k) {
              ob_write(&ob, " ", 1);
            }
          }
        } else {
          // Normal colored half-blocks - set fg to TOP, bg to BOTTOM if changed
          if (cur_fr != rT || cur_fg != gT || cur_fb != bT) {
            emit_set_fg(&ob, rT, gT, bT);
            cur_fr = rT;
            cur_fg = gT;
            cur_fb = bT;
          }
          if (cur_br != rB || cur_bg != gB || cur_bb != bB) {
            emit_set_bg(&ob, rB, gB, bB);
            cur_br = rB;
            cur_bg = gB;
            cur_bb = bB;
          }

          // Emit glyph once, then REP or literals
          ob_write(&ob, (const char *)glyph_utf8, 3);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; ++k) {
              ob_write(&ob, (const char *)glyph_utf8, 3);
            }
          }
        }

        i = j;
      }
      x += 16;
    }

    // Scalar tail (or full row if no NEON)
    for (; x < width;) {
      const uint8_t *pT = rowT + (size_t)x * 3u;
      const uint8_t *pB = rowB ? rowB + (size_t)x * 3u : NULL;

      uint8_t rT = pT[0], gT = pT[1], bT = pT[2];
      uint8_t rB = rT, gB = gT, bB = bT;
      if (pB) {
        rB = pB[0];
        gB = pB[1];
        bB = pB[2];
      }

      // Extend run while top and bottom colors match exactly
      int j = x + 1;
      for (; j < width; ++j) {
        const uint8_t *qT = rowT + (size_t)j * 3u;
        const uint8_t *qB = rowB ? rowB + (size_t)j * 3u : NULL;
        uint8_t rT2 = qT[0], gT2 = qT[1], bT2 = qT[2];
        uint8_t rB2 = qB ? qB[0] : rT2, gB2 = qB ? qB[1] : gT2, bB2 = qB ? qB[2] : bT2;
        if (!((rT2 == rT && gT2 == gT && bT2 == bT) && (rB2 == rB && gB2 == gB && bB2 == bB)))
          break;
      }
      uint32_t run = (uint32_t)(j - x);

      // Check if this is a transparent area (black pixels = padding/background)
      bool is_transparent = (rT == 0 && gT == 0 && bT == 0 && rB == 0 && gB == 0 && bB == 0);

      if (is_transparent) {
        // Reset colors before transparent areas to prevent color bleeding
        if (cur_fr != -1 || cur_fg != -1 || cur_fb != -1 || cur_br != -1 || cur_bg != -1 || cur_bb != -1) {
          emit_reset(&ob);
          cur_fr = cur_fg = cur_fb = -1;
          cur_br = cur_bg = cur_bb = -1;
        }
        // For transparent areas, emit space character with no color codes
        ob_write(&ob, " ", 1);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, " ", 1);
          }
        }
      } else {
        // SGR: fg = TOP, bg = BOTTOM for colored areas
        if (cur_fr != rT || cur_fg != gT || cur_fb != bT) {
          emit_set_fg(&ob, rT, gT, bT);
          cur_fr = rT;
          cur_fg = gT;
          cur_fb = bT;
        }
        if (cur_br != rB || cur_bg != gB || cur_bb != bB) {
          emit_set_bg(&ob, rB, gB, bB);
          cur_br = rB;
          cur_bg = gB;
          cur_bb = bB;
        }

        // Always the upper half block "▀" (U+2580)
        static const char HB[3] = {(char)0xE2, (char)0x96, (char)0x80};
        ob_write(&ob, HB, 3);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, HB, 3);
          }
        }
      }

      x = j;
    }

    // End emitted line: reset and newline (only for non-final lines)
    emit_reset(&ob);
    // Check if this is the last output line (since we process 2 pixel rows per output line)
    if (y + 2 < height) { // Only add newline if not the last output line
      ob_putc(&ob, '\n');
    }
    cur_fr = cur_fg = cur_fb = -1;
    cur_br = cur_bg = cur_bb = -1;
  }

  ob_term(&ob);
  return ob.buf;
}
#endif // SIMD_SUPPORT_NEON
