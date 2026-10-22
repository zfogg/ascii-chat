#include <criterion/criterion.h>
#include <ascii-chat/options/strings.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/video/ascii/palette.h>
#include <string.h>

Test(option_strings, explicit_modes_round_trip_and_aliases_remain_unambiguous) {
  const struct {
    asciichat_mode_t mode;
    const char *name;
  } modes[] = {{MODE_SERVER, "server"},
               {MODE_CLIENT, "client"},
               {MODE_MIRROR, "mirror"},
               {MODE_DISCOVERY_SERVICE, "discovery-service"}};
  for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); ++i) {
    cr_assert_str_eq(asciichat_mode_to_string(modes[i].mode), modes[i].name);
    cr_assert_eq(asciichat_string_to_mode(modes[i].name), modes[i].mode);
    cr_assert(asciichat_is_valid_mode_string(modes[i].name));
  }
  cr_assert_eq(asciichat_string_to_mode("acds"), MODE_DISCOVERY_SERVICE);
  cr_assert_str_eq(asciichat_mode_to_string(MODE_DISCOVERY), "default");
  const char *invalid[] = {NULL, "", "SERVER", "default", "discovery", "serverx"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    cr_assert_eq(asciichat_string_to_mode(invalid[i]), MODE_INVALID);
    cr_assert_not(asciichat_is_valid_mode_string(invalid[i]));
  }
  cr_assert_str_eq(asciichat_mode_to_string(MODE_INVALID), "unknown");
  const char **names;
  size_t count = asciichat_get_all_mode_strings(&names);
  cr_assert_eq(count, 5);
  cr_assert_eq(asciichat_get_all_mode_strings(NULL), count);
  for (size_t i = 0; i < count; ++i) {
    cr_assert(asciichat_is_valid_mode_string(names[i]));
    for (size_t j = i + 1; j < count; ++j)
      cr_assert_str_neq(names[i], names[j]);
  }
}
Test(option_strings, display_enum_names_and_unknown_values_are_stable) {
  const struct {
    terminal_color_mode_t value;
    const char *name;
  } colors[] = {{TERM_COLOR_AUTO, "auto"},
                {TERM_COLOR_NONE, "none"},
                {TERM_COLOR_16, "16"},
                {TERM_COLOR_256, "256"},
                {TERM_COLOR_TRUECOLOR, "truecolor"}};
  for (size_t i = 0; i < sizeof(colors) / sizeof(*colors); ++i)
    cr_assert_str_eq(color_mode_to_string(colors[i].value), colors[i].name);
  const struct {
    palette_type_t value;
    const char *name;
  } palettes[] = {{PALETTE_STANDARD, "standard"}, {PALETTE_BLOCKS, "blocks"}, {PALETTE_DIGITAL, "digital"},
                  {PALETTE_MINIMAL, "minimal"},   {PALETTE_COOL, "cool"},     {PALETTE_CUSTOM, "custom"}};
  for (size_t i = 0; i < sizeof(palettes) / sizeof(*palettes); ++i)
    cr_assert_str_eq(palette_type_to_string(palettes[i].value), palettes[i].name);
  cr_assert_str_eq(color_mode_to_string((terminal_color_mode_t)99), "unknown");
  cr_assert_str_eq(palette_type_to_string((palette_type_t)99), "unknown");
}
Test(option_strings, typo_suggestions_are_bounded_and_handle_missing_options) {
  cr_assert_str_eq(asciichat_suggest_mode("servre"), "server");
  cr_assert_str_eq(asciichat_suggest_mode("miror"), "mirror");
  cr_assert_null(asciichat_suggest_mode(NULL));
  cr_assert_null(asciichat_suggest_mode("completely-unrelated-command"));
  cr_assert_str_eq(asciichat_suggest_enum_value("color-mode", "trueclor"), "truecolor");
  cr_assert_null(asciichat_suggest_enum_value("not-an-option", "auto"));
  cr_assert_null(asciichat_suggest_enum_value(NULL, "auto"));
  cr_assert_null(asciichat_suggest_enum_value("color-mode", NULL));
}
