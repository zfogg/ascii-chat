#include <stdlib.h>
#include "common.h"
#include "options.h"
#include "round.h"

#define CHAR_ASPECT 2.0f // terminal cell height ÷ width
#define MIN_DIMENSION 1  // minimum width/height to prevent zero dimensions

// Helper functions for aspect ratio calculations
static inline int calc_width_from_height(int height, int img_w, int img_h) {
  if (img_h == 0)
    return MIN_DIMENSION;

  float width = (float)height * (float)img_w / (float)img_h * CHAR_ASPECT;
  int result = ROUND(width);
  return (result > 0) ? result : MIN_DIMENSION;
}

static inline int calc_height_from_width(int width, int img_w, int img_h) {
  if (img_w == 0)
    return MIN_DIMENSION;

  float height = ((float)width / CHAR_ASPECT) * (float)img_h / (float)img_w;
  int result = ROUND(height);
  return (result > 0) ? result : MIN_DIMENSION;
}

static void calculate_fit_dimensions(int img_w, int img_h, int max_w, int max_h, ssize_t *out_width,
                                     ssize_t *out_height) {
  if (!out_width || !out_height) {
    log_error("calculate_fit_dimensions: out_width or out_height is NULL");
    return;
  }

  // Calculate both possible dimensions
  int width_from_height = calc_width_from_height(max_h, img_w, img_h);
  int height_from_width = calc_height_from_width(max_w, img_w, img_h);

  // Choose the option that fits within both constraints
  if (width_from_height <= max_w) {
    // Height-constrained: use full height, calculated width
    *out_width = width_from_height;
    *out_height = max_h;
  } else {
    // Width-constrained: use full width, calculated height
    *out_width = max_w;
    *out_height = height_from_width;
  }

  // Final safety check
  if (*out_width <= 0)
    *out_width = MIN_DIMENSION;
  if (*out_height <= 0)
    *out_height = MIN_DIMENSION;
}

void aspect_ratio(const int img_w, const int img_h, ssize_t *out_width, ssize_t *out_height) {
  // Input validation
  if (!out_width || !out_height) {
    return; // or log error
  }

  if (img_w <= 0 || img_h <= 0) {
    // Handle degenerate image dimensions
    *out_width = MIN_DIMENSION;
    *out_height = MIN_DIMENSION;
    return;
  }

  // Early returns for cases where no calculation is needed
  if (opt_stretch || (!auto_width && !auto_height)) {
    return; // Use existing dimensions
  }

  if (auto_width && !auto_height) {
    *out_width = calc_width_from_height(opt_height, img_w, img_h);
    return;
  }

  if (!auto_width && auto_height) {
    *out_height = calc_height_from_width(opt_width, img_w, img_h);
    return;
  }

  // Handle both dimensions automatic - fit within max rectangle
  if (auto_width && auto_height) {
    calculate_fit_dimensions(img_w, img_h, opt_width, opt_height, out_width, out_height);
    return;
  }
}
