#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <string.h>
#include <arm_sve.h>
void simd_luminance_sve(const uint8_t *rgb, uint8_t *out, size_t width) {
  const rgb_pixel_t *row = (const rgb_pixel_t *)rgb;
  size_t x = 0;
  while (x < width) {
    // Widening consumes half the byte lanes; predicate the active halfword lanes.
    size_t process_count = width - x < svcnth() ? width - x : svcnth();
    svbool_t pg_active = svwhilelt_b8((uint64_t)0, (uint64_t)process_count);
    // Retain the renderer's channel scratch layout before widening arithmetic.
    uint8_t r_array[256], g_array[256], b_array[256]; // Max SVE vector size
    for (int j = 0; j < process_count; j++) {
      if (x + j < width) {
        r_array[j] = row[x + j].r;
        g_array[j] = row[x + j].g;
        b_array[j] = row[x + j].b;
      }
    }

    // Load into SVE vectors
    svuint8_t r_vec = svld1_u8(pg_active, r_array);
    svuint8_t g_vec = svld1_u8(pg_active, g_array);
    svuint8_t b_vec = svld1_u8(pg_active, b_array);

    // Convert to 16-bit for arithmetic
    svuint16_t r_16 = svunpklo_u16(r_vec);
    svuint16_t g_16 = svunpklo_u16(g_vec);
    svuint16_t b_16 = svunpklo_u16(b_vec);

    // Calculate luminance: (77*R + 150*G + 29*B + 128) >> 8
    svuint16_t luma = svmul_n_u16_x(svptrue_b16(), r_16, LUMA_RED);
    luma = svmla_n_u16_x(svptrue_b16(), luma, g_16, LUMA_GREEN);
    luma = svmla_n_u16_x(svptrue_b16(), luma, b_16, LUMA_BLUE);
    luma = svadd_n_u16_x(svptrue_b16(), luma, LUMA_THRESHOLD);
    luma = svlsr_n_u16_x(svptrue_b16(), luma, 8);

    // Store u16 luminance values (SVE1 compatible - no SVE2 narrowing intrinsics)
    // After right-shift by 8, values are already in 0-255 range
    uint16_t luma_temp[256];
    svst1_u16(svptrue_b16(), luma_temp, luma);

    // Convert to u8 array for ASCII lookup
    uint8_t luma_array[256];
    for (int j = 0; j < process_count; j++) {
      luma_array[j] = (uint8_t)luma_temp[j];
    }

    memcpy(out + x, luma_array, process_count);
    x += process_count;
  }
}
