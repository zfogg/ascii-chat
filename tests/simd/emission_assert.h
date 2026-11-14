#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Run the same cell interpreter without a platform-specific Catch2 library.
#define Test(suite, name, ...) static void suite##_##name(void)
#define ASCIICHAT_REQUIRE(condition, ...)                                                                                      \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                  \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)
#define ASCIICHAT_REQUIRE_EQ(a, b, ...) ASCIICHAT_REQUIRE((a) == (b))
#define ASCIICHAT_REQUIRE_LT(a, b, ...) ASCIICHAT_REQUIRE((a) < (b))
#define ASCIICHAT_REQUIRE_NOT_NULL(a, ...) ASCIICHAT_REQUIRE((a) != NULL)
#define ASCIICHAT_REQUIRE_NULL(a, ...) ASCIICHAT_REQUIRE((a) == NULL)
#define ASCIICHAT_FAIL(...) ASCIICHAT_REQUIRE(0)
#define ASCIICHAT_REQUIRE_STR_EQ(a, b, ...) ASCIICHAT_REQUIRE(strcmp(a, b) == 0)
