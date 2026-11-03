#include <criterion/criterion.h>
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
static int pipeline_create_thread(asciichat_thread_t *thread, const char *name, void *(*fn)(void *), void *arg) {
  ++pipeline_thread_calls;
  if (pipeline_thread_calls == pipeline_fail_thread_at)
    return -1;
  if (pipeline_fake_workers)
    return 0;
  return acthread_create(thread, name, fn, arg);
}
static int pipeline_join_thread(asciichat_thread_t *thread, void **result) {
  ++pipeline_join_calls;
  return pipeline_fake_workers ? 0 : thread_join(thread, result);
}
static asciichat_error_t pipeline_create_wakeup(platform_wakeup_t **wakeup) {
  return pipeline_fail_wakeup ? ERROR_MEMORY : platform_wakeup_create(wakeup);
}
static bool pipeline_recording(session_display_ctx_t *display) {
  return pipeline_force_recording || session_display_has_render_file(display);
}
#define acthread_create pipeline_create_thread
#define thread_join pipeline_join_thread
#define platform_wakeup_create pipeline_create_wakeup
#define session_display_has_render_file pipeline_recording
#include "../../../src/common/session/pipeline.c"
#undef acthread_create
#undef thread_join
#undef platform_wakeup_create
#undef session_display_has_render_file

Test(media_pipeline, bounded_queue_preserves_fifo_and_rejects_overflow) {
  log_init(NULL, LOG_ERROR, false, false);
  frame_queue_t *queue = frame_queue_create(2, "bounded_frame_test");
  cr_assert(queue);
  pipeline_frame_t frames[3] = {0};
  for (unsigned i = 0; i < 1000; ++i) {
    for (unsigned frame = 0; frame < 3; ++frame)
      frames[frame].captured_ns = time_get_ns();
    cr_assert(frame_queue_push(queue, &frames[0], 0));
    cr_assert(frame_queue_push(queue, &frames[1], 0));
    cr_assert_not(frame_queue_push(queue, &frames[2], 0));
    cr_assert_eq(frame_queue_count(queue), 2);
    cr_assert_eq(frame_queue_pop(queue, 1), &frames[0]);
    cr_assert_eq(frame_queue_pop(queue, 1), &frames[1]);
    cr_assert_eq(frame_queue_count(queue), 0);
    cr_assert_eq(frame_queue_pop(queue, 1), NULL);
  }
  cr_assert(frame_queue_push(queue, &frames[2], 0));
  cr_assert_eq(frame_queue_pop(queue, 1), &frames[2]);
  frame_queue_destroy(queue);
}

Test(media_pipeline, slots_preserve_pixels_and_reuse_released_storage) {
  // Three simultaneous owners retain immutable pixels; overflow uses an owned fallback.
  frame_slot_t slots[PIPELINE_FRAME_SLOTS] = {0};
  image_t *source = image_new(8, 4);
  cr_assert(source);
  pipeline_frame_t *held[4];
  for (int i = 0; i < 4; ++i) {
    memset(source->pixels, i + 1, 8 * 4 * sizeof(rgb_pixel_t));
    held[i] = make_frame_copy(source, slots);
    cr_assert(held[i]);
    cr_assert_eq((held[i]->slot != NULL), (i < 3));
  }
  for (int i = 0; i < 4; ++i)
    cr_assert_eq(held[i]->pixels[0], i + 1);
  const uint8_t *reusable = held[0]->pixels;
  free_frame(held[0]);
  held[0] = make_frame_copy(source, slots);
  cr_assert_eq(held[0]->pixels, reusable);
  for (int i = 0; i < 4; ++i)
    free_frame(held[i]);
  image_destroy(source);
  source = image_new(32, 8);
  memset(source->pixels, 90, 32 * 8 * sizeof(rgb_pixel_t));
  held[0] = make_frame_copy(source, slots);
  cr_assert(held[0]->w == 32 && held[0]->h == 8 && held[0]->pixels[767] == 90);
  free_frame(held[0]);
  frame_slots_destroy(slots);

  image_destroy(source);
}

Test(media_pipeline, reusable_render_matches_owned_output) {
  image_t *source = image_new(32, 8);
  cr_assert_not_null(source);
  memset(source->pixels, 90, 32 * 8 * sizeof(rgb_pixel_t));
  // Reuse produces exactly the same output, and size changes replace scratch safely.
  terminal_capabilities_t caps = {.color_level = TERM_COLOR_TRUECOLOR};
  image_t *workspace = NULL;
  const int widths[] = {40, 40, 600, 20};
  for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
    char *owned = ascii_convert_with_capabilities_fit(source, widths[i], 6, &caps, true, false, " .:#", false);
    char *reused =
        ascii_convert_with_capabilities_workspace(source, widths[i], 6, &caps, true, false, " .:#", false, &workspace);
    cr_assert(owned && reused && strcmp(owned, reused) == 0);
    cr_assert_eq(workspace->w, widths[i]);
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
        cr_assert(owned && borrowed && borrowed == output.buf && strcmp(owned, borrowed) == 0);
        SAFE_FREE(owned);
      }
    }
  }
  char *buffer = output.buf;
  caps.render_mode = RENDER_MODE_FOREGROUND;
  caps.color_level = TERM_COLOR_TRUECOLOR;
  for (unsigned i = 0; i < 1000; ++i) {
    source->pixels[0].r = (uint8_t)i;
    cr_assert_eq(ascii_convert_with_buffers(source, 20, 6, &caps, true, false, " .:#", false, &workspace, &output),
                 buffer);
  }
  SAFE_FREE(output.buf);
  image_destroy(workspace);
  image_destroy(source);
}

