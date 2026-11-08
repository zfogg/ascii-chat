/**
 * @file tooling/defer/defer.h
 * @brief Marker for build-time block-scoped cleanup statements.
 *
 * defer(expression) and defer({ statements; }) run when the enclosing block
 * exits, in reverse registration order. The build transforms each invocation
 * before compilation. A direct compilation fails rather than silently omitting
 * cleanup. The parsing-only definition lets libclang inspect the source.
 * defer_named(handle, action) registers cancellable cleanup; defer_cancel(handle)
 * cancels the action associated with that lexical name.
 * SAFE_MALLOC_DEFER_FREE(name, size, type) and
 * SAFE_CALLOC_DEFER_FREE(name, count, size, type) declare a pointer and defer
 * SAFE_FREE(name). Include common.h for the allocation and free macros.
 */
#pragma once

#ifdef ASCIICHAT_DEFER_PARSING
#define defer(...)                                                                                                     \
  do {                                                                                                                 \
  } while (0)
#define defer_named(name, ...)                                                                                         \
  do {                                                                                                                 \
  } while (0)
#define defer_cancel(name)                                                                                             \
  do {                                                                                                                 \
  } while (0)
#define SAFE_MALLOC_DEFER_FREE(name, size, cast) cast name = SAFE_MALLOC(size, cast)
#define SAFE_CALLOC_DEFER_FREE(name, count, size, cast) cast name = SAFE_CALLOC(count, size, cast)
#else
#define defer(...) _Static_assert(0, "defer() requires the source transformation")
#define defer_named(name, ...) _Static_assert(0, "defer_named() requires the source transformation")
#define defer_cancel(name) _Static_assert(0, "defer_cancel() requires the source transformation")
#define SAFE_MALLOC_DEFER_FREE(name, size, cast)                                                                       \
  _Static_assert(0, "SAFE_MALLOC_DEFER_FREE() requires the source transformation");                                    \
  cast name = SAFE_MALLOC(size, cast)
#define SAFE_CALLOC_DEFER_FREE(name, count, size, cast)                                                                \
  _Static_assert(0, "SAFE_CALLOC_DEFER_FREE() requires the source transformation");                                    \
  cast name = SAFE_CALLOC(count, size, cast)
#endif
