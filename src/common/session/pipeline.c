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
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/atomic.h>
#include <string.h>

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

    while (!atomic_load_bool(&pipeline->stop)) {
        image_t *img = session_capture_read_frame(pipeline->capture);

        if (!img) {
            if (session_capture_at_end(pipeline->capture)) {
                log_info("[PIPELINE_CAPTURE] End of media reached");
                // Push EOF sentinels
                pipeline_frame_t sentinel = {0};
                frame_queue_push(pipeline->display_queue, SAFE_MALLOC(sizeof(sentinel), pipeline_frame_t *), 10 * NS_PER_MS_INT);
                if (pipeline->has_render_file) {
                    frame_queue_push(pipeline->encode_queue, SAFE_MALLOC(sizeof(sentinel), pipeline_frame_t *), 10 * NS_PER_MS_INT);
                }
                break;
            }
            platform_sleep_ns(1 * NS_PER_MS_INT);
            continue;
        }

        // Check snapshot mode elapsed time
        if (snapshot_mode) {
            uint64_t now_ns = time_get_ns();
            uint64_t zero = 0;
            atomic_cas_u64(&pipeline->first_frame_ns, &zero, now_ns);

            uint64_t first_ns = atomic_load_u64(&pipeline->first_frame_ns);
            double elapsed = (double)(now_ns - first_ns) / NS_PER_SEC_INT;
            double snapshot_delay = GET_OPTION(snapshot_delay);

            if (elapsed >= snapshot_delay) {
                log_info("[PIPELINE_CAPTURE] Snapshot elapsed=%.3f reached delay=%.2f", elapsed, snapshot_delay);
                // Push EOF sentinels
                pipeline_frame_t sentinel = {0};
                frame_queue_push(pipeline->display_queue, SAFE_MALLOC(sizeof(sentinel), pipeline_frame_t *), 10 * NS_PER_MS_INT);
                if (pipeline->has_render_file) {
                    frame_queue_push(pipeline->encode_queue, SAFE_MALLOC(sizeof(sentinel), pipeline_frame_t *), 10 * NS_PER_MS_INT);
                }
                break;
            }
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

        if (!frame_queue_push(pipeline->display_queue, display_copy, 0)) {
            // Non-blocking: drop if queue full
            free_frame(display_copy);
        }

        if (pipeline->has_render_file) {
            if (!frame_queue_push(pipeline->encode_queue, frame, 500 * NS_PER_MS_INT)) {
                // Blocking: keep trying to enqueue for file output
                free_frame(frame);
            }
        } else {
            free_frame(frame);
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

    while (!atomic_load_bool(&pipeline->stop)) {
        pipeline_frame_t *frame = (pipeline_frame_t *)frame_queue_pop(pipeline->encode_queue, 100 * NS_PER_MS_INT);

        if (!frame) continue;  // timeout

        if (!frame->pixels) {
            // EOF sentinel
            free_frame(frame);
            break;
        }

        // Encode frame: wrap in image_t and call encoder
        image_t tmp = { .w = frame->w, .h = frame->h, .pixels = (rgb_pixel_t *)frame->pixels };
        session_display_encode_frame(pipeline->display, &tmp);
        free_frame(frame);
    }

    log_info("[PIPELINE_ENCODE] Encode thread exiting");
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
    p->encode_queue = frame_queue_create(32, "encode");

    // Check if display has render_file configured (will be checked in encode thread)
    // We store a flag to know whether to enqueue frames for encoding
    p->has_render_file = session_display_get_render_fps(display) > 0 ||
                         session_display_has_render_file(display);

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
        pipeline_frame_t *frame = (pipeline_frame_t *)frame_queue_pop(pipeline->display_queue, 100 * NS_PER_MS_INT);

        if (!frame) continue;  // timeout, check should_exit again

        if (!frame->pixels) {
            // EOF sentinel
            free_frame(frame);
            break;
        }

        // Convert to ASCII
        image_t tmp = { .w = frame->w, .h = frame->h, .pixels = (rgb_pixel_t *)frame->pixels };
        char *ascii = session_display_convert_to_ascii(pipeline->display, &tmp);
        free_frame(frame);

        if (ascii) {
            // Write ASCII to terminal
            session_display_write_ascii(pipeline->display, ascii);
            SAFE_FREE(ascii);
        }

        // Keyboard polling
        if (keyboard_handler) {
            keyboard_key_t key = keyboard_read_nonblocking();
            if (key > 0) {
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

    // Drain and wait for capture thread
    if (asciichat_thread_is_initialized(&pipeline->capture_tid)) {
        asciichat_thread_join(&pipeline->capture_tid, NULL);
    }

    // Drain and wait for encode thread
    if (asciichat_thread_is_initialized(&pipeline->encode_tid)) {
        asciichat_thread_join(&pipeline->encode_tid, NULL);
    }

    // Flush queues
    frame_queue_flush(pipeline->display_queue, (void (*)(void *))free_frame);
    frame_queue_flush(pipeline->encode_queue, (void (*)(void *))free_frame);

    frame_queue_destroy(pipeline->display_queue);
    frame_queue_destroy(pipeline->encode_queue);
    SAFE_FREE(pipeline);

    log_info("[PIPELINE] Pipeline destroyed");
    return ASCIICHAT_OK;
}
