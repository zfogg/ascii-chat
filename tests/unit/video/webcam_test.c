#include <criterion/criterion.h>
#include <string.h>

#include <ascii-chat/media/source.h>
#include <ascii-chat/video/rgba/image.h>

TestSuite(webcam);

static media_source_t *create_test_source(void) {
  media_source_t *source = media_source_create(MEDIA_SOURCE_TEST, NULL);
  cr_assert_not_null(source);
  return source;
}

Test(webcam, test_pattern_dimensions_and_colors) {
  media_source_t *source = create_test_source();
  image_t *frame = media_source_read_video(source);
  cr_assert_not_null(frame);
  cr_assert_eq(frame->w, 320);
  cr_assert_eq(frame->h, 240);
  cr_assert_not_null(frame->pixels);
  cr_assert_eq(frame->pixels[320 + 1].r, 255);
  cr_assert_eq(frame->pixels[320 + 41].g, 255);
  cr_assert_eq(frame->pixels[320 + 81].b, 255);
  cr_assert_eq(frame->pixels[320 + 40].r, 0);
  cr_assert_eq(frame->pixels[320 + 40].g, 0);
  cr_assert_eq(frame->pixels[320 + 40].b, 0);
  cr_assert(media_source_has_video(source));
  cr_assert_not(media_source_has_audio(source));
  media_source_destroy(source);
}

Test(webcam, test_pattern_reuses_buffer_and_animates_five_times_faster) {
  media_source_t *source = create_test_source();
  image_t *frame = media_source_read_video(source);
  cr_assert_not_null(frame);
  rgb_pixel_t *pixels = frame->pixels;
  rgb_pixel_t initial_row[320];
  memcpy(initial_row, pixels + 320, sizeof(initial_row));

  // Ten frame intervals advance 25 pixels, rather than the original five.
  for (unsigned int index = 1; index <= 10; index++) {
    image_t *next = media_source_read_video(source);
    cr_assert_eq(next, frame);
    cr_assert_eq(next->pixels, pixels);
    unsigned int phase = index * 5 / 2;
    for (unsigned int x = 0; x < 320; x++) {
      cr_assert_eq(memcmp(&next->pixels[320 + x], &initial_row[(x + phase) % 320], sizeof(rgb_pixel_t)), 0);
    }
  }
  media_source_destroy(source);
}

Test(webcam, test_pattern_sources_have_independent_state) {
  media_source_t *first = create_test_source();
  media_source_t *second = create_test_source();
  image_t *first_frame = media_source_read_video(first);
  cr_assert_not_null(first_frame);
  rgb_pixel_t initial_row[320];
  memcpy(initial_row, first_frame->pixels + 320, sizeof(initial_row));
  media_source_read_video(first);
  image_t *second_frame = media_source_read_video(second);
  cr_assert_not_null(second_frame);
  cr_assert_neq(first_frame, second_frame);
  cr_assert_neq(first_frame->pixels, second_frame->pixels);
  cr_assert_eq(memcmp(second_frame->pixels + 320, initial_row, sizeof(initial_row)), 0);
  media_source_destroy(first);
  cr_assert_not_null(media_source_read_video(second));
  media_source_destroy(second);
}

Test(webcam, test_pattern_recreate_resets_animation) {
  for (int cycle = 0; cycle < 3; cycle++) {
    media_source_t *source = create_test_source();
    image_t *frame = media_source_read_video(source);
    cr_assert_not_null(frame);
    cr_assert_eq(frame->pixels[320 + 39].r, 255);
    cr_assert_eq(frame->pixels[320 + 39].g, 0);
    cr_assert_not_null(media_source_read_video(source));
    media_source_destroy(source);
  }
  media_source_destroy(NULL);
}
