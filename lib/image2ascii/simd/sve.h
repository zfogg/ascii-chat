#pragma once

#include "common.h"

#ifdef SIMD_SUPPORT_SVE
#include <arm_sve.h>

// NEW: Image-based API (matching NEON architecture)
char *render_ascii_image_monochrome_sve(const image_t *image);
char *render_ascii_sve_unified_optimized(const image_t *image, bool use_background, bool use_256color);

#endif /* SIMD_SUPPORT_SVE */
