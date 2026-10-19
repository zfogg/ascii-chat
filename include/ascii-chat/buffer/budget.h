#pragma once
#include <stddef.h>

/** Non-copyable process budget lease for platform-owned storage. */
typedef struct {
  size_t bytes;
} buffer_budget_t;
