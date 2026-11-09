#pragma once
#include <stddef.h>

/** Non-copyable process budget lease for platform-owned storage. */
/**
 * @brief Owns the temporary storage used by buffer budget. The data pointers and size or cursor fields describe the live portion of the allocation; callers must use the type's cleanup path when the buffer or queue is no longer needed.
 */
typedef struct {
  size_t bytes;
} buffer_budget_t;
