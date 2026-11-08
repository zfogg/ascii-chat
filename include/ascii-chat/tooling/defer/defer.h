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
#else
#define defer(...) _Static_assert(0, "defer() requires the source transformation")
#define defer_named(name, ...) _Static_assert(0, "defer_named() requires the source transformation")
#define defer_cancel(name) _Static_assert(0, "defer_cancel() requires the source transformation")
#endif
