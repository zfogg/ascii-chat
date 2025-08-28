#pragma once

#include "common.h"

#ifdef SIMD_SUPPORT_SSE2
#include <emmintrin.h>

// SSE2-specific function declarations
void convert_pixels_sse2(const rgb_pixel_t *pixels, char *ascii_chars, int count);
size_t convert_row_with_color_sse2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode);
size_t convert_row_with_color_sse2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars);

#endif /* SIMD_SUPPORT_SSE2 */