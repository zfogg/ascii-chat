#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <string.h>
#include <arm_neon.h>
void simd_luminance_neon(const uint8_t *rgb, uint8_t *out, size_t width) {
  const rgb_pixel_t *row = (const rgb_pixel_t *)rgb;
  size_t x = 0;
  for (; width - x >= 16; x += 16) {
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

    vst1q_u8(out + x, luminance);
  }
  simd_luminance_scalar((const uint8_t *)(row + x), out + x, width - x);
}
