#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/options/strings.h>
#include <libplatform/terminal.h>
#include <ascii-chat/video/ascii/palette.h>
#include <string.h>

TEST_CASE("option_strings/explicit_modes_round_trip_and_aliases_remain_unambiguous", "[option_strings] [unit]") {
  const struct {
    asciichat_mode_t mode;
    const char *name;
  } modes[] = {{MODE_SERVER, "server"}, {MODE_CLIENT, "client"}, {MODE_MIRROR, "mirror"}, {MODE_DISCOVERY_SERVICE, "discovery-service"}};
  for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); ++i) {
    REQUIRE(std::strcmp((asciichat_mode_to_string(modes[i].mode)), (modes[i].name)) == 0);
    REQUIRE((asciichat_string_to_mode(modes[i].name)) == (modes[i].mode));
    REQUIRE(asciichat_is_valid_mode_string(modes[i].name));
  }
  REQUIRE((asciichat_string_to_mode("acds")) == (MODE_DISCOVERY_SERVICE));
  REQUIRE(std::strcmp((asciichat_mode_to_string(MODE_DISCOVERY)), ("default")) == 0);
  const char *invalid[] = {NULL, "", "SERVER", "default", "discovery", "serverx"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    REQUIRE((asciichat_string_to_mode(invalid[i])) == (MODE_INVALID));
    REQUIRE(!(asciichat_is_valid_mode_string(invalid[i])));
  }
  REQUIRE(std::strcmp((asciichat_mode_to_string(MODE_INVALID)), ("unknown")) == 0);
  const char **names;
  size_t count = asciichat_get_all_mode_strings(&names);
  REQUIRE((count) == (5));
  REQUIRE((asciichat_get_all_mode_strings(NULL)) == (count));
  for (size_t i = 0; i < count; ++i) {
    REQUIRE(asciichat_is_valid_mode_string(names[i]));
    for (size_t j = i + 1; j < count; ++j)
      REQUIRE(std::strcmp((names[i]), (names[j])) != 0);
  }
}
TEST_CASE("option_strings/display_enum_names_and_unknown_values_are_stable", "[option_strings] [unit]") {
  const struct {
    terminal_color_mode_t value;
    const char *name;
  } colors[] = {{TERM_COLOR_AUTO, "auto"}, {TERM_COLOR_NONE, "none"}, {TERM_COLOR_16, "16"}, {TERM_COLOR_256, "256"}, {TERM_COLOR_TRUECOLOR, "truecolor"}};
  for (size_t i = 0; i < sizeof(colors) / sizeof(*colors); ++i)
    REQUIRE(std::strcmp((color_mode_to_string(colors[i].value)), (colors[i].name)) == 0);
  const struct {
    palette_type_t value;
    const char *name;
  } palettes[] = {{PALETTE_STANDARD, "standard"}, {PALETTE_BLOCKS, "blocks"}, {PALETTE_DIGITAL, "digital"}, {PALETTE_MINIMAL, "minimal"}, {PALETTE_COOL, "cool"}, {PALETTE_CUSTOM, "custom"}};
  for (size_t i = 0; i < sizeof(palettes) / sizeof(*palettes); ++i)
    REQUIRE(std::strcmp((palette_type_to_string(palettes[i].value)), (palettes[i].name)) == 0);
  REQUIRE(std::strcmp((color_mode_to_string((terminal_color_mode_t)99)), ("unknown")) == 0);
  REQUIRE(std::strcmp((palette_type_to_string((palette_type_t)99)), ("unknown")) == 0);
}
TEST_CASE("option_strings/typo_suggestions_are_bounded_and_handle_missing_options", "[option_strings] [unit]") {
  REQUIRE(std::strcmp((asciichat_suggest_mode("servre")), ("server")) == 0);
  REQUIRE(std::strcmp((asciichat_suggest_mode("miror")), ("mirror")) == 0);
  REQUIRE((asciichat_suggest_mode(NULL)) == nullptr);
  REQUIRE((asciichat_suggest_mode("completely-unrelated-command")) == nullptr);
  REQUIRE(std::strcmp((asciichat_suggest_enum_value("color-mode", "trueclor")), ("truecolor")) == 0);
  REQUIRE((asciichat_suggest_enum_value("not-an-option", "auto")) == nullptr);
  REQUIRE((asciichat_suggest_enum_value(NULL, "auto")) == nullptr);
  REQUIRE((asciichat_suggest_enum_value("color-mode", NULL)) == nullptr);
}
