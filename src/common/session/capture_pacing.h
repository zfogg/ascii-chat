#pragma once
#include <stdint.h>

// Keep the last deadline, but rebase after a late blocking read. Returning a
// deadline in the past makes the next iteration immediately call the blocking
// capture API again, which can collapse a nominal 33 FPS loop to the camera's
// much slower retry cadence. The caller must provide interval > 0.
static inline uint64_t capture_pacing_deadline(uint64_t previous, uint64_t now, uint64_t interval) {
  uint64_t deadline = previous ? previous + interval : now + interval;
  if (now > deadline)
    deadline = now + interval;
  return deadline;
}
