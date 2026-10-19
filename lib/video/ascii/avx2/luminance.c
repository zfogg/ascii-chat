#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <string.h>
#include <ascii-chat/video/ascii/avx2/common.h>
void simd_luminance_avx2(const uint8_t *rgb, uint8_t *out, size_t width) {
  const rgb_pixel_t *row = (const rgb_pixel_t *)rgb;
  size_t x = 0;
  for (; width - x >= 32; x += 32) {
    avx2_load_rgb32_optimized(row + x, avx2_r_buffer, avx2_g_buffer, avx2_b_buffer);
    avx2_compute_luminance_32(avx2_r_buffer, avx2_g_buffer, avx2_b_buffer, avx2_luminance_buffer);
    memcpy(out + x, avx2_luminance_buffer, 32);
  }
  simd_luminance_scalar((const uint8_t *)(row + x), out + x, width - x);
}
