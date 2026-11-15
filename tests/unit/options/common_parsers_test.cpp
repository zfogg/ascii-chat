#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/options/common.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/levenshtein.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
TEST_CASE("common_parsers/levenshtein_handles_utf8_and_invalid_inputs", "[common_parsers]") {
  REQUIRE((levenshtein("kitten", "sitting")) == (3));
  REQUIRE((levenshtein("café", "cafe")) == (1));
  REQUIRE((levenshtein("界é", "界")) == (1));
  REQUIRE((levenshtein("", "abc")) == (3));
  REQUIRE((levenshtein(NULL, "abc")) == (SIZE_MAX));
  REQUIRE((levenshtein("\xff", "abc")) == (SIZE_MAX));
}

TEST_CASE("common_parsers/display_aliases_and_rejected_values_preserve_previous_settings", "[common_parsers]") {
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  const struct {
    const char *text;
    int mode;
  } colors[] = {{"auto", COLOR_MODE_AUTO},       {"a", COLOR_MODE_AUTO},           {"mono", COLOR_MODE_NONE},     {"none", COLOR_MODE_NONE},
                {"ansi", COLOR_MODE_16_COLOR},   {"16color", COLOR_MODE_16_COLOR}, {"256", COLOR_MODE_256_COLOR}, {"256color", COLOR_MODE_256_COLOR},
                {"24bit", COLOR_MODE_TRUECOLOR}, {"tc", COLOR_MODE_TRUECOLOR},     {"rgb", COLOR_MODE_TRUECOLOR}, {"true", COLOR_MODE_TRUECOLOR}};
  for (size_t i = 0; i < sizeof(colors) / sizeof(*colors); ++i) {
    REQUIRE((parse_color_mode_option(colors[i].text, options)) == (ASCIICHAT_OK));
    REQUIRE((options->color_mode) == (colors[i].mode));
  }
  REQUIRE((parse_color_mode_option("bad", options)) == (ERROR_INVALID_PARAM));
  REQUIRE((options->color_mode) == (COLOR_MODE_TRUECOLOR));
  const char *renders[] = {"fg", "foreground", "bg", "background", "half-block", "halfblock"};
  for (size_t i = 0; i < 6; ++i) {
    REQUIRE((parse_render_mode_option(renders[i], options)) == (ASCIICHAT_OK));
    REQUIRE((options->render_mode) == (i < 2 ? RENDER_MODE_FOREGROUND : i < 4 ? RENDER_MODE_BACKGROUND : RENDER_MODE_HALF_BLOCK));
  }
  REQUIRE((parse_render_mode_option("bad", options)) == (ERROR_INVALID_PARAM));
  REQUIRE((options->render_mode) == (RENDER_MODE_HALF_BLOCK));
  const char *palettes[] = {"standard", "blocks", "digital", "minimal", "cool", "custom"};
  for (size_t i = 0; i < 6; ++i)
    REQUIRE((parse_palette_option(palettes[i], options)) == (ASCIICHAT_OK));
  REQUIRE((parse_palette_chars_option(" .+#", options)) == (ASCIICHAT_OK));
  REQUIRE(options->palette_custom_set);
  REQUIRE(std::strcmp((options->palette_custom), (" .+#")) == 0);
  char excessive[sizeof(options->palette_custom) + 1];
  memset(excessive, 'X', sizeof(excessive) - 1);
  excessive[sizeof(excessive) - 1] = 0;
  REQUIRE((parse_palette_chars_option(excessive, options)) == (ERROR_INVALID_PARAM));
  REQUIRE(std::strcmp((options->palette_custom), (" .+#")) == 0);
  REQUIRE((parse_palette_option("bad", options)) == (ERROR_INVALID_PARAM));
  REQUIRE((parse_color_mode_option(NULL, options)) == (ERROR_INVALID_PARAM));
  REQUIRE((parse_render_mode_option(NULL, options)) == (ERROR_INVALID_PARAM));
  REQUIRE((parse_palette_option(NULL, options)) == (ERROR_INVALID_PARAM));
  REQUIRE((parse_palette_chars_option(NULL, options)) == (ERROR_INVALID_PARAM));
  SAFE_FREE(options);
}

