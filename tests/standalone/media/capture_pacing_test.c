#include "session/capture_pacing.h"
#include <stdio.h>
#include <stdint.h>

#define CHECK(expr)                                                                                                                                                                                    \
  do {                                                                                                                                                                                                 \
    if (!(expr)) {                                                                                                                                                                                     \
      fprintf(stderr, "failed: %s\n", #expr);                                                                                                                                                          \
      return 1;                                                                                                                                                                                        \
    }                                                                                                                                                                                                  \
  } while (0)

int main(void) {
  const uint64_t interval = 100;
  CHECK(capture_pacing_deadline(0, 1000, interval) == 1100);
  CHECK(capture_pacing_deadline(1100, 1200, interval) == 1200);
  CHECK(capture_pacing_deadline(1100, 1201, interval) == 1301);
  CHECK(capture_pacing_deadline(1200, 1250, interval) == 1300);
  // A blocking capture read can miss several pacing slots. The next frame
  // must still wait one full interval; never return an already-expired
  // deadline that causes an immediate retry.
  CHECK(capture_pacing_deadline(1300, 1760, interval) == 1860);
  CHECK(capture_pacing_deadline(1700, 1760, interval) == 1800);
  return 0;
}
