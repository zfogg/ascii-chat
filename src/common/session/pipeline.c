/**
 * @file session/pipeline.c
 * @brief Three-thread render pipeline implementation
 * @ingroup session
 */

#include "pipeline.h"
#include "capture.h"
#include "display.h"
#include "render.h"
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/ui/keyboard_help.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/atomic.h>
#include <string.h>

/* ============================================================================
 * External Declarations
 * ============================================================================ */

// Flag set by display thread when first frame is rendered
extern bool g_snapshot_first_frame_rendered;

/* ============================================================================
 * Frame Type & Queue
 * ============================================================================ */

typedef struct {
    uint8_t *pixels;       // SAFE_MALLOC'd copy
    int w, h;
    uint64_t captured_ns;  // wall-clock timestamp
} pipeline_frame_t;

// Thread-safe bounded queue (ring buffer)
typedef struct frame_queue_s {
    void    **slots;
    int       capacity;
    int       head, tail, count;
    mutex_t   mu;
    cond_t    not_empty;
    cond_t    not_full;
    const char *name;
} frame_queue_t;

static frame_queue_t *frame_queue_create(int capacity, const char *name) {
    frame_queue_t *q = SAFE_CALLOC(1, sizeof(*q), frame_queue_t *);
    q->slots = SAFE_CALLOC(capacity, sizeof(void *), void *);
    q->capacity = capacity;
    q->head = q->tail = q->count = 0;
    q->name = name;
    mutex_init(&q->mu, name);
    cond_init(&q->not_empty, "queue_not_empty");
    cond_init(&q->not_full, "queue_not_full");
    return q;
}

static void frame_queue_destroy(frame_queue_t *q) {
    if (!q) return;
    mutex_destroy(&q->mu);
    cond_destroy(&q->not_empty);
    cond_destroy(&q->not_full);
    SAFE_FREE(q->slots);
    SAFE_FREE(q);
}

