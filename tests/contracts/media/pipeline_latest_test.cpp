#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/options/colorscheme.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/wakeup.h>

#define g_snapshot_first_frame_rendered test_pipeline_snapshot_first_frame_rendered
bool g_snapshot_first_frame_rendered = false;
#include "../../../src/common/session/pipeline.h"
static unsigned pipeline_fail_thread_at, pipeline_thread_calls, pipeline_join_calls;
static bool pipeline_fake_workers, pipeline_force_recording, pipeline_fail_wakeup;
static int pipeline_create_thread(platform_thread_t *thread, const char *name, void *(*fn)(void *), void *arg) {
  ++pipeline_thread_calls;
  if (pipeline_thread_calls == pipeline_fail_thread_at)
    return -1;
  if (pipeline_fake_workers)
    return 0;
  return platform_thread_create(thread, name, fn, arg);
}
static int pipeline_join_thread(platform_thread_t *thread, void **result) {
  ++pipeline_join_calls;
  return pipeline_fake_workers ? 0 : thread_join(thread, result);
}
static asciichat_error_t pipeline_create_wakeup(platform_wakeup_t **wakeup) {
  return pipeline_fail_wakeup ? ERROR_MEMORY : platform_wakeup_create(wakeup);
}
static bool pipeline_recording(session_display_ctx_t *display) {
  return pipeline_force_recording || session_display_has_render_file(display);
}
#define platform_thread_create pipeline_create_thread
#define thread_join pipeline_join_thread
#define platform_wakeup_create pipeline_create_wakeup
#define session_display_has_render_file pipeline_recording
#include "../../../src/common/session/pipeline.c"
#undef platform_thread_create
#undef thread_join
#undef platform_wakeup_create
#undef session_display_has_render_file

TEST_CASE("media_pipeline/bounded_queue_preserves_fifo_and_rejects_overflow", "[media_pipeline]") {
  log_init(NULL, LOG_ERROR, false, false);
  frame_queue_t *queue = frame_queue_create(2, "bounded_frame_test");
  ASCIICHAT_REQUIRE(queue);
  pipeline_frame_t frames[3] = {0};
  for (unsigned i = 0; i < 1000; ++i) {
    for (unsigned frame = 0; frame < 3; ++frame)
      frames[frame].captured_ns = time_get_ns();
    ASCIICHAT_REQUIRE(frame_queue_push(queue, &frames[0], 0));
    ASCIICHAT_REQUIRE(frame_queue_push(queue, &frames[1], 0));
    ASCIICHAT_REQUIRE_NOT(frame_queue_push(queue, &frames[2], 0));
    ASCIICHAT_REQUIRE_EQ(frame_queue_count(queue), 2);
    ASCIICHAT_REQUIRE_EQ(frame_queue_pop(queue, 1), &frames[0]);
    ASCIICHAT_REQUIRE_EQ(frame_queue_pop(queue, 1), &frames[1]);
    ASCIICHAT_REQUIRE_EQ(frame_queue_count(queue), 0);
    ASCIICHAT_REQUIRE_EQ(frame_queue_pop(queue, 1), NULL);
  }
  ASCIICHAT_REQUIRE(frame_queue_push(queue, &frames[2], 0));
  ASCIICHAT_REQUIRE_EQ(frame_queue_pop(queue, 1), &frames[2]);
  frame_queue_destroy(queue);
}

TEST_CASE("media_pipeline/slots_preserve_pixels_and_reuse_released_storage", "[media_pipeline]") {
  // Three simultaneous owners retain immutable pixels; overflow uses an owned fallback.
  frame_slot_t slots[PIPELINE_FRAME_SLOTS] = {0};
  image_t *source = image_new(8, 4);
  ASCIICHAT_REQUIRE(source);
  pipeline_frame_t *held[4];
  for (int i = 0; i < 4; ++i) {
    memset(source->pixels, i + 1, 8 * 4 * sizeof(rgb_pixel_t));
    held[i] = make_frame_copy(source, slots);
    ASCIICHAT_REQUIRE(held[i]);
    ASCIICHAT_REQUIRE_EQ((held[i]->slot != NULL), (i < 3));
  }
  for (int i = 0; i < 4; ++i)
    ASCIICHAT_REQUIRE_EQ(held[i]->pixels[0], i + 1);
  const uint8_t *reusable = held[0]->pixels;
  free_frame(held[0]);
  held[0] = make_frame_copy(source, slots);
  ASCIICHAT_REQUIRE_EQ(held[0]->pixels, reusable);
  for (int i = 0; i < 4; ++i)
    free_frame(held[i]);
  image_destroy(source);
  source = image_new(32, 8);
  memset(source->pixels, 90, 32 * 8 * sizeof(rgb_pixel_t));
  held[0] = make_frame_copy(source, slots);
  ASCIICHAT_REQUIRE(held[0]->w == 32 && held[0]->h == 8 && held[0]->pixels[767] == 90);
  free_frame(held[0]);
  frame_slots_destroy(slots);

  image_destroy(source);
}

