#pragma once

#include <libplatform/thread.h>
#include <stdbool.h>

// Catch2 assertions must remain on the test thread. A worker callback cannot
// safely call REQUIRE/CHECK, so terminate the worker on failure; the owning
// test must validate the resulting state after joining it.
#define ASCIICHAT_WORKER_ASSERT(condition)                                                                                    \
  do {                                                                                                                 \
    if (!(condition))                                                                                                  \
      thread_exit(NULL);                                                                                               \
  } while (0)
