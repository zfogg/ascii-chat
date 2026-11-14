#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/video/anim/test_pattern.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <string.h>
#include <math.h>
TEST_CASE("webcam/test_pattern_deterministic_and_independent", "[webcam]") {
  test_pattern_t *a = NULL, *b = NULL;
  CLEAR_ERRNO();
  ASCIICHAT_REQUIRE_EQ(test_pattern_create(320, 240, &a), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(GET_ERRNO(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(test_pattern_create(320, 240, &b), ASCIICHAT_OK);
  for (int index = 0; index < 2; index++) {
    ASCIICHAT_REQUIRE_EQ(test_pattern_render(a, index, 2000, false), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(test_pattern_render(b, index, 2000, false), ASCIICHAT_OK);
    image_t *ia = test_pattern_image(a), *ib = test_pattern_image(b);
    ASCIICHAT_REQUIRE_NE(ia->pixels, ib->pixels);
    ASCIICHAT_REQUIRE_EQ(memcmp(ia->pixels, ib->pixels, 320 * 240 * sizeof(rgb_pixel_t)), 0);
    ASCIICHAT_REQUIRE_EQ(test_pattern_render(b, index, 2500, false), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_NE(memcmp(ia->pixels, ib->pixels, 320 * 240 * sizeof(rgb_pixel_t)), 0);
  }
  test_pattern_destroy(a);
  test_pattern_destroy(b);
}

TEST_CASE("webcam/test_pattern_resize_and_invalid_input", "[webcam]") {
  test_pattern_t *p = NULL;
  ASCIICHAT_REQUIRE_EQ(test_pattern_create(1, 1, &p), ASCIICHAT_OK);
  for (int index = 0; index < 2; index++)
    ASCIICHAT_REQUIRE_EQ(test_pattern_render(p, index, 0, false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(test_pattern_resize(p, 80, 48), ASCIICHAT_OK);
  image_t *image = test_pattern_image(p);
  ASCIICHAT_REQUIRE_EQ(test_pattern_resize(p, 80, 48), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(test_pattern_image(p), image);
  ASCIICHAT_REQUIRE_NE(test_pattern_render(p, 2, 0, false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(test_pattern_render(p, 0, NAN, false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(test_pattern_render(p, 0, -1, false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(test_pattern_resize(p, 0, 48), ASCIICHAT_OK);
  test_pattern_destroy(p);
  test_pattern_destroy(NULL);
}

TEST_CASE("webcam/test_pattern_circle_wraps_and_cadence_is_opt_in", "[webcam]") {
  test_pattern_t *p = NULL;
  ASCIICHAT_REQUIRE_EQ(test_pattern_create(800, 400, &p), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(test_pattern_render(p, 1, 2000, false), ASCIICHAT_OK);
  image_t *image = test_pattern_image(p);
  rgb_pixel_t center = image->pixels[200 * 800 + 400];
  ASCIICHAT_REQUIRE_EQ(center.r, 255);
  ASCIICHAT_REQUIRE_EQ(center.g, 255);
  ASCIICHAT_REQUIRE_EQ(center.b, 255);
  ASCIICHAT_REQUIRE_EQ(test_pattern_render(p, 1, 0, false), ASCIICHAT_OK);
  rgb_pixel_t edge = image->pixels[200 * 800 + 799];
  ASCIICHAT_REQUIRE(edge.r != 255 || edge.g != 255 || edge.b != 255);
  ASCIICHAT_REQUIRE_EQ(test_pattern_render(p, 1, 0, true), ASCIICHAT_OK);
  for (int i = 0; i < 800 * 400; i++)
    ASCIICHAT_REQUIRE_EQ(image->pixels[i].r, 0);
  test_pattern_destroy(p);
}

TEST_CASE("webcam/palette_round_trip_and_endpoints", "[webcam]") {
  for (int i = 16; i < 256; i++) {
    uint8_t r, g, b, rr, gg, bb;
    get_256color_rgb((uint8_t)i, &r, &g, &b);
    get_256color_rgb(rgb_to_256color(r, g, b), &rr, &gg, &bb);
    ASCIICHAT_REQUIRE_EQ(r, rr);
    ASCIICHAT_REQUIRE_EQ(g, gg);
    ASCIICHAT_REQUIRE_EQ(b, bb);
  }
  ASCIICHAT_REQUIRE_EQ(rgb_to_256color(0, 0, 0), 16);
  ASCIICHAT_REQUIRE_EQ(rgb_to_256color(255, 255, 255), 231);
  for (int i = 0; i < 16; i++) {
    uint8_t r, g, b;
    get_16color_rgb((uint8_t)i, &r, &g, &b);
    ASCIICHAT_REQUIRE_EQ(rgb_to_16color(r, g, b), i);
  }
}
