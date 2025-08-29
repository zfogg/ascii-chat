#pragma once

#include <stdint.h>
#include <stddef.h>

#include "image.h"

#if (defined(SIMD_SUPPORT_NEON) || defined(__ARM_NEON) || defined(__aarch64__))
#include <arm_neon.h>
#endif // SIMD_SUPPORT_NEON || __ARM_NEON || __aarch64__

#if (defined(__ARM_NEON) || defined(__aarch64__))
#ifndef SIMD_SUPPORT_NEON
#define SIMD_SUPPORT_NEON 1
#endif
#endif // __ARM_NEON || __aarch64__

#ifdef SIMD_SUPPORT_NEON

// NEON palette quantization with ordered dithering
uint8x16_t palette256_index_dithered_neon(uint8x16_t r, uint8x16_t g, uint8x16_t b, int pixel_offset);

// {{{ NEW
// Simple monochrome ASCII function that matches scalar image_print()
char *render_ascii_image_monochrome_neon(const image_t *image);

// Unified optimized NEON converter (foreground/background + 256-color/truecolor)
char *render_ascii_neon_unified_optimized(const image_t *image, bool use_background, bool use_256color);

// Half-blocks
char *rgb_to_truecolor_halfblocks_neon(const uint8_t *rgb, int width, int height, int stride_bytes);
/// }}} NEW
#endif
