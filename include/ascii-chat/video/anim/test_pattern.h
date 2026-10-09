#pragma once
#include <ascii-chat/video/rgba/image.h>

typedef struct test_pattern test_pattern_t;

/** Independent, reusable RGB renderer. Time is elapsed milliseconds, not frame count. */
asciichat_error_t test_pattern_create(int width, int height, test_pattern_t **out);
asciichat_error_t test_pattern_resize(test_pattern_t *pattern, int width, int height);
asciichat_error_t test_pattern_render(test_pattern_t *pattern, int index, double time_ms, bool cadence);
/** Reusable RGBA view for browser upload; invalidated by resize/destroy. */
const uint8_t *test_pattern_rgba(test_pattern_t *pattern);
image_t *test_pattern_image(test_pattern_t *pattern);
void test_pattern_destroy(test_pattern_t *pattern);
