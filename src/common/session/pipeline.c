#include <ascii-chat/otel.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/buffer.h>
/**
 * @file session/pipeline.c
 * @brief Three-thread render pipeline implementation
 * @ingroup session
 */

#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/controller.h>
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
#include <ascii-chat/debug/named.h>
#include <string.h>

/* ============================================================================
 * External Declarations
 * ============================================================================ */

// Flag set by display thread when first frame is rendered
extern bool g_snapshot_first_frame_rendered;

/* ============================================================================
 * Frame Type & Queue
 * ============================================================================ */

typedef struct frame_slot_s frame_slot_t;

typedef struct {
  frame_slot_t *slot;    // Exclusive until free_frame publishes the slot back to capture.
  const uint8_t *pixels; // Immutable owning reference shared by display and encoding
  int w, h;
  uint64_t source_epoch, frame_serial;
#ifdef ASCIICHAT_WITH_OTEL
  gotel_context_t telemetry_context;
#endif
  bool fresh;
  uint64_t captured_ns; // wall-clock timestamp
  uint64_t acquisition_started_ns;
} pipeline_frame_t;

struct frame_slot_s {
  pipeline_frame_t frame;
  _Atomic bool busy;
  size_t capacity;
};

#define PIPELINE_FRAME_SLOTS 3

// Thread-safe bounded queue (ring buffer)
typedef struct frame_queue_s {
  void **slots;
  int capacity;
  int head, tail, count;
  mutex_t mu;
  cond_t not_empty;
  cond_t not_full;
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
  if (!q)
    return;
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
    if (cond_timedwait(&q->not_full, &q->mu, timeout_ns) != 0) {
      mutex_unlock(&q->mu);
      return false; // timeout
    }
  }
  q->slots[q->tail] = item;
  q->tail = (q->tail + 1) % q->capacity;
  q->count++;
  ascii_otel_metric(ASCII_OTEL_METRIC_ENCODE_DEPTH, q->count);
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
      if (cond_timedwait(&q->not_empty, &q->mu, timeout_ns) != 0) {
        mutex_unlock(&q->mu);
        return NULL; // timeout
      }
    }
  }
  void *item = q->slots[q->head];
  q->head = (q->head + 1) % q->capacity;
  q->count--;
  ascii_otel_metric(!strcmp(q->name, "display") ? ASCII_OTEL_METRIC_DISPLAY_DEPTH : ASCII_OTEL_METRIC_ENCODE_DEPTH,
                    q->count);
  pipeline_frame_t *telemetry_frame = item;
  double age = (double)(time_get_ns() - telemetry_frame->captured_ns) / 1e9;
  ascii_otel_metric(!strcmp(q->name, "display") ? ASCII_OTEL_METRIC_DISPLAY_WAIT : ASCII_OTEL_METRIC_ENCODE_WAIT, age);
  ascii_otel_metric(ASCII_OTEL_METRIC_QUEUE_OLDEST, age);
  cond_signal(&q->not_full);
  mutex_unlock(&q->mu);
  return item;
}

static void *frame_queue_replace_latest(frame_queue_t *q, void *item) {
  mutex_lock(&q->mu);
  void *retired = q->count ? q->slots[q->head] : NULL;
  q->head = 0;
  q->tail = 0;
  q->count = 1;
  q->slots[0] = item;
  ascii_otel_metric(ASCII_OTEL_METRIC_DISPLAY_DEPTH, 1);
  if (retired)
    ascii_otel_metric(ASCII_OTEL_METRIC_DROP_DISPLAY, 1);
  cond_signal(&q->not_empty);
  mutex_unlock(&q->mu);
  return retired;
}

static int frame_queue_count(frame_queue_t *q) {
  if (!q)
    return 0;
  mutex_lock(&q->mu);
  int count = q->count;
  mutex_unlock(&q->mu);
  return count;
}

