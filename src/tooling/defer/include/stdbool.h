/**
 * @file tooling/defer/include/stdbool.h
 * @ingroup tooling
 * @brief ClangTool/LibTooling compatibility shim for stdbool.h
 *
 * Provides a workaround for a LibTooling bug that prevents use of the standard
 * clang stdbool.h header. This compatibility shim provides the same interface
 * without using the problematic `__has_include_next` preprocessor operator.
 *
 * ## Problem Statement
 *
 * Clang's LibTooling (used by ascii-instr-panic for code instrumentation) has
 * a bug where `__has_include_next` errors instead of returning false when a
 * next header doesn't exist. This causes the standard clang stdbool.h to fail
 * on macOS and other platforms, breaking instrumentation on any code that
 * includes stdbool.h.
 *
 * ## Solution
 *
 * This header provides a simplified stdbool.h implementation that:
 * - Defines `bool`, `true`, and `false` for C standards before C23
 * - Handles C++98 and C++11+ compatibility
 * - Avoids all problematic preprocessor checks
 * - Maintains compatibility with the standard stdbool.h interface
 *
 * ## Usage
 *
 * This header is automatically injected into the include path when using
 * ascii-instr-panic for instrumentation. No explicit includes are needed.
 *
 * ## Scope
 *
 * Works with:
 * - C99, C11, C17, C23 (and later standards)
 * - C++98, C++11, C++14, C++17, C++20, C++23 (and later standards)
 * - GCC, Clang, and other C-compatible compilers
 * - macOS, Linux, Windows, and other platforms
 */

/*===---- stdbool.h - ClangTool workaround header --------------------------===
 *
 * This is a simplified stdbool.h that doesn't use __has_include_next.
 * ClangTool/LibTooling has a bug where __has_include_next errors instead of
 * returning false when a header doesn't exist. This causes the standard
 * clang stdbool.h to fail on macOS.
 *
 * This header provides the same functionality as the standard stdbool.h
 * without the problematic __has_include_next check.
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __STDBOOL_H
#define __STDBOOL_H

#define __bool_true_false_are_defined 1

#if defined(__STDC_VERSION__) && __STDC_VERSION__ > 201710L
/* C23+ defines bool, true, false as keywords */
#elif !defined(__cplusplus)
#define bool _Bool
#define true 1
#define false 0
#elif defined(__GNUC__) && !defined(__STRICT_ANSI__)
/* Define _Bool as a GNU extension for C++ */
#define _Bool bool
#if defined(__cplusplus) && __cplusplus < 201103L
/* For C++98, define bool, false, true as a GNU extension. */
#define bool bool
#define false false
#define true true
#endif
#endif

#endif /* __STDBOOL_H */