Test(media_pipeline, flushing_queued_frames_releases_each_owned_frame_and_slots_can_be_reused){
  frame_slot_t slots[PIPELINE_FRAME_SLOTS]={0};image_t *image=image_new(2,2);cr_assert_not_null(image);memset(image->pixels,70,12);
  frame_queue_t *queue=frame_queue_create(4,"display");cr_assert_not_null(queue);
  for(unsigned i=0;i<4;++i){pipeline_frame_t *f=make_frame_copy(image,slots);cr_assert_not_null(f);cr_assert(frame_queue_push(queue,f,0));}
  frame_queue_flush(queue,free_frame_generic);cr_assert_eq(frame_queue_count(queue),0);
  for(unsigned i=0;i<PIPELINE_FRAME_SLOTS;++i)cr_assert_not(atomic_load_explicit(&slots[i].busy,memory_order_acquire));
  pipeline_frame_t *f = make_frame_copy(image, slots);
  cr_assert_not_null(f);
  cr_assert_eq(f->pixels[0], 70);
  free_frame(f);
  frame_queue_flush(queue, NULL);
  frame_queue_flush(NULL, free_frame_generic);
  cr_assert_eq(frame_queue_count(NULL), 0);
  frame_queue_destroy(NULL);
  free_frame(NULL);
  frame_queue_destroy(queue);
  frame_slots_destroy(slots);
  image_destroy(image);
}
Test(media_pipeline, timed_full_queue_rejects_new_ownership_without_discarding_the_existing_frame) {
  frame_queue_t *queue = frame_queue_create(1, "encode");
  pipeline_frame_t first = {.captured_ns = time_get_ns()}, second = {.captured_ns = time_get_ns()};
  cr_assert(frame_queue_push(queue, &first, 0));
  cr_assert_not(frame_queue_push(queue, &second, NS_PER_MS_INT));
  cr_assert_eq(frame_queue_count(queue), 1);
  cr_assert_eq(frame_queue_pop(queue, 1), &first);
  cr_assert(frame_queue_push(queue, &second, NS_PER_MS_INT));
  cr_assert_eq(frame_queue_pop(queue, 1), &second);
  frame_queue_destroy(queue);
}
Test(media_pipeline, corrupted_dimensions_and_missing_pixels_are_rejected_before_rendering) {
  const int dims[][2] = {{0, 1}, {1, 0}, {-1, 1}, {1, -1}, {10001, 1}, {1, 10001}};
  uint8_t pixels[3] = {1, 2, 3};
  cr_assert_not(frame_is_valid(NULL));
  for (unsigned i = 0; i < ARRAY_SIZE(dims); ++i) {
    pipeline_frame_t f = {.w = dims[i][0], .h = dims[i][1], .pixels = pixels};
    cr_assert_not(frame_is_valid(&f));
  }
  pipeline_frame_t f = {.w = 1, .h = 1};
  cr_assert_not(frame_is_valid(&f));
  f.pixels = pixels;
  cr_assert(frame_is_valid(&f));
  const int invalid[][2] = {{0, 1}, {1, 0}, {-1, 1}, {1, -1}};
  for (unsigned i = 0; i < ARRAY_SIZE(invalid); ++i) {
    image_t source = {.w = invalid[i][0], .h = invalid[i][1], .pixels = (rgb_pixel_t *)pixels};
    cr_assert_null(make_frame_copy(&source, NULL));
    CLEAR_ERRNO_ALL();
  }
}
static void pipeline_partial_start(unsigned fail_thread, bool fail_wakeup, bool recording) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  session_capture_config_t capture_config = {.type = MEDIA_SOURCE_TEST, .target_fps = 30};
  session_capture_ctx_t *capture = session_capture_create(&capture_config);
  cr_assert_not_null(capture);
  session_display_config_t display_config = {.snapshot_mode = true, .color_mode = TERM_COLOR_NONE};
  session_display_ctx_t *display = session_display_create(&display_config);
  cr_assert_not_null(display);
  pipeline_fake_workers = true;
  pipeline_fail_thread_at = fail_thread;
  pipeline_fail_wakeup = fail_wakeup;
  pipeline_force_recording = recording;
  session_pipeline_t *pipeline = NULL;
  cr_assert_eq(session_pipeline_create(capture, display, &pipeline), ERROR_INIT);
  cr_assert_null(pipeline);
  cr_assert_eq(pipeline_thread_calls, fail_wakeup ? 0 : fail_thread);
  cr_assert_eq(pipeline_join_calls, fail_thread == 2 ? 1 : 0);
  cr_assert_not_null(session_capture_get_media_source(capture));
  cr_assert_not(session_display_has_render_file(display));
  session_display_destroy(display);
  session_capture_destroy(capture);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
Test(media_pipeline, failed_display_wakeup_does_not_start_workers_or_consume_borrowed_contexts) {
  pipeline_partial_start(0, true, false);
}
Test(media_pipeline, failed_capture_worker_rolls_back_queues_without_consuming_borrowed_contexts) {
  pipeline_partial_start(1, false, false);
}
Test(media_pipeline, failed_encode_worker_joins_capture_before_releasing_its_slots) {
  pipeline_partial_start(2, false, true);
}
