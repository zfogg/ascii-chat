/** Browser-facing C interface implemented in face_follow.c. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void face_web_reset(void);
unsigned char *face_web_input(void);
int face_web_validate_input(int width, int height, size_t length);
void face_web_detect(int width, int height, double now);
float *face_web_tick(double now, int enabled);
size_t face_web_output_count(void);

#ifdef __cplusplus
}
#endif
