/** @file video/ascii/neon/common.c
 * NEON pixel operations. ANSI sequence encoding lives in the shared SGR module.
 */
#if SIMD_SUPPORT_NEON
#include <arm_neon.h>
#include <ascii-chat/video/ascii/neon.h>
#include <ascii-chat/video/ascii/common.h>

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
#if defined(__aarch64__)
  uint8x16_t dither_values = vqtbl1q_u8(dither_matrix, position_vec);
#else
  uint8x8x2_t table = {{vget_low_u8(dither_matrix), vget_high_u8(dither_matrix)}};
  uint8x16_t dither_values =
      vcombine_u8(vtbl2_u8(table, vget_low_u8(position_vec)), vtbl2_u8(table, vget_high_u8(position_vec)));
#endif

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

/**
 * @brief Flip image horizontally using NEON acceleration
 *
 * Reverses each row of the image using NEON v128 registers for fast
 * parallel byte swapping. Processes 16 bytes at a time.
 */
void image_flip_horizontal_neon(image_t *image) {
  if (!image || !image->pixels || image->w < 2) {
    return;
  }

  // Process each row - swap pixels from both ends using NEON for faster loads/stores
  for (int y = 0; y < image->h; y++) {
    rgb_pixel_t *row = &image->pixels[y * image->w];
    int width = image->w;

    // NEON-accelerated swapping: process 4 pixels at a time using uint32 loads
    // Each RGB pixel is 3 bytes, so 4 pixels = 12 bytes that can be loaded as 3x u32
    int left_pix = 0;
    int right_pix = width - 1;

    // Fast path: swap 4-pixel groups using uint32 operations
    while (left_pix + 3 < right_pix - 3) {
      // Load left 4 pixels (12 bytes) as 3 uint32 values using NEON
      uint32_t *left_ptr = (uint32_t *)&row[left_pix];
      uint32_t *right_ptr = (uint32_t *)&row[right_pix - 3];

      uint32x2_t left_0 = vld1_u32(left_ptr); // first 8 bytes
      uint32_t left_1 = left_ptr[2];          // last 4 bytes

      uint32x2_t right_0 = vld1_u32(right_ptr); // first 8 bytes
      uint32_t right_1 = right_ptr[2];          // last 4 bytes

      // Store swapped using NEON
      vst1_u32(right_ptr, left_0);
      right_ptr[2] = left_1;
      vst1_u32(left_ptr, right_0);
      left_ptr[2] = right_1;

      left_pix += 4;
      right_pix -= 4;
    }

    // Scalar cleanup for remaining pixels
    while (left_pix < right_pix) {
      rgb_pixel_t temp = row[left_pix];
      row[left_pix] = row[right_pix];
      row[right_pix] = temp;
      left_pix++;
      right_pix--;
    }
  }
}

#endif // SIMD_SUPPORT_NEON
