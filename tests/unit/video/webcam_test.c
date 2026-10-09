#include <criterion/criterion.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/video/anim/test_pattern.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <string.h>
#include <math.h>

TestSuite(webcam);

Test(webcam, test_pattern_deterministic_and_independent) {
  test_pattern_t *a = NULL, *b = NULL;
  cr_assert_eq(test_pattern_create(320, 240, &a), ASCIICHAT_OK);
  cr_assert_eq(test_pattern_create(320, 240, &b), ASCIICHAT_OK);
  for (int index = 0; index < 2; index++) {
    cr_assert_eq(test_pattern_render(a, index, 2000, false), ASCIICHAT_OK);
    cr_assert_eq(test_pattern_render(b, index, 2000, false), ASCIICHAT_OK);
    image_t *ia = test_pattern_image(a), *ib = test_pattern_image(b);
    cr_assert_neq(ia->pixels, ib->pixels);
    cr_assert_eq(memcmp(ia->pixels, ib->pixels, 320 * 240 * sizeof(rgb_pixel_t)), 0);
    cr_assert_eq(test_pattern_render(b, index, 2500, false), ASCIICHAT_OK);
    cr_assert_neq(memcmp(ia->pixels, ib->pixels, 320 * 240 * sizeof(rgb_pixel_t)), 0);
  }
  test_pattern_destroy(a);
  test_pattern_destroy(b);
}

Test(webcam, test_pattern_resize_and_invalid_input) {
  test_pattern_t *p = NULL;
  cr_assert_eq(test_pattern_create(1, 1, &p), ASCIICHAT_OK);
  for (int index = 0; index < 2; index++)
    cr_assert_eq(test_pattern_render(p, index, 0, false), ASCIICHAT_OK);
  cr_assert_eq(test_pattern_resize(p, 80, 48), ASCIICHAT_OK);
  image_t *image = test_pattern_image(p);
  cr_assert_eq(test_pattern_resize(p, 80, 48), ASCIICHAT_OK);
  cr_assert_eq(test_pattern_image(p), image);
  cr_assert_neq(test_pattern_render(p, 2, 0, false), ASCIICHAT_OK);
  cr_assert_neq(test_pattern_render(p, 0, NAN, false), ASCIICHAT_OK);
  cr_assert_neq(test_pattern_render(p, 0, -1, false), ASCIICHAT_OK);
  cr_assert_neq(test_pattern_resize(p, 0, 48), ASCIICHAT_OK);
  test_pattern_destroy(p);
  test_pattern_destroy(NULL);
}

Test(webcam, test_pattern_circle_wraps_and_cadence_is_opt_in) {
  test_pattern_t *p = NULL;
  cr_assert_eq(test_pattern_create(800, 400, &p), ASCIICHAT_OK);
  cr_assert_eq(test_pattern_render(p, 1, 2000, false), ASCIICHAT_OK);
  image_t *image = test_pattern_image(p);
  rgb_pixel_t center = image->pixels[200 * 800 + 400];
  cr_assert_eq(center.r, 255);
  cr_assert_eq(center.g, 255);
  cr_assert_eq(center.b, 255);
  cr_assert_eq(test_pattern_render(p, 1, 0, false), ASCIICHAT_OK);
  rgb_pixel_t edge = image->pixels[200 * 800 + 799];
  cr_assert(edge.r != 255 || edge.g != 255 || edge.b != 255);
  cr_assert_eq(test_pattern_render(p, 1, 0, true), ASCIICHAT_OK);
  for (int i = 0; i < 800 * 400; i++)
    cr_assert_eq(image->pixels[i].r, 0);
  test_pattern_destroy(p);
}

Test(webcam, palette_round_trip_and_endpoints) {
  for (int i = 16; i < 256; i++) {
    uint8_t r, g, b, rr, gg, bb;
    get_256color_rgb((uint8_t)i, &r, &g, &b);
    get_256color_rgb(rgb_to_256color(r, g, b), &rr, &gg, &bb);
    cr_assert_eq(r, rr);
    cr_assert_eq(g, gg);
    cr_assert_eq(b, bb);
  }
  cr_assert_eq(rgb_to_256color(0, 0, 0), 16);
  cr_assert_eq(rgb_to_256color(255, 255, 255), 231);
  for (int i = 0; i < 16; i++) {
    uint8_t r, g, b;
    get_16color_rgb((uint8_t)i, &r, &g, &b);
    cr_assert_eq(rgb_to_16color(r, g, b), i);
  }
}
