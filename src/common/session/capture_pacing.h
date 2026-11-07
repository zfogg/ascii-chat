#pragma once
#include <stdint.h>

// Keep the last elapsed deadline so a late blocking read does not make the
// next call skip an additional interval. The caller must provide interval > 0.
static inline uint64_t capture_pacing_deadline(uint64_t previous, uint64_t now, uint64_t interval) {
  uint64_t deadline = previous ? previous + interval : now + interval;
  if (now >= deadline)
    deadline += ((now - deadline) / interval) * interval;
  return deadline;
}
