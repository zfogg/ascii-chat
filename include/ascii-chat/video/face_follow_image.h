#pragma once
#include <ascii-chat/video/rgba/image.h>

typedef struct face_follow_image face_follow_image_t;

/** Output is borrowed until the next call; the source image is never modified. */
image_t *face_follow_image_process(face_follow_image_t **context, image_t *source, double now, bool enabled, bool debug,
                                   int max_width, int max_height);
void face_follow_image_destroy(face_follow_image_t *context);
