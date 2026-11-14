#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/options/colorscheme.h>
#include <ascii-chat/platform/filesystem.h>
#include <libplatform/system.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
TEST_CASE("colorscheme_files/exported_builtin_preserves_colors_and_compiles_both_backgrounds", "[colorscheme_files]") {
  char path[1024];
  int fd;
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(path, sizeof(path), "ascii-colors", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  const char *names[] = {"pastel", "nord", "dracula", "gruvbox", "monokai"};
  for (size_t n = 0; n < sizeof(names) / sizeof(*names); ++n) {
    color_scheme_t original = {0}, loaded = {0};
    ASCIICHAT_REQUIRE_EQ(colorscheme_load_builtin(names[n], &original), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(colorscheme_export_scheme(names[n], path), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(colorscheme_load_from_file(path, &loaded), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_STR_EQ(loaded.name, original.name);
    ASCIICHAT_REQUIRE_NOT(loaded.is_builtin);
    ASCIICHAT_REQUIRE_EQ(memcmp(original.log_colors_dark, loaded.log_colors_dark, sizeof(original.log_colors_dark)), 0);
    ASCIICHAT_REQUIRE_EQ(loaded.has_light_variant, original.has_light_variant);
    if (original.has_light_variant)
      ASCIICHAT_REQUIRE_EQ(memcmp(original.log_colors_light, loaded.log_colors_light, sizeof(original.log_colors_light)), 0);
    compiled_color_scheme_t compiled = {0};
    for (int background = TERM_BACKGROUND_LIGHT; background <= TERM_BACKGROUND_DARK; ++background) {
      ASCIICHAT_REQUIRE_EQ(
          colorscheme_compile_scheme(&loaded, TERM_COLOR_TRUECOLOR, (terminal_background_t)background, &compiled),
          ASCIICHAT_OK);
      const rgb_pixel_t *colors = background == TERM_BACKGROUND_LIGHT && loaded.has_light_variant
                                      ? loaded.log_colors_light
                                      : loaded.log_colors_dark;
      for (int i = 0; i < 8; ++i) {
        ASCIICHAT_REQUIRE_NOT_NULL(strstr(compiled.codes_16[i], "\033["));
        ASCIICHAT_REQUIRE_NOT_NULL(strstr(compiled.codes_256[i], "\033["));
        char expected[32];
        rgb_to_truecolor_ansi(colors[i].r, colors[i].g, colors[i].b, expected, sizeof(expected));
        ASCIICHAT_REQUIRE_STR_EQ(compiled.codes_truecolor[i], i == 7 ? "\033[0m" : expected);
      }
    }
    colorscheme_cleanup_compiled(&compiled);
    ASCIICHAT_REQUIRE_NULL(compiled.codes_16[0]);
    ASCIICHAT_REQUIRE_EQ(colorscheme_set_active_scheme(path), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_STR_EQ(colorscheme_get_active_scheme()->name, loaded.name);
  }
  ASCIICHAT_REQUIRE_EQ(platform_delete_temp_file(path), 0);
}

TEST_CASE("colorscheme_files/custom_file_assigns_named_levels_and_light_variant", "[colorscheme_files]") {
  char path[1024];
  int fd;
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(path, sizeof(path), "ascii-colors", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  FILE *stream = platform_fopen("colors-test", path, "w");
  ASCIICHAT_REQUIRE_NOT_NULL(stream);
  fputs("[scheme]\nname = 'custom'\ndescription = 'Test palette'\n"
        "[colors.dark]\ninfo = '#123456'\nwarn = '#ABCDEF'\n"
        "[colors.light]\ninfo = '#654321'\nwarn = '#FEDCBA'\n",
        stream);
  fclose(stream);
  color_scheme_t scheme = {0};
  ASCIICHAT_REQUIRE_EQ(colorscheme_load_from_file(path, &scheme), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(scheme.name, "custom");
  ASCIICHAT_REQUIRE_EQ(scheme.log_colors_dark[2].r, 0x12);
  ASCIICHAT_REQUIRE_EQ(scheme.log_colors_dark[3].r, 0xAB);
  ASCIICHAT_REQUIRE_EQ(scheme.log_colors_light[2].r, 0x65);
  ASCIICHAT_REQUIRE_EQ(scheme.log_colors_light[3].r, 0xFE);
  ASCIICHAT_REQUIRE_EQ(platform_delete_temp_file(path), 0);
  ASCIICHAT_REQUIRE_EQ(colorscheme_load_from_file(path, &scheme), ERROR_FILE_NOT_FOUND);
  ASCIICHAT_REQUIRE_EQ(colorscheme_load_builtin(NULL, &scheme), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(colorscheme_load_builtin("not-a-scheme", &scheme), ERROR_CONFIG);
  ASCIICHAT_REQUIRE_EQ(colorscheme_compile_scheme(NULL, TERM_COLOR_16, TERM_BACKGROUND_DARK, NULL), ERROR_INVALID_PARAM);
  colorscheme_cleanup_compiled(NULL);
}

TEST_CASE("colorscheme_files/hex_colors_require_six_valid_digits", "[colorscheme_files]") {
  uint8_t r = 0, g = 0, b = 0;
  ASCIICHAT_REQUIRE_EQ(parse_hex_color("#12aBfF", &r, &g, &b), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(r, 0x12);
  ASCIICHAT_REQUIRE_EQ(g, 0xAB);
  ASCIICHAT_REQUIRE_EQ(b, 0xFF);
  const char *invalid[] = {"", "#fff", "1234567", "GGGGGG", "12zzzz"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i)
    ASCIICHAT_REQUIRE_EQ(parse_hex_color(invalid[i], &r, &g, &b), ERROR_CONFIG);
  ASCIICHAT_REQUIRE_EQ(parse_hex_color(NULL, &r, &g, &b), ERROR_INVALID_PARAM);
}

TEST_CASE("colorscheme_files/live_background_palette_uses_custom_logging_colors_at_every_depth", "[colorscheme_files]") {
  color_scheme_t previous = *colorscheme_get_active_scheme();
  color_scheme_t custom = previous;
  custom.log_colors_dark[2] = custom.log_colors_light[2] = (rgb_pixel_t){1, 240, 3};
  custom.log_colors_dark[4] = custom.log_colors_light[4] = (rgb_pixel_t){240, 1, 3};
  colorscheme_apply(&custom);
  colorscheme_palette_t palette;
  colorscheme_current_palette(&palette, TERM_COLOR_TRUECOLOR);
  ASCIICHAT_CHECK_STR_EQ(palette.background_codes[2], "\033[48;2;1;240;3m");
  ASCIICHAT_CHECK_STR_EQ(palette.background_codes[4], "\033[48;2;240;1;3m");
  colorscheme_current_palette(&palette, TERM_COLOR_256);
  ASCIICHAT_CHECK_STR_EQ(palette.background_codes[2], "\033[48;5;46m");
  ASCIICHAT_CHECK_STR_EQ(palette.background_codes[4], "\033[48;5;196m");
  colorscheme_current_palette(&palette, TERM_COLOR_16);
  ASCIICHAT_CHECK_STR_EQ(palette.background_codes[2], "\033[102m");
  ASCIICHAT_CHECK_STR_EQ(palette.background_codes[4], "\033[101m");
  colorscheme_current_palette(&palette, TERM_COLOR_NONE);
  ASCIICHAT_CHECK_STR_EQ(palette.background_codes[2], "");
  colorscheme_apply(&previous);
}
