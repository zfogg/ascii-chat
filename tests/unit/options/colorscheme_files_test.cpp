#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/options/colorscheme.h>
#include <libplatform/filesystem.h>
#include <libplatform/system.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
TEST_CASE("colorscheme_files/exported_builtin_preserves_colors_and_compiles_both_backgrounds", "[colorscheme_files]") {
  char path[1024];
  int fd;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "ascii-colors", &fd)) == (0));
  if (fd >= 0)
    platform_close(fd);
  const char *names[] = {"pastel", "nord", "dracula", "gruvbox", "monokai"};
  for (size_t n = 0; n < sizeof(names) / sizeof(*names); ++n) {
    color_scheme_t original = {0}, loaded = {0};
    REQUIRE((colorscheme_load_builtin(names[n], &original)) == (ASCIICHAT_OK));
    REQUIRE((colorscheme_export_scheme(names[n], path)) == (ASCIICHAT_OK));
    REQUIRE((colorscheme_load_from_file(path, &loaded)) == (ASCIICHAT_OK));
    REQUIRE(std::strcmp((loaded.name), (original.name)) == 0);
    REQUIRE(!(loaded.is_builtin));
    REQUIRE((memcmp(original.log_colors_dark, loaded.log_colors_dark, sizeof(original.log_colors_dark))) == (0));
    REQUIRE((loaded.has_light_variant) == (original.has_light_variant));
    if (original.has_light_variant)
      REQUIRE((memcmp(original.log_colors_light, loaded.log_colors_light, sizeof(original.log_colors_light))) == (0));
    compiled_color_scheme_t compiled = {0};
    for (int background = TERM_BACKGROUND_LIGHT; background <= TERM_BACKGROUND_DARK; ++background) {
      REQUIRE((colorscheme_compile_scheme(&loaded, TERM_COLOR_TRUECOLOR, (terminal_background_t)background, &compiled)) == (ASCIICHAT_OK));
      const rgb_pixel_t *colors = background == TERM_BACKGROUND_LIGHT && loaded.has_light_variant ? loaded.log_colors_light : loaded.log_colors_dark;
      for (int i = 0; i < 8; ++i) {
        REQUIRE((strstr(compiled.codes_16[i], "\033[")) != nullptr);
        REQUIRE((strstr(compiled.codes_256[i], "\033[")) != nullptr);
        char expected[32];
        rgb_to_truecolor_ansi(colors[i].r, colors[i].g, colors[i].b, expected, sizeof(expected));
        REQUIRE(std::strcmp((compiled.codes_truecolor[i]), (i == 7 ? "\033[0m" : expected)) == 0);
      }
    }
    colorscheme_cleanup_compiled(&compiled);
    REQUIRE((compiled.codes_16[0]) == nullptr);
    REQUIRE((colorscheme_set_active_scheme(path)) == (ASCIICHAT_OK));
    REQUIRE(std::strcmp((colorscheme_get_active_scheme()->name), (loaded.name)) == 0);
  }
  REQUIRE((platform_delete_temp_file(path)) == (0));
}

TEST_CASE("colorscheme_files/custom_file_assigns_named_levels_and_light_variant", "[colorscheme_files]") {
  char path[1024];
  int fd;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "ascii-colors", &fd)) == (0));
  if (fd >= 0)
    platform_close(fd);
  FILE *stream = platform_fopen("colors-test", path, "w");
  REQUIRE((stream) != nullptr);
  fputs("[scheme]\nname = 'custom'\ndescription = 'Test palette'\n"
        "[colors.dark]\ninfo = '#123456'\nwarn = '#ABCDEF'\n"
        "[colors.light]\ninfo = '#654321'\nwarn = '#FEDCBA'\n",
        stream);
  fclose(stream);
  color_scheme_t scheme = {0};
  REQUIRE((colorscheme_load_from_file(path, &scheme)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((scheme.name), ("custom")) == 0);
  REQUIRE((scheme.log_colors_dark[2].r) == (0x12));
  REQUIRE((scheme.log_colors_dark[3].r) == (0xAB));
  REQUIRE((scheme.log_colors_light[2].r) == (0x65));
  REQUIRE((scheme.log_colors_light[3].r) == (0xFE));
  REQUIRE((platform_delete_temp_file(path)) == (0));
  REQUIRE((colorscheme_load_from_file(path, &scheme)) == (ERROR_FILE_NOT_FOUND));
  REQUIRE((colorscheme_load_builtin(NULL, &scheme)) == (ERROR_INVALID_PARAM));
  REQUIRE((colorscheme_load_builtin("not-a-scheme", &scheme)) == (ERROR_CONFIG));
  REQUIRE((colorscheme_compile_scheme(NULL, TERM_COLOR_16, TERM_BACKGROUND_DARK, NULL)) == (ERROR_INVALID_PARAM));
  colorscheme_cleanup_compiled(NULL);
}

TEST_CASE("colorscheme_files/hex_colors_require_six_valid_digits", "[colorscheme_files]") {
  uint8_t r = 0, g = 0, b = 0;
  REQUIRE((parse_hex_color("#12aBfF", &r, &g, &b)) == (ASCIICHAT_OK));
  REQUIRE((r) == (0x12));
  REQUIRE((g) == (0xAB));
  REQUIRE((b) == (0xFF));
  const char *invalid[] = {"", "#fff", "1234567", "GGGGGG", "12zzzz"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i)
    REQUIRE((parse_hex_color(invalid[i], &r, &g, &b)) == (ERROR_CONFIG));
  REQUIRE((parse_hex_color(NULL, &r, &g, &b)) == (ERROR_INVALID_PARAM));
}

TEST_CASE("colorscheme_files/live_background_palette_uses_custom_logging_colors_at_every_depth", "[colorscheme_files]") {
  color_scheme_t previous = *colorscheme_get_active_scheme();
  color_scheme_t custom = previous;
  custom.log_colors_dark[2] = custom.log_colors_light[2] = (rgb_pixel_t){1, 240, 3};
  custom.log_colors_dark[4] = custom.log_colors_light[4] = (rgb_pixel_t){240, 1, 3};
  colorscheme_apply(&custom);
  colorscheme_palette_t palette;
  colorscheme_current_palette(&palette, TERM_COLOR_TRUECOLOR);
  CHECK(std::strcmp((palette.background_codes[2]), ("\033[48;2;1;240;3m")) == 0);
  CHECK(std::strcmp((palette.background_codes[4]), ("\033[48;2;240;1;3m")) == 0);
  colorscheme_current_palette(&palette, TERM_COLOR_256);
  CHECK(std::strcmp((palette.background_codes[2]), ("\033[48;5;46m")) == 0);
  CHECK(std::strcmp((palette.background_codes[4]), ("\033[48;5;196m")) == 0);
  colorscheme_current_palette(&palette, TERM_COLOR_16);
  CHECK(std::strcmp((palette.background_codes[2]), ("\033[102m")) == 0);
  CHECK(std::strcmp((palette.background_codes[4]), ("\033[101m")) == 0);
  colorscheme_current_palette(&palette, TERM_COLOR_NONE);
  CHECK(std::strcmp((palette.background_codes[2]), ("")) == 0);
  colorscheme_apply(&previous);
}
