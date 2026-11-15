#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/media/source.h>
#include <ascii-chat/video/anim/test_pattern.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <string.h>
#include <math.h>
TEST_CASE("webcam/test_pattern_deterministic_and_independent", "[webcam]") {
  test_pattern_t *a = NULL, *b = NULL;
  CLEAR_ERRNO();
  REQUIRE((test_pattern_create(320, 240, &a)) == (ASCIICHAT_OK));
  REQUIRE((GET_ERRNO()) == (ASCIICHAT_OK));
  REQUIRE((test_pattern_create(320, 240, &b)) == (ASCIICHAT_OK));
  for (int index = 0; index < 2; index++) {
    REQUIRE((test_pattern_render(a, index, 2000, false)) == (ASCIICHAT_OK));
    REQUIRE((test_pattern_render(b, index, 2000, false)) == (ASCIICHAT_OK));
    image_t *ia = test_pattern_image(a), *ib = test_pattern_image(b);
    REQUIRE((ia->pixels) != (ib->pixels));
    REQUIRE((memcmp(ia->pixels, ib->pixels, 320 * 240 * sizeof(rgb_pixel_t))) == (0));
    REQUIRE((test_pattern_render(b, index, 2500, false)) == (ASCIICHAT_OK));
    REQUIRE((memcmp(ia->pixels, ib->pixels, 320 * 240 * sizeof(rgb_pixel_t))) != (0));
  }
  test_pattern_destroy(a);
  test_pattern_destroy(b);
}

TEST_CASE("webcam/test_pattern_resize_and_invalid_input", "[webcam]") {
  test_pattern_t *p = NULL;
  REQUIRE((test_pattern_create(1, 1, &p)) == (ASCIICHAT_OK));
  for (int index = 0; index < 2; index++)
    REQUIRE((test_pattern_render(p, index, 0, false)) == (ASCIICHAT_OK));
  REQUIRE((test_pattern_resize(p, 80, 48)) == (ASCIICHAT_OK));
  image_t *image = test_pattern_image(p);
  REQUIRE((test_pattern_resize(p, 80, 48)) == (ASCIICHAT_OK));
  REQUIRE((test_pattern_image(p)) == (image));
  REQUIRE((test_pattern_render(p, 2, 0, false)) != (ASCIICHAT_OK));
  REQUIRE((test_pattern_render(p, 0, NAN, false)) != (ASCIICHAT_OK));
  REQUIRE((test_pattern_render(p, 0, -1, false)) != (ASCIICHAT_OK));
  REQUIRE((test_pattern_resize(p, 0, 48)) != (ASCIICHAT_OK));
  test_pattern_destroy(p);
  test_pattern_destroy(NULL);
}

TEST_CASE("webcam/test_pattern_circle_wraps_and_cadence_is_opt_in", "[webcam]") {
  test_pattern_t *p = NULL;
  REQUIRE((test_pattern_create(800, 400, &p)) == (ASCIICHAT_OK));
  REQUIRE((test_pattern_render(p, 1, 2000, false)) == (ASCIICHAT_OK));
  image_t *image = test_pattern_image(p);
  rgb_pixel_t center = image->pixels[200 * 800 + 400];
  REQUIRE((center.r) == (255));
  REQUIRE((center.g) == (255));
  REQUIRE((center.b) == (255));
  REQUIRE((test_pattern_render(p, 1, 0, false)) == (ASCIICHAT_OK));
  rgb_pixel_t edge = image->pixels[200 * 800 + 799];
  REQUIRE((edge.r != 255 || edge.g != 255 || edge.b != 255));
  REQUIRE((test_pattern_render(p, 1, 0, true)) == (ASCIICHAT_OK));
  for (int i = 0; i < 800 * 400; i++)
    REQUIRE((image->pixels[i].r) == (0));
  test_pattern_destroy(p);
}

TEST_CASE("webcam/palette_round_trip_and_endpoints", "[webcam]") {
  for (int i = 16; i < 256; i++) {
    uint8_t r, g, b, rr, gg, bb;
    get_256color_rgb((uint8_t)i, &r, &g, &b);
    get_256color_rgb(rgb_to_256color(r, g, b), &rr, &gg, &bb);
    REQUIRE((r) == (rr));
    REQUIRE((g) == (gg));
    REQUIRE((b) == (bb));
  }
  REQUIRE((rgb_to_256color(0, 0, 0)) == (16));
  REQUIRE((rgb_to_256color(255, 255, 255)) == (231));
  for (int i = 0; i < 16; i++) {
    uint8_t r, g, b;
    get_16color_rgb((uint8_t)i, &r, &g, &b);
    REQUIRE((rgb_to_16color(r, g, b)) == (i));
  }
}