// Push item, timeout_ns=0 means non-blocking (drops if full)
static bool frame_queue_push(frame_queue_t *q, void *item, uint64_t timeout_ns) {
    mutex_lock(&q->mu);
    while (q->count >= q->capacity) {
        if (timeout_ns == 0) {
            // Non-blocking: drop the item
            mutex_unlock(&q->mu);
            return false;
        }
        if (!cond_timedwait(&q->not_full, &q->mu, timeout_ns)) {
            mutex_unlock(&q->mu);
            return false;  // timeout
        }
    }
    q->slots[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    cond_signal(&q->not_empty);
    mutex_unlock(&q->mu);
    return true;
}

// Pop item, timeout_ns=0 means blocking forever
static void *frame_queue_pop(frame_queue_t *q, uint64_t timeout_ns) {
    mutex_lock(&q->mu);
    while (q->count == 0) {
        if (timeout_ns == 0) {
            // Block forever
            cond_wait(&q->not_empty, &q->mu);
        } else {
            if (!cond_timedwait(&q->not_empty, &q->mu, timeout_ns)) {
                mutex_unlock(&q->mu);
                return NULL;  // timeout
            }
        }
    }
    void *item = q->slots[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    cond_signal(&q->not_full);
    mutex_unlock(&q->mu);
    return item;
}

static int frame_queue_count(frame_queue_t *q) {
    if (!q) return 0;
    mutex_lock(&q->mu);
    int count = q->count;
    mutex_unlock(&q->mu);
    return count;
}

static void frame_queue_flush(frame_queue_t *q, void (*free_fn)(void *)) {
    if (!q) return;
    mutex_lock(&q->mu);
    while (q->count > 0) {
        void *item = q->slots[q->head];
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        if (item && free_fn) free_fn(item);
    }
    mutex_unlock(&q->mu);
}

/* ============================================================================
 * Frame Helpers
 * ============================================================================ */

static pipeline_frame_t *make_frame_copy(const image_t *src) {
    pipeline_frame_t *f = SAFE_CALLOC(1, sizeof(*f), pipeline_frame_t *);
    f->w = src->w;
    f->h = src->h;
    f->captured_ns = time_get_ns();
    size_t bytes = src->w * src->h * 3;
    f->pixels = SAFE_MALLOC(bytes, uint8_t *);
    memcpy(f->pixels, src->pixels, bytes);
    return f;
}

static void free_frame(pipeline_frame_t *f) {
    if (!f) return;
    SAFE_FREE(f->pixels);
    SAFE_FREE(f);
}

// Generic wrapper for frame_queue_flush that matches the expected void (*)(void *) signature
static void free_frame_generic(void *f) {
    free_frame((pipeline_frame_t *)f);
}

// Validate frame dimensions to detect corruption
static bool frame_is_valid(const pipeline_frame_t *f) {
    if (!f) {
        log_error("Frame pointer is NULL");
        return false;
    }
    // Reasonable bounds: width/height should be between 1 and 10000
    if (f->w <= 0 || f->w > 10000 || f->h <= 0 || f->h > 10000) {
        log_error("Invalid frame dimensions detected: w=%d, h=%d (likely corrupted)", f->w, f->h);
        return false;
    }
    if (!f->pixels) {
        log_error("Frame pixels pointer is NULL");
        return false;
    }
    return true;
}

/* ============================================================================
 * Pipeline Context
 * ============================================================================ */

struct session_pipeline_s {
    asciichat_thread_t capture_tid;
    asciichat_thread_t encode_tid;

    frame_queue_t *display_queue;
    frame_queue_t *encode_queue;

    session_capture_ctx_t *capture;
    session_display_ctx_t *display;

    atomic_t stop;
    atomic_t first_frame_ns;
    bool has_render_file;  // Track if render_file was set at creation time
};

/* ============================================================================
 * Capture Thread
 * ============================================================================ */

static void *pipeline_capture_thread(void *arg) {
    session_pipeline_t *pipeline = (session_pipeline_t *)arg;
    log_info("[PIPELINE_CAPTURE] Starting capture thread");

    bool snapshot_mode = GET_OPTION(snapshot_mode);
    if (snapshot_mode) {
        // Initialize duration estimate BEFORE encoding frames so PTS scaling works from frame 1
        extern uint64_t g_snapshot_actual_duration_ms;
        double snapshot_delay = GET_OPTION(snapshot_delay);
        g_snapshot_actual_duration_ms = (uint64_t)(snapshot_delay * 1000.0);
        log_info("[PIPELINE_CAPTURE] Snapshot mode: initialized g_snapshot_actual_duration_ms=%llu ms (snapshot_delay=%.2f)",
                 (unsigned long long)g_snapshot_actual_duration_ms, snapshot_delay);
    }

    while (!atomic_load_bool(&pipeline->stop)) {
        image_t *img = session_capture_read_frame(pipeline->capture);

        if (!img) {
            if (session_capture_at_end(pipeline->capture)) {
                log_info("[PIPELINE_CAPTURE] End of media reached");
                // Push EOF sentinels (zero-initialized)
                pipeline_frame_t *sentinel1 = SAFE_CALLOC(1, sizeof(*sentinel1), pipeline_frame_t *);
                frame_queue_push(pipeline->display_queue, sentinel1, 10 * NS_PER_MS_INT);
                if (pipeline->has_render_file) {
                    pipeline_frame_t *sentinel2 = SAFE_CALLOC(1, sizeof(*sentinel2), pipeline_frame_t *);
                    frame_queue_push(pipeline->encode_queue, sentinel2, 10 * NS_PER_MS_INT);
                }
                break;
            }
            platform_sleep_ns(1 * NS_PER_MS_INT);
            continue;
        }

        // Respect configured FPS
        session_capture_sleep_for_fps(pipeline->capture);

        // Copy frame
        pipeline_frame_t *frame = make_frame_copy(img);

        // Record first frame timestamp for snapshot timer
        uint64_t zero = 0;
        atomic_cas_u64(&pipeline->first_frame_ns, &zero, frame->captured_ns);

        // Fan out: display queue (fast, can drop), encode queue (slow, keep all)
        pipeline_frame_t *display_copy = SAFE_MALLOC(sizeof(*display_copy), pipeline_frame_t *);
        memcpy(display_copy, frame, sizeof(*display_copy));
        display_copy->pixels = SAFE_MALLOC(frame->w * frame->h * 3, uint8_t *);
        memcpy(display_copy->pixels, frame->pixels, frame->w * frame->h * 3);

        log_info("[PIPELINE_CAPTURE_PUSH_DISPLAY] Pushing %dx%d frame to display_queue", display_copy->w, display_copy->h);
        if (!frame_queue_push(pipeline->display_queue, display_copy, 0)) {
            // Non-blocking: drop if queue full
            log_warn("[PIPELINE_CAPTURE_DROP] Display queue full, dropping frame");
            free_frame(display_copy);
        }

        if (pipeline->has_render_file) {
            // Encode all captured frames, including frames before display rendering starts
            // The encoder will use them with correct timestamps regardless of display timing
            if (!frame_queue_push(pipeline->encode_queue, frame, 500 * NS_PER_MS_INT)) {
                // Non-blocking: drop if queue full after 500ms
                log_warn("[PIPELINE_CAPTURE] Encode queue blocked, dropping frame");
                free_frame(frame);
            } else {
                log_debug_every(60 * NS_PER_SEC_INT, "[PIPELINE_CAPTURE] Enqueued frame to encode_queue (%dx%d)", frame->w, frame->h);
            }
        } else {
            log_warn_every(1000 * NS_PER_MS_INT, "[PIPELINE_CAPTURE] has_render_file=false, NOT encoding frames");
            free_frame(frame);
        }

        // Check snapshot mode elapsed time AFTER queueing frame (ensure at least 1 frame is queued)
        if (snapshot_mode) {
            uint64_t now_ns = time_get_ns();

            // Set first capture timestamp on first frame (only once)
            if (g_snapshot_first_capture_ns == 0) {
                g_snapshot_first_capture_ns = now_ns;
                log_info("[SNAPSHOT_CAPTURE] First frame captured at %llu ns", (unsigned long long)now_ns);
            }

            // Calculate elapsed time from first capture
            double elapsed = (double)(now_ns - g_snapshot_first_capture_ns) / NS_PER_SEC_INT;
            double snapshot_delay = GET_OPTION(snapshot_delay);

            if (elapsed >= snapshot_delay) {
                log_info("[PIPELINE_CAPTURE] Snapshot video elapsed=%.3f reached delay=%.2f - stopping capture but waiting for encode queue to drain",
                         elapsed, snapshot_delay);

                // Update with actual measured duration so encoder can scale frames accurately
                extern uint64_t g_snapshot_actual_duration_ms;
                extern uint64_t g_snapshot_last_capture_elapsed_ns;
                uint64_t actual_ms = (uint64_t)(elapsed * 1000.0);
                g_snapshot_actual_duration_ms = actual_ms;
                uint64_t last_frame_elapsed_ns = now_ns - g_snapshot_first_capture_ns;
                g_snapshot_last_capture_elapsed_ns = last_frame_elapsed_ns;
                log_info("[PIPELINE_CAPTURE] g_snapshot_actual_duration_ms=%llu, last_capture_elapsed_ns=%llu",
                         (unsigned long long)actual_ms, (unsigned long long)last_frame_elapsed_ns);

                // Wait for encode queue to drain (all buffered frames processed)
                // This ensures slow encoders can catch up and encode all captured frames
                // Skip wait if snapshot_delay is 0 (immediate exit after 1 frame)
                double snapshot_delay = GET_OPTION(snapshot_delay);
                if (pipeline->has_render_file && snapshot_delay > 0) {
                    while (frame_queue_count(pipeline->encode_queue) > 0) {
                        log_debug("[PIPELINE_CAPTURE] Waiting for encode queue to drain (%d frames pending)",
                                 frame_queue_count(pipeline->encode_queue));
                        platform_sleep_ns(10 * NS_PER_MS_INT);
                    }
                    log_info("[PIPELINE_CAPTURE] Encode queue drained, sending EOF sentinel");
                }

                // Push EOF sentinels (zero-initialized)
                pipeline_frame_t *sentinel1 = SAFE_CALLOC(1, sizeof(*sentinel1), pipeline_frame_t *);
                frame_queue_push(pipeline->display_queue, sentinel1, 10 * NS_PER_MS_INT);
                if (pipeline->has_render_file) {
                    pipeline_frame_t *sentinel2 = SAFE_CALLOC(1, sizeof(*sentinel2), pipeline_frame_t *);
                    frame_queue_push(pipeline->encode_queue, sentinel2, 10 * NS_PER_MS_INT);
                }
                break;
            }
        }
    }

    log_info("[PIPELINE_CAPTURE] Capture thread exiting");
    return NULL;
}

/* ============================================================================
 * Encode Thread
 * ============================================================================ */

static void *pipeline_encode_thread(void *arg) {
    session_pipeline_t *pipeline = (session_pipeline_t *)arg;
    log_info("[PIPELINE_ENCODE] Starting encode thread");

    uint64_t frames_processed = 0;

    while (!atomic_load_bool(&pipeline->stop)) {
        pipeline_frame_t *frame = (pipeline_frame_t *)frame_queue_pop(pipeline->encode_queue, 100 * NS_PER_MS_INT);

        if (!frame) {
            log_debug_every(1 * NS_PER_SEC_INT, "[PIPELINE_ENCODE] Waiting for frames (processed=%llu)", (unsigned long long)frames_processed);
            continue;  // timeout
        }

        if (!frame->pixels) {
            // EOF sentinel
            log_info("[PIPELINE_ENCODE] Received EOF sentinel, exiting (processed=%llu frames)", (unsigned long long)frames_processed);
            free_frame(frame);
            break;
        }

        // Validate frame before processing (catch memory corruption)
        if (!frame_is_valid(frame)) {
            log_error("Skipping frame with corrupted dimensions in encode thread");
            free_frame(frame);
            continue;
        }

        if (frames_processed == 0 || frames_processed % 30 == 0) {
            log_info("[PIPELINE_ENCODE] Processing frame %llu: %dx%d", (unsigned long long)frames_processed, frame->w, frame->h);
        }

        // Encode frame (convert_to_ascii is called internally by encode_frame)
        // Avoid double-conversion that was causing state desynchronization between display/encode threads
        image_t raw_image = { .w = frame->w, .h = frame->h, .pixels = (rgb_pixel_t *)frame->pixels };
        session_display_encode_frame(pipeline->display, &raw_image, frame->captured_ns);
        frames_processed++;

        free_frame(frame);
    }

    log_info("[PIPELINE_ENCODE] Encode thread exiting (total frames encoded=%llu)", (unsigned long long)frames_processed);
    return NULL;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

asciichat_error_t session_pipeline_create(
    session_capture_ctx_t *capture,
    session_display_ctx_t *display,
    session_pipeline_t **out)
{
    if (!capture || !display || !out) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "session_pipeline_create: NULL argument");
    }

    session_pipeline_t *p = SAFE_CALLOC(1, sizeof(*p), session_pipeline_t *);
    p->capture = capture;
    p->display = display;
    p->display_queue = frame_queue_create(4, "display");
    p->encode_queue = frame_queue_create(256, "encode");  // Large buffer for slow encoding

    // Check if display has render_file configured (will be checked in encode thread)
    // We store a flag to know whether to enqueue frames for encoding
    double render_fps = session_display_get_render_fps(display);
    bool has_file = session_display_has_render_file(display);

    // Enable encode thread if render_file is available
    // Note: has_file can be true even if render_file creation failed (ctx->render_file exists)
    // OR if render_fps is explicitly set for playback speed control
    p->has_render_file = render_fps > 0 || has_file;
    log_info("[PIPELINE_CREATE] render_fps=%.1f, has_file=%d, has_render_file=%d",
             render_fps, has_file ? 1 : 0, p->has_render_file ? 1 : 0);

    atomic_store_bool(&p->stop, false);
    atomic_store_u64(&p->first_frame_ns, 0);

    // Start capture thread
    if (asciichat_thread_create(&p->capture_tid, "pipeline_capture", pipeline_capture_thread, p) != 0) {
        frame_queue_destroy(p->display_queue);
        frame_queue_destroy(p->encode_queue);
        SAFE_FREE(p);
        return SET_ERRNO(ERROR_INIT, "session_pipeline_create: failed to start capture thread");
    }

    // Start encode thread only if render_file is active
    if (p->has_render_file) {
        if (asciichat_thread_create(&p->encode_tid, "pipeline_encode", pipeline_encode_thread, p) != 0) {
            atomic_store_bool(&p->stop, true);
            asciichat_thread_join(&p->capture_tid, NULL);
            frame_queue_destroy(p->display_queue);
            frame_queue_destroy(p->encode_queue);
            SAFE_FREE(p);
            return SET_ERRNO(ERROR_INIT, "session_pipeline_create: failed to start encode thread");
        }
    }

    *out = p;
    return ASCIICHAT_OK;
}

asciichat_error_t session_pipeline_run_main(
    session_pipeline_t *pipeline,
    session_should_exit_fn should_exit,
    session_keyboard_handler_fn keyboard_handler,
    void *user_data)
{
    if (!pipeline || !should_exit) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "session_pipeline_run_main: NULL argument");
    }

    log_info("[PIPELINE_MAIN] Starting main thread loop");

    bool snapshot_mode = GET_OPTION(snapshot_mode);
    bool snapshot_done = false;

    while (!should_exit(user_data) && !atomic_load_bool(&pipeline->stop) && !snapshot_done) {
        bool exit_check = should_exit(user_data);
        log_debug_every(NS_PER_SEC_INT, "[PIPELINE_DEBUG] should_exit=%d, stop=%d, snapshot_done=%d", exit_check, atomic_load_bool(&pipeline->stop), snapshot_done);

        pipeline_frame_t *frame = (pipeline_frame_t *)frame_queue_pop(pipeline->display_queue, 1 * NS_PER_MS_INT);

        if (!frame) continue;  // timeout, check should_exit again

        // Validate frame structure exists before accessing fields
        if (!frame_is_valid(frame)) {
            log_error("Received invalid frame: corrupted or NULL");
            free_frame(frame);
            frame = NULL;
            continue;
        }

        // Check for EOF sentinel (NULL pixels)
        if (!frame->pixels) {
            // EOF sentinel
            log_info("[PIPELINE_MAIN_EOF] Received EOF sentinel, stopping");
            free_frame(frame);
            frame = NULL;
            break;
        }

        // Log frame details after validation
        log_debug("[PIPELINE_MAIN_POP] frame=%p, pixels=%p, w=%d, h=%d",
                  (void *)frame, (void *)frame->pixels, frame->w, frame->h);

        // Check if help screen is active - if so, don't render ASCII frames
        bool help_is_active = pipeline->display && keyboard_help_is_active(pipeline->display);

        if (!help_is_active) {
            log_info("[PIPELINE_MAIN_RENDER] Converting frame to ASCII: %dx%d", frame->w, frame->h);
            // Convert to ASCII
            image_t tmp = { .w = frame->w, .h = frame->h, .pixels = (rgb_pixel_t *)frame->pixels };
            char *ascii = session_display_convert_to_ascii(pipeline->display, &tmp);
            free_frame(frame);
            frame = NULL;

            if (ascii) {
                // Write ASCII to terminal
                session_display_write_ascii(pipeline->display, ascii);
                SAFE_FREE(ascii);
            }
        } else {
            free_frame(frame);
            frame = NULL;
        }

        // Keyboard polling
        if (keyboard_handler) {
            keyboard_key_t key = keyboard_read_nonblocking();
            if (key != KEY_NONE) {
                log_debug("PIPELINE_KEYBOARD: Received key=%d", key);
                keyboard_handler(NULL, (int)key, user_data);  // NULL capture ctx
            }
        }

        // Snapshot mode: check if elapsed time has reached snapshot_delay duration
        // This check runs every iteration after first frame is rendered (when display.c sets g_snapshot_first_frame_rendered)
        if (snapshot_mode && !snapshot_done) {
            if (g_snapshot_first_frame_rendered && g_snapshot_first_frame_rendered_ns > 0) {
                double snapshot_delay = GET_OPTION(snapshot_delay);
                uint64_t now_ns = time_get_ns();
                uint64_t elapsed_ns = now_ns - g_snapshot_first_frame_rendered_ns;
                double elapsed_sec = (double)elapsed_ns / (double)NS_PER_SEC_INT;

                if ((unsigned long)(elapsed_sec * 10) % 10 == 0 || elapsed_sec < 0.2) {
                    log_info("[SNAPSHOT_PIPELINE] CHECK: elapsed=%.3f target=%.2f", elapsed_sec, snapshot_delay);
                }

                // snapshot_delay=0 means exit after first frame
                // snapshot_delay>0 means wait that many seconds before exiting
                bool should_snapshot_exit = (snapshot_delay == 0.0) || (elapsed_sec >= snapshot_delay);

                if (should_snapshot_exit) {
                    log_info("[SNAPSHOT_PIPELINE] EXITING - elapsed=%.3f target=%.2f", elapsed_sec, snapshot_delay);
                    snapshot_done = true;
                }
            }
        }
    }

    log_info("[PIPELINE_MAIN] Main loop exiting, signaling threads to stop");
    atomic_store_bool(&pipeline->stop, true);

    return ASCIICHAT_OK;
}

