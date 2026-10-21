#pragma once

#include <criterion/criterion.h>
#include <ascii-chat/platform/thread.h>
#include <stdbool.h>

// Fatal Criterion assertions unwind to the test thread's jump buffer. Worker
// failures must report nonfatally, then exit their own thread instead.
#define cr_assert_worker(condition)                                                                                    \
  do {                                                                                                                 \
    bool worker_check_passed = !!(condition);                                                                          \
    cr_expect(worker_check_passed, "Worker assertion failed: %s", #condition);                                         \
    if (!worker_check_passed)                                                                                          \
      thread_exit(NULL);                                                                                     \
  } while (0)
