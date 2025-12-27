/*
 * Wrapper stdbool.h that provides C99 bool without __has_include_next issues.
 *
 * This is used by the defer tool to avoid LibTooling errors when clang's
 * builtin stdbool.h uses __has_include_next(<stdbool.h>) which can error
 * when LibTooling evaluates it and can't find a system stdbool.h.
 */
#ifndef __STDBOOL_WRAPPER_H
#define __STDBOOL_WRAPPER_H

#define __bool_true_false_are_defined 1

#if defined(__STDC_VERSION__) && __STDC_VERSION__ > 201710L
/* C23+ has bool as a keyword */
#elif !defined(__cplusplus)
#define bool _Bool
#define true 1
#define false 0
#elif defined(__GNUC__) && !defined(__STRICT_ANSI__)
#define _Bool bool
#if defined(__cplusplus) && __cplusplus < 201103L
#define bool bool
#define false false
#define true true
#endif
#endif

#endif /* __STDBOOL_WRAPPER_H */
