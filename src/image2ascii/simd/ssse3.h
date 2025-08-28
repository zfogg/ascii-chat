#pragma once

#include "common.h"

#ifdef SIMD_SUPPORT_SSSE3
#include <tmmintrin.h>

// SSSE3-specific function declarations
void convert_pixels_ssse3(const rgb_pixel_t *pixels, char *ascii_chars, int count);
size_t convert_row_with_color_ssse3(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                    bool background_mode);

#endif /* SIMD_SUPPORT_SSSE3 */