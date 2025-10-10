#pragma once

#include "platform/abstraction.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @file math.h
 * @brief Mathematical utility functions
 *
 * Provides utilities for rounding, clamping, and aspect ratio calculations.
 */

/* Rounding macro */
#define ROUND(xfloat) (int)(0.5f + (xfloat))

/* RGB value clamping */
static inline uint8_t clamp_rgb(int value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return (uint8_t)value;
}

/**
 * Calculate aspect ratio with terminal character correction
 *
 * @param img_width Input image width
 * @param img_height Input image height
 * @param width Target width in terminal characters
 * @param height Target height in terminal lines
 * @param stretch Whether to allow stretching
 * @param out_width Output width
 * @param out_height Output height
 */
void aspect_ratio(const ssize_t img_width, const ssize_t img_height, const ssize_t width, const ssize_t height,
                  const bool stretch, ssize_t *out_width, ssize_t *out_height);

/**
 * Simple aspect ratio calculation without terminal character correction
 *
 * @param img_width Input image width
 * @param img_height Input image height
 * @param target_width Target width
 * @param target_height Target height
 * @param out_width Output width
 * @param out_height Output height
 */
void aspect_ratio2(const ssize_t img_width, const ssize_t img_height, const ssize_t target_width,
                   const ssize_t target_height, ssize_t *out_width, ssize_t *out_height);

/**
 * Calculate fit dimensions for pixel-based images
 *
 * @param img_width Input image width
 * @param img_height Input image height
 * @param max_width Maximum allowed width
 * @param max_height Maximum allowed height
 * @param out_width Output width
 * @param out_height Output height
 */
void calculate_fit_dimensions_pixel(int img_width, int img_height, int max_width, int max_height, int *out_width,
                                    int *out_height);
