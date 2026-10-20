#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/options/colorscheme.h>

#define g_snapshot_first_frame_rendered test_pipeline_snapshot_first_frame_rendered
bool g_snapshot_first_frame_rendered = false;
#include "../../../src/common/session/pipeline.c"

#define CHECK(value)                                                                                                   \
  do {                                                                                                                 \
    if (!(value))                                                                                                      \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #value);                                                  \
  } while (0)

int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  frame_queue_t *queue = frame_queue_create(1, "latest_frame_test");
  CHECK(queue);
  unsigned frames[1000];
  for (unsigned i = 0; i < 1000; ++i) {
    frames[i] = i;
    void *retired = frame_queue_replace_latest(queue, &frames[i]);
    CHECK(retired == (i ? &frames[i - 1] : NULL));
    CHECK(frame_queue_count(queue) == 1);
  }
  CHECK(frame_queue_pop(queue, 1) == &frames[999]);
  CHECK(frame_queue_count(queue) == 0);
  CHECK(frame_queue_pop(queue, 1) == NULL);
  CHECK(frame_queue_replace_latest(queue, &frames[0]) == NULL);
  CHECK(frame_queue_pop(queue, 1) == &frames[0]);
  frame_queue_destroy(queue);
  // Three simultaneous owners retain immutable pixels; overflow uses an owned fallback.
  frame_slot_t slots[PIPELINE_FRAME_SLOTS] = {0};
  image_t *source = image_new(8, 4);
  CHECK(source);
  pipeline_frame_t *held[4];
  for (int i = 0; i < 4; ++i) {
    memset(source->pixels, i + 1, 8 * 4 * sizeof(rgb_pixel_t));
    held[i] = make_frame_copy(source, slots);
    CHECK(held[i]);
    CHECK((held[i]->slot != NULL) == (i < 3));
  }
  for (int i = 0; i < 4; ++i)
    CHECK(held[i]->pixels[0] == i + 1);
  const uint8_t *reusable = held[0]->pixels;
  free_frame(held[0]);
  held[0] = make_frame_copy(source, slots);
  CHECK(held[0]->pixels == reusable);
  for (int i = 0; i < 4; ++i)
    free_frame(held[i]);
  image_destroy(source);
  source = image_new(32, 8);
  memset(source->pixels, 90, 32 * 8 * sizeof(rgb_pixel_t));
  held[0] = make_frame_copy(source, slots);
  CHECK(held[0]->w == 32 && held[0]->h == 8 && held[0]->pixels[767] == 90);
  free_frame(held[0]);
  frame_slots_destroy(slots);

  // Reuse produces exactly the same output, and size changes replace scratch safely.
  terminal_capabilities_t caps = {.color_level = TERM_COLOR_TRUECOLOR};
  image_t *workspace = NULL;
  const int widths[] = {40, 40, 600, 20};
  for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
    char *owned = ascii_convert_with_capabilities_fit(source, widths[i], 6, &caps, true, false, " .:#", false);
    char *reused = ascii_convert_with_capabilities_workspace(source, widths[i], 6, &caps, true, false, " .:#",
                                                            false, &workspace);
    CHECK(owned && reused && strcmp(owned, reused) == 0);
    CHECK(workspace->w == widths[i]);
    SAFE_FREE(owned);
    SAFE_FREE(reused);
  }
  image_destroy(workspace);
  image_destroy(source);
  return 0;
}
