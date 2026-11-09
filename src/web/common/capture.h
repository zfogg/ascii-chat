/** Browser-facing C interface implemented in capture.c. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int browser_capture_transform_rgba(uint8_t *pixels, int width, int height);

int browser_capture_transform_required(void);

#ifdef __cplusplus
}
#endif
