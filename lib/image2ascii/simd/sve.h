#pragma once

#include "common.h"

#if defined(SIMD_SUPPORT_SVE) && defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>

// ARM SVE-specific function declarations
size_t convert_row_with_color_sve(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                  bool background_mode);

#endif /* SIMD_SUPPORT_SVE && __ARM_FEATURE_SVE */