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

  // Calculate aspect ratio
  float img_aspect = (float)img_w / (float)img_h;

  // We want the image to fill at least one dimension of the target box
  // while maintaining aspect ratio and fitting within the box

  // Calculate dimensions if we scale to fill width
  ssize_t width_if_fill_width = target_w;
  ssize_t height_if_fill_width = (ssize_t)((float)target_w / img_aspect);

  // Calculate dimensions if we scale to fill height
  ssize_t width_if_fill_height = (ssize_t)((float)target_h * img_aspect);
  ssize_t height_if_fill_height = target_h;

  // Choose the scaling that maximizes usage of the target box
  // We want to fill at least one dimension completely
  if (height_if_fill_width <= target_h) {
    // Filling width fits within height - use it
    *out_width = width_if_fill_width;
    *out_height = height_if_fill_width;
  } else {
    // Filling width would exceed height, so fill height instead
    *out_width = width_if_fill_height;
    *out_height = height_if_fill_height;
  }

  // Ensure minimum dimensions
  if (*out_width <= 0) {
    *out_width = MIN_DIMENSION;
  }
  if (*out_height <= 0) {
    *out_height = MIN_DIMENSION;
  }
}

// Calculate the best dimensions to fit an image in a terminal area while preserving aspect ratio
// Returns the dimensions in characters/pixels (1:1 for our use case with stretch=false)
void calculate_fit_dimensions_pixel(int img_width, int img_height, int max_width, int max_height, int *out_width,
                                    int *out_height) {
  if (!out_width || !out_height || img_width <= 0 || img_height <= 0) {
    if (out_width)
      *out_width = max_width;
    if (out_height)
      *out_height = max_height;
    return;
  }

  float src_aspect = (float)img_width / (float)img_height;

  // Try filling width
  int width_if_fill_w = max_width;
  int height_if_fill_w = (int)((float)max_width / src_aspect + 0.5f);

  // Try filling height
  int width_if_fill_h = (int)((float)max_height * src_aspect + 0.5f);
  int height_if_fill_h = max_height;

  // log_debug("calculate_fit_dimensions: img %dx%d (aspect %.3f), max %dx%d", img_width, img_height, src_aspect,
  //           max_width, max_height);
  // log_debug("  Fill width: %dx%d, Fill height: %dx%d", width_if_fill_w, height_if_fill_w, width_if_fill_h,
  //           height_if_fill_h);

  // Choose the option that fits
  if (height_if_fill_w <= max_height) {
    // Filling width fits
    // log_debug("  Choosing fill width: %dx%d", width_if_fill_w, height_if_fill_w);
    *out_width = width_if_fill_w;
    *out_height = height_if_fill_w;
  } else {
    // Fill height instead
    // log_debug("  Choosing fill height: %dx%d", width_if_fill_h, height_if_fill_h);
    *out_width = width_if_fill_h;
    *out_height = height_if_fill_h;
  }

  // Clamp to bounds
  if (*out_width > max_width)
    *out_width = max_width;
  if (*out_height > max_height)
    *out_height = max_height;
  if (*out_width < 1)
    *out_width = 1;
  if (*out_height < 1)
    *out_height = 1;

  // log_debug("  Final output: %dx%d", *out_width, *out_height);
}