TEST_CASE("media_pipeline/reusable_render_matches_owned_output", "[media_pipeline]") {
  image_t *source = image_new(32, 8);
  ASCIICHAT_REQUIRE_NOT_NULL(source);
  memset(source->pixels, 90, 32 * 8 * sizeof(rgb_pixel_t));
  // Reuse produces exactly the same output, and size changes replace scratch safely.
  terminal_capabilities_t caps = {.color_level = TERM_COLOR_TRUECOLOR};
  image_t *workspace = NULL;
  const int widths[] = {40, 40, 600, 20};
  for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
    char *owned = ascii_convert_with_capabilities_fit(source, widths[i], 6, &caps, true, false, " .:#", false);
    char *reused =
        ascii_convert_with_capabilities_workspace(source, widths[i], 6, &caps, true, false, " .:#", false, &workspace);
    ASCIICHAT_REQUIRE(owned && reused && strcmp(owned, reused) == 0);
    ASCIICHAT_REQUIRE_EQ(workspace->w, widths[i]);
    SAFE_FREE(owned);
    SAFE_FREE(reused);
  }
  outbuf_t output = {0};
  // Every color/render mode and a wide fallback must match the owned API.
  for (int render = 0; render < 3; ++render) {
    caps.render_mode = render;
    for (int color = TERM_COLOR_NONE; color <= TERM_COLOR_TRUECOLOR; ++color) {
      caps.color_level = color;
      for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        char *owned = ascii_convert_with_capabilities_fit(source, widths[i], 6, &caps, true, false, " .:#", false);
        char *borrowed =
            ascii_convert_with_buffers(source, widths[i], 6, &caps, true, false, " .:#", false, &workspace, &output);
        ASCIICHAT_REQUIRE(owned && borrowed && borrowed == output.buf && strcmp(owned, borrowed) == 0);
        SAFE_FREE(owned);
      }
    }
  }
  char *buffer = output.buf;
  caps.render_mode = RENDER_MODE_FOREGROUND;
  caps.color_level = TERM_COLOR_TRUECOLOR;
  for (unsigned i = 0; i < 1000; ++i) {
    source->pixels[0].r = (uint8_t)i;
    ASCIICHAT_REQUIRE_EQ(ascii_convert_with_buffers(source, 20, 6, &caps, true, false, " .:#", false, &workspace, &output),
                 buffer);
  }
  SAFE_FREE(output.buf);
  image_destroy(workspace);
  image_destroy(source);
}

