#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/common.h>
#include <ascii-chat/font.h>
#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/options/rcu.h>
#include <string.h>

static void setup(void) {
  options_state_init();
}

static void teardown(void) {
  errno_clear();
  options_state_destroy();
}
TEST_CASE("render_font/invalid_memory_font_returns_error_without_renderer", "[render_font]") {
  static const uint8_t invalid_font[] = "This is not a supported font format";
  term_renderer_config_t cfg = {
      .cols = 20, .rows = 10, .font_size_pt = 12, .font_data = invalid_font, .font_data_size = sizeof(invalid_font)};
  terminal_renderer_t *renderer = NULL;
  ASCIICHAT_REQUIRE_NE(term_renderer_create(&cfg, &renderer), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NULL(renderer);
  asciichat_error_context_t error;
  ASCIICHAT_REQUIRE(HAS_ERRNO(&error));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(error.context_message, "font"));
}

TEST_CASE("render_font/bundled_font_renders_visible_pixels", "[render_font]") {
  term_renderer_config_t cfg = {.cols = 20,
                                .rows = 10,
                                .font_size_pt = 12,
                                .theme = TERM_RENDERER_THEME_DARK,
                                .font_data = g_font_default,
                                .font_data_size = g_font_default_size};
  terminal_renderer_t *renderer = NULL;
  ASCIICHAT_REQUIRE_EQ(term_renderer_create(&cfg, &renderer), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(term_renderer_feed(renderer, "\033[H", 3), ASCIICHAT_OK);
  const uint8_t *pixels = term_renderer_pixels(renderer);
  ASCIICHAT_REQUIRE_NOT_NULL(pixels);
  size_t size = (size_t)term_renderer_pitch(renderer) * (size_t)term_renderer_height_px(renderer);
  uint8_t *blank = SAFE_MALLOC(size, uint8_t *);
  memcpy(blank, pixels, size);
  const char *frame = "\033[H\033[37mM";
  ASCIICHAT_REQUIRE_EQ(term_renderer_feed(renderer, frame, strlen(frame)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(memcmp(blank, term_renderer_pixels(renderer), size), 0);
  SAFE_FREE(blank);
  term_renderer_destroy(renderer);
}
