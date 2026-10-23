#include <criterion/criterion.h>
#include <ascii-chat/options/colorscheme.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

TestSuite(colorscheme_files);

Test(colorscheme_files, exported_builtin_preserves_colors_and_compiles_both_backgrounds) {
  char path[1024];
  int fd;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "ascii-colors", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  const char *names[] = {"pastel", "nord", "dracula", "gruvbox", "monokai"};
  for (size_t n = 0; n < sizeof(names) / sizeof(*names); ++n) {
    color_scheme_t original = {0}, loaded = {0};
    cr_assert_eq(colorscheme_load_builtin(names[n], &original), ASCIICHAT_OK);
    cr_assert_eq(colorscheme_export_scheme(names[n], path), ASCIICHAT_OK);
    cr_assert_eq(colorscheme_load_from_file(path, &loaded), ASCIICHAT_OK);
    cr_assert_str_eq(loaded.name, original.name);
    cr_assert_not(loaded.is_builtin);
    cr_assert_eq(memcmp(original.log_colors_dark, loaded.log_colors_dark, sizeof(original.log_colors_dark)), 0);
    cr_assert_eq(loaded.has_light_variant, original.has_light_variant);
    if (original.has_light_variant)
      cr_assert_eq(memcmp(original.log_colors_light, loaded.log_colors_light, sizeof(original.log_colors_light)), 0);
    compiled_color_scheme_t compiled = {0};
    for (int background = TERM_BACKGROUND_LIGHT; background <= TERM_BACKGROUND_DARK; ++background) {
      cr_assert_eq(
          colorscheme_compile_scheme(&loaded, TERM_COLOR_TRUECOLOR, (terminal_background_t)background, &compiled),
          ASCIICHAT_OK);
      const rgb_pixel_t *colors = background == TERM_BACKGROUND_LIGHT && loaded.has_light_variant
                                      ? loaded.log_colors_light
                                      : loaded.log_colors_dark;
      for (int i = 0; i < 8; ++i) {
        cr_assert_not_null(strstr(compiled.codes_16[i], "\033["));
        cr_assert_not_null(strstr(compiled.codes_256[i], "\033["));
        char expected[32];
        rgb_to_truecolor_ansi(colors[i].r, colors[i].g, colors[i].b, expected, sizeof(expected));
        cr_assert_str_eq(compiled.codes_truecolor[i], i == 7 ? "\033[0m" : expected);
      }
    }
    colorscheme_cleanup_compiled(&compiled);
    cr_assert_null(compiled.codes_16[0]);
    cr_assert_eq(colorscheme_set_active_scheme(path), ASCIICHAT_OK);
    cr_assert_str_eq(colorscheme_get_active_scheme()->name, loaded.name);
  }
  cr_assert_eq(platform_delete_temp_file(path), 0);
}

Test(colorscheme_files, custom_file_assigns_named_levels_and_light_variant) {
  char path[1024];
  int fd;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "ascii-colors", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  FILE *stream = platform_fopen("colors-test", path, "w");
  cr_assert_not_null(stream);
  fputs("[scheme]\nname = 'custom'\ndescription = 'Test palette'\n"
        "[colors.dark]\ninfo = '#123456'\nwarn = '#ABCDEF'\n"
        "[colors.light]\ninfo = '#654321'\nwarn = '#FEDCBA'\n",
        stream);
  fclose(stream);
  color_scheme_t scheme = {0};
  cr_assert_eq(colorscheme_load_from_file(path, &scheme), ASCIICHAT_OK);
  cr_assert_str_eq(scheme.name, "custom");
  cr_assert_eq(scheme.log_colors_dark[2].r, 0x12);
  cr_assert_eq(scheme.log_colors_dark[3].r, 0xAB);
  cr_assert_eq(scheme.log_colors_light[2].r, 0x65);
  cr_assert_eq(scheme.log_colors_light[3].r, 0xFE);
  cr_assert_eq(platform_delete_temp_file(path), 0);
  cr_assert_eq(colorscheme_load_from_file(path, &scheme), ERROR_FILE_NOT_FOUND);
  cr_assert_eq(colorscheme_load_builtin(NULL, &scheme), ERROR_INVALID_PARAM);
  cr_assert_eq(colorscheme_load_builtin("not-a-scheme", &scheme), ERROR_CONFIG);
  cr_assert_eq(colorscheme_compile_scheme(NULL, TERM_COLOR_16, TERM_BACKGROUND_DARK, NULL), ERROR_INVALID_PARAM);
  colorscheme_cleanup_compiled(NULL);
}

Test(colorscheme_files, hex_colors_require_six_valid_digits) {
  uint8_t r = 0, g = 0, b = 0;
  cr_assert_eq(parse_hex_color("#12aBfF", &r, &g, &b), ASCIICHAT_OK);
  cr_assert_eq(r, 0x12);
  cr_assert_eq(g, 0xAB);
  cr_assert_eq(b, 0xFF);
  const char *invalid[] = {"", "#fff", "1234567", "GGGGGG", "12zzzz"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i)
    cr_assert_eq(parse_hex_color(invalid[i], &r, &g, &b), ERROR_CONFIG);
  cr_assert_eq(parse_hex_color(NULL, &r, &g, &b), ERROR_INVALID_PARAM);
}

Test(colorscheme_files, live_background_palette_uses_custom_logging_colors_at_every_depth) {
  color_scheme_t previous = *colorscheme_get_active_scheme();
  color_scheme_t custom = previous;
  custom.log_colors_dark[2] = custom.log_colors_light[2] = (rgb_pixel_t){1, 240, 3};
  custom.log_colors_dark[4] = custom.log_colors_light[4] = (rgb_pixel_t){240, 1, 3};
  colorscheme_apply(&custom);
  colorscheme_palette_t palette;
  colorscheme_current_palette(&palette, TERM_COLOR_TRUECOLOR);
  cr_expect_str_eq(palette.background_codes[2], "\033[48;2;1;240;3m");
  cr_expect_str_eq(palette.background_codes[4], "\033[48;2;240;1;3m");
  colorscheme_current_palette(&palette, TERM_COLOR_256);
  cr_expect_str_eq(palette.background_codes[2], "\033[48;5;46m");
  cr_expect_str_eq(palette.background_codes[4], "\033[48;5;196m");
  colorscheme_current_palette(&palette, TERM_COLOR_16);
  cr_expect_str_eq(palette.background_codes[2], "\033[102m");
  cr_expect_str_eq(palette.background_codes[4], "\033[101m");
  colorscheme_current_palette(&palette, TERM_COLOR_NONE);
  cr_expect_str_eq(palette.background_codes[2], "");
  colorscheme_apply(&previous);
}
