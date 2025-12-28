/**
 * @file fps_tracker.c
 * @brief FPS tracker utility implementation
 *
 * Provides simplified FPS tracking wrapper to eliminate duplicate
 * static initialization patterns across multiple threads.
 *
 * @ingroup util
 */

#include "lib/util/fps_tracker.h"
#include "lib/common.h"
#include <time.h>

/**
 * @internal
 * @brief Internal state for FPS tracker
 */
struct fps_tracker_state {
    fps_t fps;           /**< Underlying FPS tracker */
    char label[32];      /**< Label for logging */
    bool initialized;    /**< Whether fps_init was called */
};

fps_tracker_t *fps_tracker_create(int expected_fps, const char *label) {
    fps_tracker_t *tracker = SAFE_MALLOC(sizeof(fps_tracker_t), fps_tracker_t *);
    if (!tracker) {
        return NULL;
    }

    // Initialize fields
    tracker->fps = (fps_t){0};
    tracker->initialized = false;

    // Store label safely
    if (label) {
        SAFE_STRNCPY(tracker->label, label, sizeof(tracker->label));
    } else {
        tracker->label[0] = '\0';
    }

    // Initialize fps tracker
    fps_init(&tracker->fps, expected_fps, tracker->label);
    tracker->initialized = true;

    return tracker;
}

void fps_tracker_record_frame(fps_tracker_t *tracker, const char *message) {
    if (!tracker || !tracker->initialized) {
        return;
    }

    struct timespec current_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &current_time);
    fps_frame(&tracker->fps, &current_time, message);
}

void fps_tracker_record_frame_at(fps_tracker_t *tracker, const struct timespec *ts, const char *message) {
    if (!tracker || !tracker->initialized || !ts) {
        return;
    }

    fps_frame(&tracker->fps, ts, message);
}

fps_t *fps_tracker_get_fps(fps_tracker_t *tracker) {
    if (!tracker) {
        return NULL;
    }
    return &tracker->fps;
}

void fps_tracker_free(fps_tracker_t *tracker) {
    if (!tracker) {
        return;
    }
    SAFE_FREE(tracker);
}
