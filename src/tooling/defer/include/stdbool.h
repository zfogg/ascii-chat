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
