/**
 * @file tooling/defer/defer.h
 * @brief Marker for build-time block-scoped cleanup statements.
 *
 * defer(expression) and defer({ statements; }) run when the enclosing block
 * exits, in reverse registration order. The build transforms each invocation
 * before compilation. A direct compilation fails rather than silently omitting
 * cleanup. The parsing-only definition lets libclang inspect the source.
 */
#pragma once

#ifdef ASCIICHAT_DEFER_PARSING
#define defer(...) do { } while (0)
#else
#define defer(...) _Static_assert(0, "defer() requires the source transformation")
#endif
