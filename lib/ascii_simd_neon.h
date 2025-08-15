#pragma once

#include <stdint.h>
#include <stddef.h>

// Forward declarations
#include "image.h"
#include "ascii_simd.h" // For Str type

// ImageRGB structure for NEON renderers
// Based on usage in ascii_simd_neon.c where img->w, img->h, img->pixels are accessed
typedef struct {
  int w, h;
  uint8_t *pixels; // RGB data: w * h * 3 bytes
} ImageRGB;

// Allocate a new ImageRGB (RGB8), abort on OOM
ImageRGB alloc_image(int w, int h);

// Types defined in ascii_simd.h: Str, RLEState

#if defined(__ARM_NEON) || defined(__aarch64__)

// NEON intrinsics
#include <arm_neon.h>

#endif // __ARM_NEON || __aarch64__

#ifdef SIMD_SUPPORT_NEON
// Configuration constants
#ifndef BGASCII_LUMA_THRESHOLD
#define BGASCII_LUMA_THRESHOLD 128 // Y >= 128 -> black text; else white text
#endif

#ifndef CUBE_GRAY_THRESHOLD
#define CUBE_GRAY_THRESHOLD 10
#endif

// String utility functions
void str_init(Str *s);
void str_free(Str *s);
void str_reserve(Str *s, size_t need);
void str_append_bytes(Str *s, const void *src, size_t n);
void str_append_c(Str *s, char c);
void str_printf(Str *s, const char *fmt, ...);

// ASCII rendering functions
void render_ascii_grayscale(const ImageRGB *img, Str *out);
void render_halfblock_truecolor(const ImageRGB *img, Str *out);
void render_ascii_bgcolor(const ImageRGB *img, Str *out);
void render_ascii_bgcolor_256(const ImageRGB *img, Str *out);
void render_halfblock_256(const ImageRGB *img, Str *out);

// RLE state management
void rle_init(RLEState *st);
void rle_flush(Str *out, RLEState *st, int FR, int FG, int FB, int BR, int BG, int BB);

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

void rgb_to_ansi256_neon(const rgb_pixel_t *pixels, uint8_t *indices);

// NEON palette quantization with ordered dithering
uint8x16_t palette256_index_dithered(uint8x16_t r, uint8x16_t g, uint8x16_t b, int pixel_offset);

// New streaming row renderer - no heap allocation, direct output to dst
size_t convert_row_with_color_neon_streaming(const rgb_pixel_t *px, char *dst, size_t cap, int width, bool bg);

// NEON helper: Process remaining pixels (< 16) efficiently for scalar fallback
void process_remaining_pixels_neon(const rgb_pixel_t *pixels, int count, uint8_t *luminance, char *glyphs);

// Individual NEON renderer functions for direct benchmarking
size_t render_row_neon_256_bg_block_rep(const rgb_pixel_t *pixels, int width, char *dst, size_t cap);
size_t render_row_neon_256_fg_rep(const rgb_pixel_t *pixels, int width, char *dst, size_t cap);
size_t render_row_neon_truecolor_bg_block_rep(const rgb_pixel_t *pixels, int width, char *dst, size_t cap);
size_t render_row_neon_truecolor_fg_rep(const rgb_pixel_t *pixels, int width, char *dst, size_t cap);

// Unified NEON + scalar REP dispatcher
size_t render_row_ascii_rep_dispatch_neon(
    const rgb_pixel_t *row,
    int width,
    char *dst,
    size_t cap,
    bool background_mode,
    bool use_fast_path);

// ARM NEON version for Apple Silicon
void convert_pixels_neon(const rgb_pixel_t *pixels, char *ascii_chars, int count);
size_t convert_row_with_color_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode);
#endif