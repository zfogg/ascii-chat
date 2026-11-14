#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/options/common.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/levenshtein.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
TEST_CASE("common_parsers/levenshtein_handles_utf8_and_invalid_inputs", "[common_parsers]") {
  ASCIICHAT_REQUIRE_EQ(levenshtein("kitten", "sitting"), 3);
  ASCIICHAT_REQUIRE_EQ(levenshtein("café", "cafe"), 1);
  ASCIICHAT_REQUIRE_EQ(levenshtein("界é", "界"), 1);
  ASCIICHAT_REQUIRE_EQ(levenshtein("", "abc"), 3);
  ASCIICHAT_REQUIRE_EQ(levenshtein(NULL, "abc"), SIZE_MAX);
  ASCIICHAT_REQUIRE_EQ(levenshtein("\xff", "abc"), SIZE_MAX);
}

TEST_CASE("common_parsers/display_aliases_and_rejected_values_preserve_previous_settings", "[common_parsers]") {
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
    ASCIICHAT_REQUIRE_EQ(parse_color_mode_option(colors[i].text, options), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(options->color_mode, colors[i].mode);
  }
  ASCIICHAT_REQUIRE_EQ(parse_color_mode_option("bad", options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(options->color_mode, COLOR_MODE_TRUECOLOR);
  const char *renders[] = {"fg", "foreground", "bg", "background", "half-block", "halfblock"};
  for (size_t i = 0; i < 6; ++i) {
    ASCIICHAT_REQUIRE_EQ(parse_render_mode_option(renders[i], options), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(options->render_mode, i < 2   ? RENDER_MODE_FOREGROUND
                                       : i < 4 ? RENDER_MODE_BACKGROUND
                                               : RENDER_MODE_HALF_BLOCK);
  }
  ASCIICHAT_REQUIRE_EQ(parse_render_mode_option("bad", options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(options->render_mode, RENDER_MODE_HALF_BLOCK);
  const char *palettes[] = {"standard", "blocks", "digital", "minimal", "cool", "custom"};
  for (size_t i = 0; i < 6; ++i)
    ASCIICHAT_REQUIRE_EQ(parse_palette_option(palettes[i], options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(parse_palette_chars_option(" .+#", options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(options->palette_custom_set);
  ASCIICHAT_REQUIRE_STR_EQ(options->palette_custom, " .+#");
  char excessive[sizeof(options->palette_custom) + 1];
  memset(excessive, 'X', sizeof(excessive) - 1);
  excessive[sizeof(excessive) - 1] = 0;
  ASCIICHAT_REQUIRE_EQ(parse_palette_chars_option(excessive, options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_STR_EQ(options->palette_custom, " .+#");
  ASCIICHAT_REQUIRE_EQ(parse_palette_option("bad", options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(parse_color_mode_option(NULL, options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(parse_render_mode_option(NULL, options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(parse_palette_option(NULL, options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(parse_palette_chars_option(NULL, options), ERROR_INVALID_PARAM);
  SAFE_FREE(options);
}

TEST_CASE("common_parsers/numeric_options_enforce_ranges_and_do_not_commit_rejected_values", "[common_parsers]") {
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  ASCIICHAT_REQUIRE_EQ(parse_width_option("80", options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(parse_height_option("25", options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(options->auto_width);
  ASCIICHAT_REQUIRE_NOT(options->auto_height);
  ASCIICHAT_REQUIRE_EQ(parse_width_option("0", options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(parse_height_option("-1", options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(options->width, 80);
  ASCIICHAT_REQUIRE_EQ(options->height, 25);
  ASCIICHAT_REQUIRE_EQ(parse_webcam_index_option("0", options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(parse_webcam_index_option("-1", options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(parse_snapshot_delay_option("1.25", options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_FLOAT_EQ(options->snapshot_delay, 1.25, 0.0001);
  ASCIICHAT_REQUIRE_EQ(parse_snapshot_delay_option("1.5x", options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(parse_snapshot_delay_option("-1", options), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_FLOAT_EQ(options->snapshot_delay, 1.25, 0.0001);
  ASCIICHAT_REQUIRE_EQ(parse_log_level_option("debug", options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(parse_log_level_option("unknown", options), ERROR_INVALID_PARAM);
  uint16_t port = 17;
  ASCIICHAT_REQUIRE(validate_port_opt("65535", &port));
  ASCIICHAT_REQUIRE_EQ(port, 65535);
  ASCIICHAT_REQUIRE_NOT(validate_port_opt("65536", &port));
  ASCIICHAT_REQUIRE_EQ(port, 65535);
  int fps = 10;
  ASCIICHAT_REQUIRE(validate_fps_opt("30", &fps));
  ASCIICHAT_REQUIRE(validate_fps_opt("0", &fps));
  ASCIICHAT_REQUIRE_EQ(fps, 0);
  ASCIICHAT_REQUIRE_NOT(validate_fps_opt("-1", &fps));
  ASCIICHAT_REQUIRE_EQ(fps, 0);
  SAFE_FREE(options);
}

TEST_CASE("common_parsers/suggestions_explain_mode_restrictions_and_reject_unrelated_names", "[common_parsers]") {
  option_descriptor_t descriptors[] = {{.long_name = "width", .mode_bitmask = static_cast<option_mode_bitmask_t>(OPTION_MODE_CLIENT | OPTION_MODE_MIRROR)},
                                       {.long_name = "max-clients", .mode_bitmask = static_cast<option_mode_bitmask_t>(OPTION_MODE_SERVER)}};
  options_config_t config = {.descriptors = descriptors, .num_descriptors = 2};
  const char *suggestion = find_similar_option_with_mode("--widht", &config, OPTION_MODE_CLIENT);
  ASCIICHAT_REQUIRE_NOT_NULL(suggestion);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(suggestion, "--width"));
  suggestion = find_similar_option_with_mode("--max-client", &config, OPTION_MODE_CLIENT);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(suggestion, "server"));
  ASCIICHAT_REQUIRE_NULL(find_similar_option_with_mode("not-an-option", &config, OPTION_MODE_CLIENT));
  ASCIICHAT_REQUIRE_NULL(find_similar_option_with_mode("--unrelated-unknown-flag", &config, OPTION_MODE_CLIENT));
  ASCIICHAT_REQUIRE_STR_EQ(format_available_modes(OPTION_MODE_BINARY), "global options");
  ASCIICHAT_REQUIRE_STR_EQ(format_available_modes(static_cast<option_mode_bitmask_t>(0)), "unknown mode");
  const char *all = format_available_modes(static_cast<option_mode_bitmask_t>(OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_MIRROR |
                                           OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(all, "default"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(all, "discovery-service"));
}
