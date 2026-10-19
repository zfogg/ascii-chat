#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Normalized coordinates in the original, unmirrored source. */
typedef struct {
  float x, y, w, h;
} face_follow_rect_t;

typedef struct {
  face_follow_rect_t box;
  float confidence;
} face_follow_detection_t;

/** One instance per capture source; times are monotonic seconds. */
typedef struct {
  face_follow_rect_t crop, target, face;
  double last_tick, last_seen;
  double pan_outside_since, zoom_outside_since;
  float anchor_x, anchor_y;
  float pan_direction_x, pan_direction_y, zoom_direction;
  bool initialized, tracking, detected;
  bool pan_pending, zoom_pending;
  unsigned int misses;
} face_follow_state_t;

void face_follow_init(face_follow_state_t *state);
void face_follow_observe(face_follow_state_t *state, const face_follow_detection_t *faces, size_t count, double now);
void face_follow_tick(face_follow_state_t *state, double now, bool enabled);

/** The caller supplies a 0x9000-byte result buffer and packed BGR input. */
void face_follow_detect(unsigned char *results, unsigned char *bgr, int width, int height,
                        face_follow_detection_t *faces, size_t capacity, size_t *count);

#ifdef __cplusplus
}
#endif
