#include <criterion/criterion.h>
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

TestSuite(render_font, .init = setup, .fini = teardown);

Test(render_font, unsupported_memory_font_returns_error_without_renderer) {
  static const uint8_t invalid_font[] = "This is not a supported font format";
  term_renderer_config_t cfg = {
      .cols = 20, .rows = 10, .font_size_pt = 12, .font_data = invalid_font, .font_data_size = sizeof(invalid_font)};
  terminal_renderer_t *renderer = NULL;
  cr_assert_eq(term_renderer_create(&cfg, &renderer), ERROR_NOT_SUPPORTED);
  cr_assert_null(renderer);
  asciichat_error_context_t error;
  cr_assert(HAS_ERRNO(&error));
  cr_assert_not_null(strstr(error.context_message, "Unsupported font format"));
}

Test(render_font, bundled_font_renders_visible_pixels) {
  term_renderer_config_t cfg = {.cols = 20,
                                .rows = 10,
                                .font_size_pt = 12,
                                .theme = TERM_RENDERER_THEME_DARK,
                                .font_data = g_font_default,
                                .font_data_size = g_font_default_size};
  terminal_renderer_t *renderer = NULL;
  cr_assert_eq(term_renderer_create(&cfg, &renderer), ASCIICHAT_OK);
  cr_assert_eq(term_renderer_feed(renderer, "\033[H", 3), ASCIICHAT_OK);
  const uint8_t *pixels = term_renderer_pixels(renderer);
  cr_assert_not_null(pixels);
  size_t size = (size_t)term_renderer_pitch(renderer) * (size_t)term_renderer_height_px(renderer);
  uint8_t *blank = SAFE_MALLOC(size, uint8_t *);
  memcpy(blank, pixels, size);
  const char *frame = "\033[H\033[37mM";
  cr_assert_eq(term_renderer_feed(renderer, frame, strlen(frame)), ASCIICHAT_OK);
  cr_assert_neq(memcmp(blank, term_renderer_pixels(renderer), size), 0);
  SAFE_FREE(blank);
  term_renderer_destroy(renderer);
}
