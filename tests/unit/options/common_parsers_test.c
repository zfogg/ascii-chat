#include <criterion/criterion.h>
#include <ascii-chat/options/common.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

TestSuite(common_parsers);

Test(common_parsers, display_aliases_and_rejected_values_preserve_previous_settings) {
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  const struct {
    const char *text;
    int mode;
  } colors[] = {{"auto", COLOR_MODE_AUTO},       {"a", COLOR_MODE_AUTO},
                {"mono", COLOR_MODE_NONE},       {"none", COLOR_MODE_NONE},
                {"ansi", COLOR_MODE_16_COLOR},   {"16color", COLOR_MODE_16_COLOR},
                {"256", COLOR_MODE_256_COLOR},   {"256color", COLOR_MODE_256_COLOR},
                {"24bit", COLOR_MODE_TRUECOLOR}, {"tc", COLOR_MODE_TRUECOLOR},
                {"rgb", COLOR_MODE_TRUECOLOR},   {"true", COLOR_MODE_TRUECOLOR}};
  for (size_t i = 0; i < sizeof(colors) / sizeof(*colors); ++i) {
    cr_assert_eq(parse_color_mode_option(colors[i].text, options), ASCIICHAT_OK);
    cr_assert_eq(options->color_mode, colors[i].mode);
  }
  cr_assert_eq(parse_color_mode_option("bad", options), ERROR_INVALID_PARAM);
  cr_assert_eq(options->color_mode, COLOR_MODE_TRUECOLOR);
  const char *renders[] = {"fg", "foreground", "bg", "background", "half-block", "halfblock"};
  for (size_t i = 0; i < 6; ++i) {
    cr_assert_eq(parse_render_mode_option(renders[i], options), ASCIICHAT_OK);
    cr_assert_eq(options->render_mode, i < 2   ? RENDER_MODE_FOREGROUND
                                       : i < 4 ? RENDER_MODE_BACKGROUND
                                               : RENDER_MODE_HALF_BLOCK);
  }
  cr_assert_eq(parse_render_mode_option("bad", options), ERROR_INVALID_PARAM);
  cr_assert_eq(options->render_mode, RENDER_MODE_HALF_BLOCK);
  const char *palettes[] = {"standard", "blocks", "digital", "minimal", "cool", "custom"};
  for (size_t i = 0; i < 6; ++i)
    cr_assert_eq(parse_palette_option(palettes[i], options), ASCIICHAT_OK);
  cr_assert_eq(parse_palette_chars_option(" .+#", options), ASCIICHAT_OK);
  cr_assert(options->palette_custom_set);
  cr_assert_str_eq(options->palette_custom, " .+#");
  char excessive[sizeof(options->palette_custom) + 1];
  memset(excessive, 'X', sizeof(excessive) - 1);
  excessive[sizeof(excessive) - 1] = 0;
  cr_assert_eq(parse_palette_chars_option(excessive, options), ERROR_INVALID_PARAM);
  cr_assert_str_eq(options->palette_custom, " .+#");
  cr_assert_eq(parse_palette_option("bad", options), ERROR_INVALID_PARAM);
  cr_assert_eq(parse_color_mode_option(NULL, options), ERROR_INVALID_PARAM);
  cr_assert_eq(parse_render_mode_option(NULL, options), ERROR_INVALID_PARAM);
  cr_assert_eq(parse_palette_option(NULL, options), ERROR_INVALID_PARAM);
  cr_assert_eq(parse_palette_chars_option(NULL, options), ERROR_INVALID_PARAM);
  SAFE_FREE(options);
}

Test(common_parsers, numeric_options_enforce_ranges_and_do_not_commit_rejected_values) {
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  cr_assert_eq(parse_width_option("80", options), ASCIICHAT_OK);
  cr_assert_eq(parse_height_option("25", options), ASCIICHAT_OK);
  cr_assert_not(options->auto_width);
  cr_assert_not(options->auto_height);
  cr_assert_eq(parse_width_option("0", options), ERROR_INVALID_PARAM);
  cr_assert_eq(parse_height_option("-1", options), ERROR_INVALID_PARAM);
  cr_assert_eq(options->width, 80);
  cr_assert_eq(options->height, 25);
  cr_assert_eq(parse_webcam_index_option("0", options), ASCIICHAT_OK);
  cr_assert_eq(parse_webcam_index_option("-1", options), ERROR_INVALID_PARAM);
  cr_assert_eq(parse_snapshot_delay_option("1.25", options), ASCIICHAT_OK);
  cr_assert_float_eq(options->snapshot_delay, 1.25, 0.0001);
  cr_assert_eq(parse_snapshot_delay_option("1.5x", options), ERROR_INVALID_PARAM);
  cr_assert_eq(parse_snapshot_delay_option("-1", options), ERROR_INVALID_PARAM);
  cr_assert_float_eq(options->snapshot_delay, 1.25, 0.0001);
  cr_assert_eq(parse_log_level_option("debug", options), ASCIICHAT_OK);
  cr_assert_eq(parse_log_level_option("unknown", options), ERROR_INVALID_PARAM);
  uint16_t port = 17;
  cr_assert(validate_port_opt("65535", &port));
  cr_assert_eq(port, 65535);
  cr_assert_not(validate_port_opt("65536", &port));
  cr_assert_eq(port, 65535);
  int fps = 10;
  cr_assert(validate_fps_opt("30", &fps));
  cr_assert_not(validate_fps_opt("0", &fps));
  cr_assert_eq(fps, 30);
  SAFE_FREE(options);
}

Test(common_parsers, suggestions_explain_mode_restrictions_and_reject_unrelated_names) {
  option_descriptor_t descriptors[] = {{.long_name = "width", .mode_bitmask = OPTION_MODE_CLIENT | OPTION_MODE_MIRROR},
                                       {.long_name = "max-clients", .mode_bitmask = OPTION_MODE_SERVER}};
  options_config_t config = {.descriptors = descriptors, .num_descriptors = 2};
  const char *suggestion = find_similar_option_with_mode("--widht", &config, OPTION_MODE_CLIENT);
  cr_assert_not_null(suggestion);
  cr_assert_not_null(strstr(suggestion, "--width"));
  suggestion = find_similar_option_with_mode("--max-client", &config, OPTION_MODE_CLIENT);
  cr_assert_not_null(strstr(suggestion, "server"));
  cr_assert_null(find_similar_option_with_mode("not-an-option", &config, OPTION_MODE_CLIENT));
  cr_assert_null(find_similar_option_with_mode("--unrelated-unknown-flag", &config, OPTION_MODE_CLIENT));
  cr_assert_str_eq(format_available_modes(OPTION_MODE_BINARY), "global options");
  cr_assert_str_eq(format_available_modes(0), "unknown mode");
  const char *all = format_available_modes(OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_MIRROR |
                                           OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC);
  cr_assert_not_null(strstr(all, "default"));
  cr_assert_not_null(strstr(all, "discovery-service"));
}
