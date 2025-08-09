#include <stdlib.h>
#include <sys/_types/_ssize_t.h>
#include <stdbool.h>
#include "common.h"
#include "round.h"

#define CHAR_ASPECT 2.0f // terminal cell height ÷ width
#define MIN_DIMENSION 1  // minimum width/height to prevent zero dimensions

// Helper functions for aspect ratio calculations
static inline ssize_t calc_width_from_height(ssize_t height, ssize_t img_w, ssize_t img_h) {
  if (img_h == 0) {
    return MIN_DIMENSION;
  }

  float width = (float)height * (float)img_w / (float)img_h * CHAR_ASPECT;
  int result = ROUND(width);
  return (result > 0) ? result : MIN_DIMENSION;
}

static inline ssize_t calc_height_from_width(ssize_t width, ssize_t img_w, ssize_t img_h) {
  if (img_w == 0) {
    return MIN_DIMENSION;
  }

  float height = ((float)width / CHAR_ASPECT) * (float)img_h / (float)img_w;
  int result = ROUND(height);
  return (result > 0) ? result : MIN_DIMENSION;
}

static void calculate_fit_dimensions(ssize_t img_w, ssize_t img_h, ssize_t max_w, ssize_t max_h, ssize_t *out_width,
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
  if (*out_width <= 0) {
    *out_width = MIN_DIMENSION;
  }
  if (*out_height <= 0) {
    *out_height = MIN_DIMENSION;
  }
}

void aspect_ratio(const ssize_t img_w, const ssize_t img_h, const ssize_t width, const ssize_t height,
                  const bool stretch, ssize_t *out_width, ssize_t *out_height) {
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

  if (stretch) {
    // If stretching is enabled, just use the full terminal dimensions
    *out_width = width;
    *out_height = height;
  } else {
    // Calculate aspect-correct dimensions to fit within the given width/height
    calculate_fit_dimensions(img_w, img_h, width, height, out_width, out_height);
  }
}

// Simple aspect ratio calculation without terminal character correction
// Used for image resizing where we want pixel-perfect aspect ratio
void aspect_ratio2(const ssize_t img_w, const ssize_t img_h, const ssize_t target_w, const ssize_t target_h,
                   ssize_t *out_width, ssize_t *out_height) {
  // Input validation
  if (!out_width || !out_height) {
    return;
  }

  if (img_w <= 0 || img_h <= 0 || target_w <= 0 || target_h <= 0) {
    // Handle invalid dimensions
    *out_width = MIN_DIMENSION;
    *out_height = MIN_DIMENSION;
    return;
  }

  // Calculate aspect ratios
  float img_aspect = (float)img_w / (float)img_h;
  float target_aspect = (float)target_w / (float)target_h;

  // Check if we should fit to width or height
  if (target_aspect > img_aspect) {
    // Target is wider than image aspect, fit to height
    *out_height = target_h;
    *out_width = (ssize_t)(target_h * img_aspect);
  } else {
    // Target is taller than or equal to image aspect, fit to width
    *out_width = target_w;
    *out_height = (ssize_t)(target_w / img_aspect);
  }

  // Ensure minimum dimensions
  if (*out_width <= 0) {
    *out_width = MIN_DIMENSION;
  }
  if (*out_height <= 0) {
    *out_height = MIN_DIMENSION;
  }
}