TEST_CASE("common_parsers/numeric_options_enforce_ranges_and_do_not_commit_rejected_values", "[common_parsers]") {
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  REQUIRE((parse_width_option("80", options)) == (ASCIICHAT_OK));
  REQUIRE((parse_height_option("25", options)) == (ASCIICHAT_OK));
  REQUIRE(!(options->auto_width));
  REQUIRE(!(options->auto_height));
  REQUIRE((parse_width_option("0", options)) == (ERROR_INVALID_PARAM));
  REQUIRE((parse_height_option("-1", options)) == (ERROR_INVALID_PARAM));
  REQUIRE((options->width) == (80));
  REQUIRE((options->height) == (25));
  REQUIRE((parse_webcam_index_option("0", options)) == (ASCIICHAT_OK));
  REQUIRE((parse_webcam_index_option("-1", options)) == (ERROR_INVALID_PARAM));
  REQUIRE((parse_snapshot_delay_option("1.25", options)) == (ASCIICHAT_OK));
  REQUIRE((options->snapshot_delay) == Catch::Approx(1.25));
  REQUIRE((parse_snapshot_delay_option("1.5x", options)) == (ERROR_INVALID_PARAM));
  REQUIRE((parse_snapshot_delay_option("-1", options)) == (ERROR_INVALID_PARAM));
  REQUIRE((options->snapshot_delay) == Catch::Approx(1.25));
  REQUIRE((parse_log_level_option("debug", options)) == (ASCIICHAT_OK));
  REQUIRE((parse_log_level_option("unknown", options)) == (ERROR_INVALID_PARAM));
  uint16_t port = 17;
  REQUIRE(validate_port_opt("65535", &port));
  REQUIRE((port) == (65535));
  REQUIRE(!(validate_port_opt("65536", &port)));
  REQUIRE((port) == (65535));
  int fps = 10;
  REQUIRE(validate_fps_opt("30", &fps));
  REQUIRE(validate_fps_opt("0", &fps));
  REQUIRE((fps) == (0));
  REQUIRE(!(validate_fps_opt("-1", &fps)));
  REQUIRE((fps) == (0));
  SAFE_FREE(options);
}

TEST_CASE("common_parsers/suggestions_explain_mode_restrictions_and_reject_unrelated_names", "[common_parsers]") {
  option_descriptor_t descriptors[] = {{.long_name = "width", .mode_bitmask = static_cast<option_mode_bitmask_t>(OPTION_MODE_CLIENT | OPTION_MODE_MIRROR)},
                                       {.long_name = "max-clients", .mode_bitmask = static_cast<option_mode_bitmask_t>(OPTION_MODE_SERVER)}};
  options_config_t config = {.descriptors = descriptors, .num_descriptors = 2};
  const char *suggestion = find_similar_option_with_mode("--widht", &config, OPTION_MODE_CLIENT);
  REQUIRE((suggestion) != nullptr);
  REQUIRE((strstr(suggestion, "--width")) != nullptr);
  suggestion = find_similar_option_with_mode("--max-client", &config, OPTION_MODE_CLIENT);
  REQUIRE((strstr(suggestion, "server")) != nullptr);
  REQUIRE((find_similar_option_with_mode("not-an-option", &config, OPTION_MODE_CLIENT)) == nullptr);
  REQUIRE((find_similar_option_with_mode("--unrelated-unknown-flag", &config, OPTION_MODE_CLIENT)) == nullptr);
  REQUIRE(std::strcmp((format_available_modes(OPTION_MODE_BINARY)), ("global options")) == 0);
  REQUIRE(std::strcmp((format_available_modes(static_cast<option_mode_bitmask_t>(0))), ("unknown mode")) == 0);
  const char *all = format_available_modes(static_cast<option_mode_bitmask_t>(OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC));
  REQUIRE((strstr(all, "default")) != nullptr);
  REQUIRE((strstr(all, "discovery-service")) != nullptr);
}
