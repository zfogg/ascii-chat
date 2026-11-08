/** Browser-facing C interface implemented in test_pattern.c. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct test_pattern test_pattern_t;

#ifdef __cplusplus
extern "C" {
#endif

test_pattern_t *browser_test_pattern_create(int width, int height);
const uint8_t *browser_test_pattern_render_rgba(test_pattern_t *pattern, int width, int height, int index,
                                                double time_ms, bool cadence);
void browser_test_pattern_destroy(test_pattern_t *pattern);

#ifdef __cplusplus
}
#endif
