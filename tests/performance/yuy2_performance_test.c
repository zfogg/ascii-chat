// Criterion-based performance tests for YUY2 to RGB conversion
// NOTE: YUY2 conversion is Windows-only, so these tests cannot run on Unix

#include <criterion/criterion.h>

#include "common.h"
#include "tests/logging.h"

void yuy2_suite_setup(void) {
  log_set_level(LOG_FATAL);
  test_logging_disable(true, true);
}

void yuy2_suite_teardown(void) {
  log_set_level(LOG_DEBUG);
  test_logging_restore();
}

TestSuite(yuy2_performance, .init = yuy2_suite_setup, .fini = yuy2_suite_teardown);

Test(yuy2_performance, yuy2_windows_only) {
  cr_skip("YUY2 conversion is Windows-only, cannot test on Unix");
}