static void frame_queue_flush(frame_queue_t *q, void (*free_fn)(void *)) {
  if (!q)
    return;
  mutex_lock(&q->mu);
  while (q->count > 0) {
    void *item = q->slots[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    ascii_otel_metric(!strcmp(q->name, "display") ? ASCII_OTEL_METRIC_DISPLAY_DEPTH : ASCII_OTEL_METRIC_ENCODE_DEPTH,
                      q->count);
    pipeline_frame_t *telemetry_frame = item;
    double age = (double)(time_get_ns() - telemetry_frame->captured_ns) / 1e9;
    ascii_otel_metric(!strcmp(q->name, "display") ? ASCII_OTEL_METRIC_DISPLAY_WAIT : ASCII_OTEL_METRIC_ENCODE_WAIT,
                      age);
    ascii_otel_metric(ASCII_OTEL_METRIC_QUEUE_OLDEST, age);
    if (item && free_fn)
      free_fn(item);
  }
  mutex_unlock(&q->mu);
}

/* ============================================================================
 * Frame Helpers
 * ============================================================================ */

static pipeline_frame_t *make_frame_copy(const image_t *src, frame_slot_t *slots) {
  if (src->w <= 0 || src->h <= 0 || (size_t)src->w > SIZE_MAX / sizeof(rgb_pixel_t) / (size_t)src->h) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid pipeline frame dimensions");
    return NULL;
  }
  size_t bytes = (size_t)src->w * (size_t)src->h * sizeof(rgb_pixel_t);
  pipeline_frame_t *f = NULL;
  if (slots) {
    for (size_t i = 0; i < PIPELINE_FRAME_SLOTS; ++i) {
      bool expected = false;
      if (atomic_compare_exchange_strong_explicit(&slots[i].busy, &expected, true, memory_order_acquire,
                                                  memory_order_relaxed)) {
        f = &slots[i].frame;
        f->slot = &slots[i];
        if (slots[i].capacity < bytes) {
          buffer_release(f->pixels);
          f->pixels = NULL;
          slots[i].capacity = 0;
        }
        break;
      }
    }
  }
  if (!f)
    f = SAFE_CALLOC(1, sizeof(*f), pipeline_frame_t *);
  if (!f->pixels) {
    f->pixels = buffer_alloc(NULL, bytes);
    if (!f->pixels) {
      if (f->slot)
        atomic_store_explicit(&f->slot->busy, false, memory_order_release);
      else
        SAFE_FREE(f);
      return NULL;
    }
    if (f->slot)
      f->slot->capacity = bytes;
  }
  f->w = src->w;
  f->h = src->h;
  f->captured_ns = time_get_ns();
  memcpy((void *)f->pixels, src->pixels, bytes);
  return f;
}

static void free_frame(pipeline_frame_t *f) {
  if (!f)
    return;
  if (f->slot) {
    atomic_store_explicit(&f->slot->busy, false, memory_order_release);
    return;
  }
  buffer_release(f->pixels);
  SAFE_FREE(f);
}

static void frame_slots_destroy(frame_slot_t *slots) {
  for (size_t i = 0; i < PIPELINE_FRAME_SLOTS; ++i) {
    buffer_release(slots[i].frame.pixels);
    slots[i].frame.pixels = NULL;
    slots[i].capacity = 0;
  }
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
  asciichat_thread_t input_tid;
  session_keyboard_handler_fn keyboard_handler;
  session_should_exit_fn should_exit;
  void *input_data;

  frame_slot_t frame_slots[PIPELINE_FRAME_SLOTS];
  frame_queue_t *display_queue;
  frame_queue_t *encode_queue;

  session_capture_ctx_t *capture;
  session_display_ctx_t *display;

  atomic_t stop;
  atomic_t capture_finished;
  atomic_t first_frame_ns;
  asciichat_error_context_t capture_failure; // Read after capture joins.
  asciichat_error_t encode_error;            // Encode-owned; read only after joining encode.
  uint64_t frames_accepted;                  // Capture-owned; read only after joining capture.
  bool has_render_file;                      // Track if render_file was set at creation time
};

/* ============================================================================
 * Capture Thread
 * ============================================================================ */

