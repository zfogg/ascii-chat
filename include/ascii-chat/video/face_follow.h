#pragma once

/* Existing buffer budgets in bytes, including NUL for text storage. */
#define FACE_FOLLOW_DETECT_WIDTH 320
#define FACE_FOLLOW_DETECT_HEIGHT 240
#define FACE_FOLLOW_RESULT_BUFFER_SIZE 0x9000
#define FACE_FOLLOW_MAX_DETECTIONS 32

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Normalized coordinates in the original, unmirrored source. */
/**
 * @struct face_follow_detection_t
 * @brief When code needs to work with face follow detection, it keeps `box`, `confidence` here instead of scattering it across globals. Initialization establishes the handles and sizes; teardown must release any allocation, lock, thread, or callback context recorded in those fields.
 */
/**
 * @struct face_follow_rect_t
 * @brief For face follow rect, this is the live record that ties `x`, `y`, `w`, `h` together. The owning code creates it, updates it while work is active, and releases its handles and pointed-to storage before freeing the record.
 */
typedef struct {
  float x, y, w, h;
} face_follow_rect_t;

typedef struct {
  face_follow_rect_t box;
  float confidence;
} face_follow_detection_t;

/** One instance per capture source; times are monotonic seconds. */
/**
 * @struct face_follow_state_t
 * @brief face follow state keeps the values that move through the subsystem: `crop`, `target`, `face`, `last_tick`. Some members are pointers into caller- or subsystem-owned memory, so lifetime follows the API that populated them rather than the struct copy itself.
 */
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
