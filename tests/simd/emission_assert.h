#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Run the same cell interpreter without a platform-specific Criterion library.
#define Test(suite, name, ...) static void suite##_##name(void)
#define cr_assert(condition, ...)                                                                                      \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                  \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)
#define cr_assert_eq(a, b, ...) cr_assert((a) == (b))
#define cr_assert_lt(a, b, ...) cr_assert((a) < (b))
#define cr_assert_not_null(a, ...) cr_assert((a) != NULL)
#define cr_assert_null(a, ...) cr_assert((a) == NULL)
#define cr_assert_fail(...) cr_assert(0)
#define cr_assert_str_eq(a, b, ...) cr_assert(strcmp(a, b) == 0)