asciichat_error_t session_pipeline_destroy(session_pipeline_t *pipeline) {
    if (!pipeline) return ASCIICHAT_OK;

    log_info("[PIPELINE] Destroying pipeline, waiting for threads...");

    // Signal threads to stop (may already be stopped)
    atomic_store_bool(&pipeline->stop, true);

    // Wake up any threads blocked on condition variable waits
    // so they can check the stop flag and exit immediately
    // instead of waiting for timeouts (e.g., 100ms frame_queue_pop timeout)
    if (pipeline->display_queue) {
        mutex_lock(&pipeline->display_queue->mu);
        cond_broadcast(&pipeline->display_queue->not_empty);
        cond_broadcast(&pipeline->display_queue->not_full);
        mutex_unlock(&pipeline->display_queue->mu);
    }
    if (pipeline->encode_queue) {
        mutex_lock(&pipeline->encode_queue->mu);
        cond_broadcast(&pipeline->encode_queue->not_empty);
        cond_broadcast(&pipeline->encode_queue->not_full);
        mutex_unlock(&pipeline->encode_queue->mu);
    }

    // Drain and wait for capture thread with 1 second timeout
    if (asciichat_thread_is_initialized(&pipeline->capture_tid)) {
        int join_result = asciichat_thread_join_timeout(&pipeline->capture_tid, NULL, 1000 * NS_PER_MS_INT);
        if (join_result != 0) {
            log_warn("[PIPELINE] Capture thread join timed out or failed (result=%d)", join_result);
        }
    }

    // Drain and wait for encode thread with 1 second timeout
    if (asciichat_thread_is_initialized(&pipeline->encode_tid)) {
        int join_result = asciichat_thread_join_timeout(&pipeline->encode_tid, NULL, 1000 * NS_PER_MS_INT);
        if (join_result != 0) {
            log_warn("[PIPELINE] Encode thread join timed out or failed (result=%d)", join_result);
        }
    }

    // Flush queues to free any remaining frames
    frame_queue_flush(pipeline->display_queue, free_frame_generic);
    frame_queue_flush(pipeline->encode_queue, free_frame_generic);

    // Don't free queue structures - debug_sync monitoring thread may still be accessing
    // the condition variables. Queues will be cleaned up with the pipeline structure.
    SAFE_FREE(pipeline);

    log_info("[PIPELINE] Pipeline destroyed");
    return ASCIICHAT_OK;
}