static void *pipeline_capture_thread(void *arg) {
  session_pipeline_t *pipeline = (session_pipeline_t *)arg;
  log_debug("[PIPELINE_CAPTURE] Starting capture thread");

  bool snapshot_mode = GET_OPTION(snapshot_mode);
  uint64_t snapshot_frames_captured = 0;
  uint64_t snapshot_target_frames = 1;
  media_source_t *capture_source = session_capture_get_media_source(pipeline->capture);
  media_source_type_t capture_source_type = media_source_get_type(capture_source);
  bool snapshot_uses_frame_target =
      (capture_source_type == MEDIA_SOURCE_FILE || capture_source_type == MEDIA_SOURCE_STDIN) &&
      !media_source_uses_webcam(capture_source);
  if (snapshot_mode) {
    // Initialize duration estimate BEFORE encoding frames so PTS scaling works from frame 1
    extern uint64_t g_snapshot_actual_duration_ms;
    double snapshot_delay = GET_OPTION(snapshot_delay);
    g_snapshot_actual_duration_ms = (uint64_t)(snapshot_delay * 1000.0);
    if (snapshot_delay > 0.0 && snapshot_uses_frame_target) {
      double target_frames = snapshot_delay * session_capture_get_target_fps(pipeline->capture);
      snapshot_target_frames = (uint64_t)(target_frames + 0.999999);
    }
    log_debug(
        "[PIPELINE_CAPTURE] Snapshot mode: initialized g_snapshot_actual_duration_ms=%llu ms (snapshot_delay=%.2f)",
        (unsigned long long)g_snapshot_actual_duration_ms, snapshot_delay);
  }

  uint64_t previous_serial = 0;
  while (!atomic_load_bool(&pipeline->stop)) {
    ASCII_OTEL_SCOPE(PIPELINE_CAPTURE);
    asciichat_errno_scope_t frame_scope = errno_checkpoint();
    uint64_t acquisition_started_ns = time_get_ns();
    image_t *img = session_capture_read_frame(pipeline->capture);

    if (!img) {
      if (errno_peek_since(frame_scope, &pipeline->capture_failure))
        break;
      if (session_capture_at_end(pipeline->capture)) {
        log_debug("[PIPELINE_CAPTURE] End of media reached");
        break;
      }
      platform_sleep_ns(1 * NS_PER_MS_INT);
      continue;
    }

    // Copy frame
    pipeline_frame_t *frame = make_frame_copy(img, pipeline->has_render_file ? NULL : pipeline->frame_slots);
    if (!frame) {
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_FRAMES_DROPPED, 1);
      continue;
    }
    frame->acquisition_started_ns = acquisition_started_ns;
#ifdef ASCIICHAT_WITH_OTEL
    frame->telemetry_context = ascii_otel_context();
#endif

    frame->source_epoch = session_capture_source_epoch(pipeline->capture);
    frame->frame_serial = session_capture_frame_serial(pipeline->capture);
    frame->fresh = frame->frame_serial != previous_serial;
    previous_serial = frame->frame_serial;

    // Record first frame timestamp for snapshot timer
    uint64_t zero = 0;
    atomic_cas_u64(&pipeline->first_frame_ns, &zero, frame->captured_ns);

    ascii_otel_metric(ASCII_OTEL_METRIC_FRAME_CAPTURED, 1);
    // Fan out: display queue (fast, can drop), encode queue (slow, keep all)
    pipeline_frame_t *display_copy = frame;
    if (pipeline->has_render_file) {
      display_copy = SAFE_MALLOC(sizeof(*display_copy), pipeline_frame_t *);
      memcpy(display_copy, frame, sizeof(*display_copy));
      display_copy->pixels = buffer_share(frame->pixels);
    }
    pipeline_frame_t *retired = frame_queue_replace_latest(pipeline->display_queue, display_copy);
    if (retired) {
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_FRAMES_DROPPED, 1);
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_QUEUE_DROPS, 1);
      free_frame(retired);
    }
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_FRAMES_ENQUEUED, 1);

    if (pipeline->has_render_file) {
      // Encode all captured frames, including frames before display rendering starts
      // The encoder will use them with correct timestamps regardless of display timing
      bool queued = frame_queue_push(pipeline->encode_queue, frame, 500 * NS_PER_MS_INT);
      while (!queued && snapshot_uses_frame_target && !atomic_load_bool(&pipeline->stop))
        queued = frame_queue_push(pipeline->encode_queue, frame, 100 * NS_PER_MS_INT);
      if (!queued) {
        ascii_otel_metric(ASCII_OTEL_METRIC_DROP_ENCODE, 1);
        stats_counter_add(stats_runtime_scope(), STATS_COUNTER_FRAMES_DROPPED, 1);
        stats_counter_add(stats_runtime_scope(), STATS_COUNTER_QUEUE_DROPS, 1);
        // Non-blocking: drop if queue full after 500ms
        log_warn_every(US_PER_SEC_INT, "[PIPELINE_CAPTURE] Encode queue blocked, dropping frame");
        free_frame(frame);
      } else {
        stats_counter_add(stats_runtime_scope(), STATS_COUNTER_FRAMES_ENQUEUED, 1);
        pipeline->frames_accepted++;
        log_debug_every(60 * US_PER_SEC_INT, "[PIPELINE_CAPTURE] Enqueued frame to encode_queue");
      }
    }

    // Publish the acquired image before pacing the next acquisition. Sleeping
    // with an already captured frame adds a full interval to live latency.
    session_capture_sleep_for_fps(pipeline->capture);

    // Check snapshot mode elapsed time AFTER queueing frame (ensure at least 1 frame is queued)
    if (snapshot_mode) {
      snapshot_frames_captured++;
      uint64_t now_ns = time_get_ns();

      // Set first capture timestamp on first frame (only once)
      if (g_snapshot_first_capture_ns == 0) {
        g_snapshot_first_capture_ns = now_ns;
        log_debug("[SNAPSHOT_CAPTURE] First frame captured at %llu ns", (unsigned long long)now_ns);
      }

      // Calculate elapsed time from first capture
      double elapsed = (double)(now_ns - g_snapshot_first_capture_ns) / NS_PER_SEC_INT;
      double snapshot_delay = GET_OPTION(snapshot_delay);
      bool snapshot_complete =
          snapshot_uses_frame_target ? snapshot_frames_captured >= snapshot_target_frames : elapsed >= snapshot_delay;

      if (snapshot_complete) {
        log_debug(
            "[PIPELINE_CAPTURE] Snapshot video elapsed=%.3f reached delay=%.2f - stopping capture but waiting for "
            "encode queue to drain",
            elapsed, snapshot_delay);

        // Update with actual measured duration so encoder can scale frames accurately
        extern uint64_t g_snapshot_actual_duration_ms;
        extern uint64_t g_snapshot_last_capture_elapsed_ns;
        uint64_t actual_ms =
            snapshot_uses_frame_target ? (uint64_t)(snapshot_delay * 1000.0) : (uint64_t)(elapsed * 1000.0);
        g_snapshot_actual_duration_ms = actual_ms;
        uint64_t last_frame_elapsed_ns = now_ns - g_snapshot_first_capture_ns;
        g_snapshot_last_capture_elapsed_ns = last_frame_elapsed_ns;
        log_debug("[PIPELINE_CAPTURE] g_snapshot_actual_duration_ms=%llu, last_capture_elapsed_ns=%llu",
                  (unsigned long long)actual_ms, (unsigned long long)last_frame_elapsed_ns);

        break;
      }
    }
  }

  atomic_store_bool(&pipeline->capture_finished, true);
  log_debug("[PIPELINE_CAPTURE] Capture thread exiting");
  return NULL;
}