TEST_CASE("media_pipeline/flushing_queued_frames_releases_each_owned_frame_and_slots_can_be_reused", "[media_pipeline]") {
  frame_slot_t slots[PIPELINE_FRAME_SLOTS] = {0};
  image_t *image = image_new(2, 2);
  ASCIICHAT_REQUIRE_NOT_NULL(image);
  memset(image->pixels, 70, 12);
  frame_queue_t *queue = frame_queue_create(4, "display");
  ASCIICHAT_REQUIRE_NOT_NULL(queue);
  for (unsigned i = 0; i < 4; ++i) {
    pipeline_frame_t *f = make_frame_copy(image, slots);
    ASCIICHAT_REQUIRE_NOT_NULL(f);
    ASCIICHAT_REQUIRE(frame_queue_push(queue, f, 0));
  }
  frame_queue_flush(queue, free_frame_generic);
  ASCIICHAT_REQUIRE_EQ(frame_queue_count(queue), 0);
  for (unsigned i = 0; i < PIPELINE_FRAME_SLOTS; ++i)
    ASCIICHAT_REQUIRE_NOT(atomic_load_explicit(&slots[i].busy, memory_order_acquire));
  pipeline_frame_t *f = make_frame_copy(image, slots);
  ASCIICHAT_REQUIRE_NOT_NULL(f);
  ASCIICHAT_REQUIRE_EQ(f->pixels[0], 70);
  free_frame(f);
  frame_queue_flush(queue, NULL);
  frame_queue_flush(NULL, free_frame_generic);
  ASCIICHAT_REQUIRE_EQ(frame_queue_count(NULL), 0);
  frame_queue_destroy(NULL);
  free_frame(NULL);
  frame_queue_destroy(queue);
  frame_slots_destroy(slots);
  image_destroy(image);
}
TEST_CASE("media_pipeline/timed_full_queue_rejects_new_ownership_without_discarding_the_existing_frame", "[media_pipeline]") {
  frame_queue_t *queue = frame_queue_create(1, "encode");
  pipeline_frame_t first = {.captured_ns = time_get_ns()}, second = {.captured_ns = time_get_ns()};
  ASCIICHAT_REQUIRE(frame_queue_push(queue, &first, 0));
  ASCIICHAT_REQUIRE_NOT(frame_queue_push(queue, &second, NS_PER_MS_INT));
  ASCIICHAT_REQUIRE_EQ(frame_queue_count(queue), 1);
  ASCIICHAT_REQUIRE_EQ(frame_queue_pop(queue, 1), &first);
  ASCIICHAT_REQUIRE(frame_queue_push(queue, &second, NS_PER_MS_INT));
  ASCIICHAT_REQUIRE_EQ(frame_queue_pop(queue, 1), &second);
  frame_queue_destroy(queue);
}
TEST_CASE("media_pipeline/corrupted_dimensions_and_missing_pixels_are_rejected_before_rendering", "[media_pipeline]") {
  const int dims[][2] = {{0, 1}, {1, 0}, {-1, 1}, {1, -1}, {10001, 1}, {1, 10001}};
  uint8_t pixels[3] = {1, 2, 3};
  ASCIICHAT_REQUIRE_NOT(frame_is_valid(NULL));
  for (unsigned i = 0; i < ARRAY_SIZE(dims); ++i) {
    pipeline_frame_t f = {.w = dims[i][0], .h = dims[i][1], .pixels = pixels};
    ASCIICHAT_REQUIRE_NOT(frame_is_valid(&f));
  }
  pipeline_frame_t f = {.w = 1, .h = 1};
  ASCIICHAT_REQUIRE_NOT(frame_is_valid(&f));
  f.pixels = pixels;
  ASCIICHAT_REQUIRE(frame_is_valid(&f));
  const int invalid[][2] = {{0, 1}, {1, 0}, {-1, 1}, {1, -1}};
  for (unsigned i = 0; i < ARRAY_SIZE(invalid); ++i) {
    image_t source = {.w = invalid[i][0], .h = invalid[i][1], .pixels = (rgb_pixel_t *)pixels};
    ASCIICHAT_REQUIRE_NULL(make_frame_copy(&source, NULL));
    CLEAR_ERRNO_ALL();
  }
}
static void pipeline_partial_start(unsigned fail_thread, bool fail_wakeup, bool recording) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  session_capture_config_t capture_config = {.type = MEDIA_SOURCE_TEST, .target_fps = 30};
  session_capture_ctx_t *capture = session_capture_create(&capture_config);
  ASCIICHAT_REQUIRE_NOT_NULL(capture);
  session_display_config_t display_config = {.snapshot_mode = true, .color_mode = TERM_COLOR_NONE};
  session_display_ctx_t *display = session_display_create(&display_config);
  ASCIICHAT_REQUIRE_NOT_NULL(display);
  pipeline_fake_workers = true;
  pipeline_fail_thread_at = fail_thread;
  pipeline_fail_wakeup = fail_wakeup;
  pipeline_force_recording = recording;
  session_pipeline_t *pipeline = NULL;
  ASCIICHAT_REQUIRE_EQ(session_pipeline_create(capture, display, &pipeline), ERROR_INIT);
  ASCIICHAT_REQUIRE_NULL(pipeline);
  ASCIICHAT_REQUIRE_EQ(pipeline_thread_calls, fail_wakeup ? 0 : fail_thread);
  ASCIICHAT_REQUIRE_EQ(pipeline_join_calls, fail_thread == 2 ? 1 : 0);
  ASCIICHAT_REQUIRE_NOT_NULL(session_capture_get_media_source(capture));
  ASCIICHAT_REQUIRE_NOT(session_display_has_render_file(display));
  session_display_destroy(display);
  session_capture_destroy(capture);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TEST_CASE("media_pipeline/failed_display_wakeup_does_not_start_workers_or_consume_borrowed_contexts", "[media_pipeline]") {
  pipeline_partial_start(0, true, false);
}
TEST_CASE("media_pipeline/failed_capture_worker_rolls_back_queues_without_consuming_borrowed_contexts", "[media_pipeline]") {
  pipeline_partial_start(1, false, false);
}
TEST_CASE("media_pipeline/failed_encode_worker_joins_capture_before_releasing_its_slots", "[media_pipeline]") {
  pipeline_partial_start(2, false, true);
}
