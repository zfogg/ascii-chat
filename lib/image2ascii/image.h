#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "platform/abstraction.h"
#include "common.h"

typedef struct rgb_t {
  uint8_t r, g, b;
} PACKED_ATTR rgb_t;

// SIMD-aligned RGB pixel structure for optimal NEON/AVX performance
typedef struct rgb_pixel_simd_t {
  uint8_t r, g, b;
  uint8_t padding; // Align to 4-byte boundary for efficient SIMD access
} ALIGNED_ATTR(16) rgb_pixel_simd_t;

typedef struct image_t {
  int w, h;
  rgb_t *pixels;
} image_t;

// 4K resolution
#define IMAGE_MAX_WIDTH 3840
#define IMAGE_MAX_HEIGHT 2160
#define IMAGE_MAX_PIXELS_SIZE (IMAGE_MAX_WIDTH * IMAGE_MAX_HEIGHT * sizeof(rgb_t))

// Standard allocation (malloc/free)
image_t *image_new(size_t, size_t);
void image_destroy(image_t *);

// Buffer pool allocation (for video pipeline - consistent memory management)
image_t *image_new_from_pool(size_t width, size_t height);
void image_destroy_to_pool(image_t *image);
void image_clear(image_t *);
char *image_print(const image_t *, const char *palette);
char *image_print_color(const image_t *, const char *palette);

// Capability-aware image printing functions
#include "terminal_detect.h"
char *image_print_with_capabilities(const image_t *image, const terminal_capabilities_t *caps, const char *palette,
                                    const char luminance_palette[256]);
char *image_print_256color(const image_t *image, const char *palette);
char *image_print_16color(const image_t *image, const char *palette);
char *image_print_16color_dithered(const image_t *image, const char *palette);
char *image_print_16color_dithered_with_background(const image_t *image, bool use_background, const char *palette);

void quantize_color(int *r, int *g, int *b, int levels);
void image_resize(const image_t *, image_t *);
void image_resize_interpolation(const image_t *source, image_t *dest);

void precalc_rgb_palettes(const float, const float, const float);

// Color support functions
char *rgb_to_ansi_fg(int r, int g, int b);
char *rgb_to_ansi_bg(int r, int g, int b);
void rgb_to_ansi_8bit(int r, int g, int b, int *fg_code, int *bg_code);