/* ============================================================================
 * Encode Thread
 * ============================================================================ */

static void *pipeline_encode_thread(void *arg) {
  session_pipeline_t *pipeline = (session_pipeline_t *)arg;
  log_debug("[PIPELINE_ENCODE] Starting encode thread");

  uint64_t frames_processed = 0;
  while (true) {
    // Drain queued frames before exiting, even when capture was interrupted.
    pipeline_frame_t *frame = (pipeline_frame_t *)frame_queue_pop(pipeline->encode_queue, 100 * NS_PER_MS_INT);

    if (!frame) {
      // An empty queue is final only after the producer has exited.
      if (atomic_load_bool(&pipeline->capture_finished) && frame_queue_count(pipeline->encode_queue) == 0)
        break; // No producer remains and the queue has drained.
      log_debug_every(1 * US_PER_SEC_INT, "[PIPELINE_ENCODE] Waiting for frames (processed=%llu)",
                      (unsigned long long)frames_processed);
      continue; // timeout, keep waiting
    }

    ASCII_OTEL_SCOPE_PARENT(PIPELINE_ENCODE, &frame->telemetry_context);
    // Validate frame before processing (catch memory corruption)
    if (!frame_is_valid(frame)) {
      ascii_otel_metric(ASCII_OTEL_METRIC_DROP_INVALID, 1);
      log_error("Skipping frame with corrupted dimensions in encode thread");
      free_frame(frame);
      continue;
    }

    if (frames_processed == 0 || frames_processed % 30 == 0) {
      log_debug_every(US_PER_SEC_INT, "[PIPELINE_ENCODE] Processing frame %llu: %dx%d",
                      (unsigned long long)frames_processed, frame->w, frame->h);
    }

    // Encode frame (convert_to_ascii is called internally by encode_frame)
    // Avoid double-conversion that was causing state desynchronization between display/encode threads
    image_t raw_image = {.w = frame->w, .h = frame->h, .pixels = (rgb_pixel_t *)frame->pixels};
    pipeline->encode_error = session_display_encode_frame(pipeline->display, &raw_image, frame->captured_ns);
    if (pipeline->encode_error == ASCIICHAT_OK) {
      ascii_otel_metric(ASCII_OTEL_METRIC_FRAME_ENCODED, 1);
      ascii_otel_metric(ASCII_OTEL_METRIC_ENCODE_LATENCY,
                        (double)(time_get_ns() - frame->acquisition_started_ns) / 1e9);
    }
    if (pipeline->encode_error != ASCIICHAT_OK) {
      atomic_store_bool(&pipeline->stop, true);
      free_frame(frame);
      break;
    }
    frames_processed++;

    free_frame(frame);
  }

  log_debug("[PIPELINE_ENCODE] Encode thread exiting (total frames encoded=%llu)",
            (unsigned long long)frames_processed);
  return NULL;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

asciichat_error_t session_pipeline_create(session_capture_ctx_t *capture, session_display_ctx_t *display,
                                          session_pipeline_t **out) {
  if (!capture || !display || !out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL argument");
  }

  session_pipeline_t *p = SAFE_CALLOC(1, sizeof(*p), session_pipeline_t *);
  for (size_t i = 0; i < PIPELINE_FRAME_SLOTS; ++i)
    atomic_init(&p->frame_slots[i].busy, false);
  p->capture = capture;
  p->display = display;
  p->display_queue = frame_queue_create(1, "display");
  p->encode_queue = frame_queue_create(256, "encode"); // Large buffer for slow encoding

  // Check if display has render_file configured (will be checked in encode thread)
  // We store a flag to know whether to enqueue frames for encoding
  double render_fps = session_display_get_render_fps(display);
  bool has_file = session_display_has_render_file(display);
  media_source_t *source = session_capture_get_media_source(capture);
  if (media_source_uses_webcam(source) || media_source_get_type(source) == MEDIA_SOURCE_FILE)
    session_display_set_render_live_timing(display);

  // Playback FPS also has a default when no recording was requested. It must
  // not enable the encoder's lossless queue for a live-only session.
  p->has_render_file = has_file;
  log_debug("[PIPELINE_CREATE] render_fps=%.1f, has_file=%d, has_render_file=%d", render_fps, has_file ? 1 : 0,
            p->has_render_file ? 1 : 0);

  atomic_store_bool(&p->stop, false);
  atomic_store_bool(&p->capture_finished, false);
  NAMED_REGISTER_ATOMIC(&p->capture_finished, "pipeline_capture_finished", p);
  atomic_store_u64(&p->first_frame_ns, 0);

  // Start capture thread
  if (acthread_create(&p->capture_tid, "pipeline_capture", pipeline_capture_thread, p) != 0) {
    frame_queue_destroy(p->display_queue);
    frame_queue_destroy(p->encode_queue);
    NAMED_UNREGISTER(&p->capture_finished);
    SAFE_FREE(p);
    return SET_ERRNO(ERROR_INIT, "failed to start capture thread");
  }

  // Start encode thread only if render_file is active
  if (p->has_render_file) {
    if (acthread_create(&p->encode_tid, "pipeline_encode", pipeline_encode_thread, p) != 0) {
      atomic_store_bool(&p->stop, true);
      thread_join(&p->capture_tid, NULL);
      frame_queue_destroy(p->display_queue);
      frame_queue_destroy(p->encode_queue);
      NAMED_UNREGISTER(&p->capture_finished);
      SAFE_FREE(p);
      return SET_ERRNO(ERROR_INIT, "failed to start encode thread");
    }
  }

  *out = p;
  return ASCIICHAT_OK;
}

// Input has its own bounded poll cadence; ASCII conversion and terminal output
// must never determine when an interactive command is consumed.
static void *pipeline_input_thread(void *data) {
  session_pipeline_t *pipeline = data;
  while (!atomic_load_bool(&pipeline->stop) && !pipeline->should_exit(pipeline->input_data)) {
    keyboard_key_t key = ui_input_read_key(UI_SCREEN_MEDIA);
    if (key != KEY_NONE)
      pipeline->keyboard_handler(pipeline->capture, (int)key, pipeline->input_data);
    else
      platform_sleep_ms(10);
  }
  return NULL;
}

asciichat_error_t session_pipeline_run_main(session_pipeline_t *pipeline, session_should_exit_fn should_exit,
                                            session_keyboard_handler_fn keyboard_handler, void *user_data) {
  if (!pipeline || !should_exit) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL argument");
  }

  log_debug("[PIPELINE_MAIN] Starting main thread loop");
  if (keyboard_handler && !GET_OPTION(snapshot_mode)) {
    asciichat_error_t keyboard_result = keyboard_init();
    if (keyboard_result != ASCIICHAT_OK)
      return keyboard_result;
    pipeline->keyboard_handler = keyboard_handler;
    pipeline->should_exit = should_exit;
    pipeline->input_data = user_data;
    if (acthread_create(&pipeline->input_tid, "pipeline_input", pipeline_input_thread, pipeline) != ASCIICHAT_OK)
      return SET_ERRNO(ERROR_THREAD, "Cannot start responsive session input");
  }

  // Snapshot duration is owned by the capture thread. It records the first
  // captured frame and marks capture_finished at its duration limit.
  // The display thread waits for producer completion instead of using
  // a second timer based on terminal rendering speed; large terminals can make
  // ASCII conversion and render-file encoding substantially slower than capture.
  while (!should_exit(user_data) && !atomic_load_bool(&pipeline->stop)) {
    bool exit_check = should_exit(user_data);
    log_debug_every(US_PER_SEC_INT, "[PIPELINE_DEBUG] should_exit=%d, stop=%d", exit_check,
                    atomic_load_bool(&pipeline->stop));

    pipeline_frame_t *frame = (pipeline_frame_t *)frame_queue_pop(pipeline->display_queue, 100 * NS_PER_MS_INT);

    if (!frame) {
      if (atomic_load_bool(&pipeline->capture_finished) && frame_queue_count(pipeline->display_queue) == 0)
        break;
      continue;
    }

    ASCII_OTEL_SCOPE_PARENT(PIPELINE_DISPLAY, &frame->telemetry_context);
#ifdef ASCIICHAT_WITH_OTEL
    ascii_otel_scope_int(&otel_scope, "ascii_chat.queue.wait_ns", (int64_t)(time_get_ns() - frame->captured_ns));
#endif
    // Validate frame structure exists before accessing fields
    if (!frame_is_valid(frame)) {
      ascii_otel_metric(ASCII_OTEL_METRIC_DROP_INVALID, 1);
      log_error("Received invalid frame: corrupted or NULL");
      free_frame(frame);
      frame = NULL;
      continue;
    }

    // Check if help screen is active - if so, don't render ASCII frames
    int screen = ui_controller_current_screen();
    bool help_is_active = screen == UI_SCREEN_PROMPT || screen == UI_SCREEN_HELP || screen == UI_SCREEN_LOGS;

    if (!help_is_active) {
      // Convert to ASCII
      image_t tmp = {.w = frame->w, .h = frame->h, .pixels = (rgb_pixel_t *)frame->pixels};
      uint64_t conversion_started_ns = time_get_ns();
      char *ascii = session_display_convert_to_ascii_reuse(pipeline->display, &tmp);
      uint64_t converted_ns = time_get_ns();
      double read_ms = (double)(frame->captured_ns - frame->acquisition_started_ns) / NS_PER_MS_INT;
      double queue_ms = (double)(conversion_started_ns - frame->captured_ns) / NS_PER_MS_INT;
      double convert_ms = (double)(converted_ns - conversion_started_ns) / NS_PER_MS_INT;
      uint64_t epoch = frame->source_epoch;
      bool fresh = frame->fresh;
      uint64_t acquisition_started_ns = frame->acquisition_started_ns;
      free_frame(frame);
      frame = NULL;

      if (ascii) {
        if (epoch == session_capture_source_epoch(pipeline->capture)) {
          ui_controller_frame_acquired_at(acquisition_started_ns);
          session_display_write_ascii_frame(pipeline->display, ascii, fresh);
          ui_controller_frame_acquired_at(0);
          uint64_t published_ns = time_get_ns();
          ascii_otel_metric(ASCII_OTEL_METRIC_FRAME_DISPLAYED, 1);
          ascii_otel_metric(ASCII_OTEL_METRIC_FRAME_LATENCY, (double)(published_ns - acquisition_started_ns) / 1e9);
          session_capture_complete_open(pipeline->capture, epoch);
          log_debug("LIVE_FRAME: capture_to_publish_ms=%.2f fresh=%d read_ms=%.2f queue_ms=%.2f convert_ms=%.2f "
                    "publish_ms=%.2f",
                    (double)(published_ns - acquisition_started_ns) / NS_PER_MS_INT, fresh, read_ms, queue_ms,
                    convert_ms, (double)(published_ns - converted_ns) / NS_PER_MS_INT);
        }
        session_display_release_ascii(pipeline->display, ascii);
      } else {
        ascii_otel_metric(ASCII_OTEL_METRIC_DROP_CONVERT, 1);
        log_error_every(US_PER_SEC_INT, "Failed to convert captured frame to ASCII; skipping display frame");
      }
    } else {
      ascii_otel_metric(ASCII_OTEL_METRIC_DROP_SCREEN, 1);
      free_frame(frame);
      frame = NULL;
    }
  }

  log_debug("[PIPELINE_MAIN] Main loop exiting, signaling threads to stop");
  atomic_store_bool(&pipeline->stop, true);
  if (thread_is_initialized(&pipeline->input_tid) && thread_join(&pipeline->input_tid, NULL) != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_THREAD, "Cannot join session input thread");

  return ASCIICHAT_OK;
}

