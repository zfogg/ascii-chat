#include <criterion/criterion.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/options/colorscheme.h>

#define g_snapshot_first_frame_rendered test_pipeline_snapshot_first_frame_rendered
bool g_snapshot_first_frame_rendered = false;
#include "../../../src/common/session/pipeline.c"

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
