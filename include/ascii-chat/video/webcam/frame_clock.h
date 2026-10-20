#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Map a live device's 100 ns sample clock to monotonic arrival times. */
typedef struct {
  int64_t sample, previous;
  uint64_t wall;
  bool initialized;
} webcam_frame_clock_t;

/** Age above the fastest observed delivery; reset on either clock discontinuity. */
static inline uint64_t webcam_frame_clock_age(webcam_frame_clock_t *clock, int64_t sample, uint64_t wall) {
  if (!clock->initialized || sample < clock->previous || wall < clock->wall) {
    *clock = (webcam_frame_clock_t){.sample = sample, .previous = sample, .wall = wall, .initialized = true};
    return 0;
  }
  clock->previous = sample;
  // Bias signed device times before subtraction so crossing zero does not wrap.
  uint64_t elapsed_ticks = ((uint64_t)sample ^ (UINT64_C(1) << 63)) - ((uint64_t)clock->sample ^ (UINT64_C(1) << 63));
  uint64_t elapsed_wall = wall - clock->wall;
  if (elapsed_ticks > UINT64_MAX / 100 || elapsed_ticks * 100 > elapsed_wall) {
    clock->sample = sample;
    clock->wall = wall;
    return 0;
  }
  return elapsed_wall - elapsed_ticks * 100;
}
