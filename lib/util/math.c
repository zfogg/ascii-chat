#include "common.h"
#include "util/math.h"

#define CHAR_ASPECT 2.0f // terminal cell height ÷ width

// Constants for aspect ratio calculations
enum {
  MIN_DIMENSION = 1 // minimum width/height to prevent zero dimensions
};

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

// Aspect ratio functions are defined in util/aspect_ratio.c
