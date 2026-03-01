#pragma once

/**
 * @file video/render/scalar/foreground.h
 * @brief Scalar foreground color ASCII rendering
 * @ingroup video
 *
 * Scalar (non-SIMD) implementations for converting RGB images to
 * ASCII art with foreground colors using various color palettes
 * (greyscale, 256-color, 16-color).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 */

#include <stddef.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/platform/terminal.h>

// Greyscale functions
char *image_print(const image_t *p, const char *palette);
char *image_print_color(const image_t *p, const char *palette);

// 256-color functions
char *image_print_256color(const image_t *p, const char *palette);

// 16-color functions
char *image_print_16color(const image_t *p, const char *palette);

// 16-color dithered functions
char *image_print_16color_dithered(const image_t *p, const char *palette);
char *image_print_16color_dithered_with_background(const image_t *image, bool use_background, const char *palette);

/** @} */
