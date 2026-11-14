#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/options/strings.h>
#include <libplatform/terminal.h>
#include <ascii-chat/video/ascii/palette.h>
#include <string.h>

TEST_CASE("option_strings/explicit_modes_round_trip_and_aliases_remain_unambiguous", "[option_strings]") {
  const struct {
    asciichat_mode_t mode;
    const char *name;
  } modes[] = {{MODE_SERVER, "server"},
               {MODE_CLIENT, "client"},
               {MODE_MIRROR, "mirror"},
               {MODE_DISCOVERY_SERVICE, "discovery-service"}};
  for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); ++i) {
    ASCIICHAT_REQUIRE_STR_EQ(asciichat_mode_to_string(modes[i].mode), modes[i].name);
    ASCIICHAT_REQUIRE_EQ(asciichat_string_to_mode(modes[i].name), modes[i].mode);
    ASCIICHAT_REQUIRE(asciichat_is_valid_mode_string(modes[i].name));
  }
  ASCIICHAT_REQUIRE_EQ(asciichat_string_to_mode("acds"), MODE_DISCOVERY_SERVICE);
  ASCIICHAT_REQUIRE_STR_EQ(asciichat_mode_to_string(MODE_DISCOVERY), "default");
  const char *invalid[] = {NULL, "", "SERVER", "default", "discovery", "serverx"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    ASCIICHAT_REQUIRE_EQ(asciichat_string_to_mode(invalid[i]), MODE_INVALID);
    ASCIICHAT_REQUIRE_NOT(asciichat_is_valid_mode_string(invalid[i]));
  }
  ASCIICHAT_REQUIRE_STR_EQ(asciichat_mode_to_string(MODE_INVALID), "unknown");
  const char **names;
  size_t count = asciichat_get_all_mode_strings(&names);
  ASCIICHAT_REQUIRE_EQ(count, 5);
  ASCIICHAT_REQUIRE_EQ(asciichat_get_all_mode_strings(NULL), count);
  for (size_t i = 0; i < count; ++i) {
    ASCIICHAT_REQUIRE(asciichat_is_valid_mode_string(names[i]));
    for (size_t j = i + 1; j < count; ++j)
      ASCIICHAT_REQUIRE_STR_NE(names[i], names[j]);
  }
}
TEST_CASE("option_strings/display_enum_names_and_unknown_values_are_stable", "[option_strings]") {
  const struct {
    terminal_color_mode_t value;
    const char *name;
  } colors[] = {{TERM_COLOR_AUTO, "auto"},
                {TERM_COLOR_NONE, "none"},
                {TERM_COLOR_16, "16"},
                {TERM_COLOR_256, "256"},
                {TERM_COLOR_TRUECOLOR, "truecolor"}};
  for (size_t i = 0; i < sizeof(colors) / sizeof(*colors); ++i)
    ASCIICHAT_REQUIRE_STR_EQ(color_mode_to_string(colors[i].value), colors[i].name);
  const struct {
    palette_type_t value;
    const char *name;
  } palettes[] = {{PALETTE_STANDARD, "standard"}, {PALETTE_BLOCKS, "blocks"}, {PALETTE_DIGITAL, "digital"},
                  {PALETTE_MINIMAL, "minimal"},   {PALETTE_COOL, "cool"},     {PALETTE_CUSTOM, "custom"}};
  for (size_t i = 0; i < sizeof(palettes) / sizeof(*palettes); ++i)
    ASCIICHAT_REQUIRE_STR_EQ(palette_type_to_string(palettes[i].value), palettes[i].name);
  ASCIICHAT_REQUIRE_STR_EQ(color_mode_to_string((terminal_color_mode_t)99), "unknown");
  ASCIICHAT_REQUIRE_STR_EQ(palette_type_to_string((palette_type_t)99), "unknown");
}
TEST_CASE("option_strings/typo_suggestions_are_bounded_and_handle_missing_options", "[option_strings]") {
  ASCIICHAT_REQUIRE_STR_EQ(asciichat_suggest_mode("servre"), "server");
  ASCIICHAT_REQUIRE_STR_EQ(asciichat_suggest_mode("miror"), "mirror");
  ASCIICHAT_REQUIRE_NULL(asciichat_suggest_mode(NULL));
  ASCIICHAT_REQUIRE_NULL(asciichat_suggest_mode("completely-unrelated-command"));
  ASCIICHAT_REQUIRE_STR_EQ(asciichat_suggest_enum_value("color-mode", "trueclor"), "truecolor");
  ASCIICHAT_REQUIRE_NULL(asciichat_suggest_enum_value("not-an-option", "auto"));
  ASCIICHAT_REQUIRE_NULL(asciichat_suggest_enum_value(NULL, "auto"));
  ASCIICHAT_REQUIRE_NULL(asciichat_suggest_enum_value("color-mode", NULL));
}
