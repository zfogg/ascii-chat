#include <catch2/catch_test_macros.hpp>
#include "session/capture_pacing.h"

TEST_CASE("capture pacing deadlines", "[standalone][media]") {
  const uint64_t interval = 100;
  REQUIRE(capture_pacing_deadline(0, 1000, interval) == 1100);
  REQUIRE(capture_pacing_deadline(1100, 1200, interval) == 1200);
  REQUIRE(capture_pacing_deadline(1100, 1201, interval) == 1301);
  REQUIRE(capture_pacing_deadline(1200, 1250, interval) == 1300);
  // A blocking capture read can miss several pacing slots. The next frame
  // must still wait one full interval; never return an already-expired
  // deadline that causes an immediate retry.
  REQUIRE(capture_pacing_deadline(1300, 1760, interval) == 1860);
  REQUIRE(capture_pacing_deadline(1700, 1760, interval) == 1800);
}
