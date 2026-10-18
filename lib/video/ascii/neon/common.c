/** @file video/ascii/neon/common.c
 * NEON image operations. Terminal emission is shared by all backends.
 */
#if SIMD_SUPPORT_NEON
#include <arm_neon.h>
#include <ascii-chat/video/ascii/neon.h>

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