asciichat_error_t session_pipeline_destroy(session_pipeline_t *pipeline) {
  if (!pipeline)
    return ASCIICHAT_OK;

  log_debug("[PIPELINE] Destroying pipeline, waiting for threads...");

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

  if (thread_is_initialized(&pipeline->input_tid) && thread_join(&pipeline->input_tid, NULL) != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_THREAD, "Cannot join session input thread during shutdown");
  session_display_begin_drain(pipeline->display, 0, false);

  // A timeout must never let the owner free memory still referenced by a worker.
  // Capture observes stop; encode drains its queue and exits once capture is done.
  if (thread_is_initialized(&pipeline->capture_tid)) {
    if (thread_join(&pipeline->capture_tid, NULL) != 0)
      return SET_ERRNO(ERROR_THREAD, "Cannot join pipeline capture thread");
  }
  session_display_begin_drain(pipeline->display, pipeline->frames_accepted, true);
  if (thread_is_initialized(&pipeline->encode_tid)) {
    if (thread_join(&pipeline->encode_tid, NULL) != 0)
      return SET_ERRNO(ERROR_THREAD, "Cannot join pipeline encode thread");
  }

  // Flush queues to free any remaining frames
  frame_queue_flush(pipeline->display_queue, free_frame_generic);
  frame_queue_flush(pipeline->encode_queue, free_frame_generic);

  frame_slots_destroy(pipeline->frame_slots);
  frame_queue_destroy(pipeline->display_queue);
  frame_queue_destroy(pipeline->encode_queue);
  NAMED_UNREGISTER(&pipeline->capture_finished);
  asciichat_error_t err = pipeline->capture_failure.code != ASCIICHAT_OK ? errno_import(&pipeline->capture_failure)
                                                                         : pipeline->encode_error;
  if (pipeline->capture_failure.code != ASCIICHAT_OK)
    LOG_ERRNO_IF_SET("Media capture failed");
  SAFE_FREE(pipeline);

  log_debug("[PIPELINE] Pipeline destroyed");
  return err;
}
