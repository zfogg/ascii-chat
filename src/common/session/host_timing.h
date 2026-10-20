#pragma once

#include <ascii-chat/util/time.h>

/* Preserve the 50 Hz sample clock across scheduler overshoot. Long stalls
 * resynchronize rather than sending an unbounded burst of overdue packets. */
static inline bool session_host_audio_due(uint64_t *deadline, uint64_t now) {
  const uint64_t interval = 20 * NS_PER_MS_INT;
  if (!*deadline) {
    *deadline = now + interval;
    return false;
  }
  if (now < *deadline)
    return false;
  *deadline = now - *deadline > 100 * NS_PER_MS_INT ? now + interval : *deadline + interval;
  return true;
}
