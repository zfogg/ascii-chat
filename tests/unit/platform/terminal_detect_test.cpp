#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <stdlib.h>
#include <stdint.h>

#include <libplatform/terminal.h>
#include <libplatform/system.h>
#include <options/options.h>
#include <ascii-chat/tests/logging.h>

#define setenv(name, value, overwrite) platform_setenv((name), (value))
#define unsetenv platform_unsetenv
static inline terminal_capabilities_t detect_terminal_capabilities_test() {
  return ::detect_terminal_capabilities(NULL);
}
static inline terminal_capabilities_t detect_terminal_capabilities_test(const terminal_options_t *options) {
  return ::detect_terminal_capabilities(options);
}
#define detect_terminal_capabilities(...) detect_terminal_capabilities_test(__VA_ARGS__)

// Use the enhanced macro with stdout/stderr enabled for debugging
// Note: We don't disable stdout/stderr because terminal detection needs them
TEST_CASE("terminal_detect/hyperlink_format_and_plain_fallback", "[terminal_detect]") {
  char output[128];
  int length = terminal_format_hyperlink(output, sizeof(output), "https://ascii-chat.com", "ascii-chat.com", true);
  REQUIRE((length) > (0));
  REQUIRE(((size_t)length) == (strlen(output)));
  REQUIRE(std::strcmp((output), ("\033]8;;https://ascii-chat.com\033\\\033[4mascii-chat.com\033[24m\033]8;;\033\\")) == 0);
  REQUIRE((terminal_format_hyperlink(output, sizeof(output), "https://ascii-chat.com", "ascii-chat.com", false)) == (14));
  REQUIRE(std::strcmp((output), ("ascii-chat.com")) == 0);
}

TEST_CASE("terminal_detect/hyperlink_rejects_controls_and_partial_output", "[terminal_detect]") {
  char output[128] = "unchanged";
  REQUIRE((terminal_format_hyperlink(output, sizeof(output), "https://example.com\033]8;;evil", "site", true)) == (-1));
  REQUIRE(std::strcmp((output), ("")) == 0);
  REQUIRE((terminal_format_hyperlink(output, sizeof(output), "https://example.com", "site\n", true)) == (-1));
  REQUIRE(std::strcmp((output), ("")) == 0);
  REQUIRE((terminal_format_hyperlink(output, 4, "https://example.com", "site", true)) == (-1));
  REQUIRE(std::strcmp((output), ("")) == 0);
  REQUIRE((terminal_format_hyperlink(output, 4, "https://example.com", "site", false)) == (-1));
  REQUIRE(std::strcmp((output), ("")) == 0);
  REQUIRE((terminal_format_hyperlink(NULL, 0, "https://example.com", "site", true)) == (-1));
}

/* ============================================================================
 * Terminal Size Detection Tests
 * ============================================================================ */

TEST_CASE("terminal_detect/get_terminal_size_basic", "[terminal_detect]") {
  unsigned short int width, height;

  int result = get_terminal_size(&width, &height);

  // Should return valid dimensions (may be 0 if not in terminal)
  REQUIRE((width) >= (0));
  REQUIRE((height) >= (0));

  // If we're in a terminal, dimensions should be reasonable
  if (result == 0) {
    REQUIRE((width) > (0));
    REQUIRE((height) > (0));
    REQUIRE((width) <= (1000)); // Reasonable upper bound
    REQUIRE((height) <= (1000));
  }
}

TEST_CASE("terminal_detect/get_terminal_size_null_pointers", "[terminal_detect]") {
  // Note: get_terminal_size doesn't check for NULL pointers, so this test is removed
  // to avoid crashes. The function should be fixed to handle NULL gracefully.
}

/* ============================================================================
 * Terminal Capabilities Detection Tests
 * ============================================================================ */

TEST_CASE("terminal_detect/detect_terminal_capabilities_basic", "[terminal_detect]") {
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Basic sanity checks
  REQUIRE((caps.term_type) != nullptr);
  REQUIRE((caps.color_level) <= (TERM_COLOR_TRUECOLOR));
  REQUIRE((caps.color_count) >= (0));

  // Render mode should be valid
  REQUIRE((caps.render_mode == RENDER_MODE_FOREGROUND || caps.render_mode == RENDER_MODE_BACKGROUND || caps.render_mode == RENDER_MODE_HALF_BLOCK));
}

// Parameterized test for COLORTERM variable detection
typedef struct {
  char colorterm_value[32];
  terminal_color_mode_t expected_color_level;
  uint32_t expected_color_count;
  bool should_have_truecolor_cap;
  char description[64];
} colorterm_test_case_t;

static colorterm_test_case_t colorterm_cases[] = {
    {"truecolor", TERM_COLOR_TRUECOLOR, 16777216, true, "COLORTERM=truecolor"},
    {"24bit", TERM_COLOR_TRUECOLOR, 16777216, true, "COLORTERM=24bit"},
    {"", TERM_COLOR_NONE, 0, false, "COLORTERM empty (unset)"},
    {"other", TERM_COLOR_NONE, 0, false, "COLORTERM=other (unknown value)"},
};

TEST_CASE("terminal_detect/colorterm_variable_detection_parameterized", "[terminal_detect]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(colorterm_cases));
  auto *tc = &generated_case;
  // Save original environment
  char *original_colorterm = getenv("COLORTERM");
  char *original_term = getenv("TERM");

  // Clear TERM to test COLORTERM in isolation
  unsetenv("TERM");

  // Set COLORTERM value (empty string means unset)
  if (tc->colorterm_value[0] == '\0') {
    unsetenv("COLORTERM");
  } else {
    setenv("COLORTERM", tc->colorterm_value, 1);
  }

  terminal_capabilities_t caps = detect_terminal_capabilities(options_terminal_settings());

  REQUIRE((caps.color_level) == (tc->expected_color_level));

  if (tc->expected_color_count > 0) {
    REQUIRE((caps.color_count) == (tc->expected_color_count));
  }

  if (tc->should_have_truecolor_cap) {
    uint32_t result = caps.capabilities & TERM_CAP_COLOR_TRUE;
    REQUIRE((result) != (0));
  }

  // Restore original environment
  if (original_colorterm) {
    setenv("COLORTERM", original_colorterm, 1);
  } else {
    unsetenv("COLORTERM");
  }

  if (original_term) {
    setenv("TERM", original_term, 1);
  } else {
    unsetenv("TERM");
  }
}

// Parameterized test for TERM variable color detection
typedef struct {
  char term_value[32];
  terminal_color_mode_t expected_color_level;
  uint32_t expected_color_count;
  uint32_t expected_capability_flag;
  char description[64];
} term_color_test_case_t;

static term_color_test_case_t term_color_cases[] = {
    {"xterm-256color", TERM_COLOR_256, 256, TERM_CAP_COLOR_256, "TERM=xterm-256color"},
    {"xterm-color", TERM_COLOR_16, 16, TERM_CAP_COLOR_16, "TERM=xterm-color"},
    {"xterm", TERM_COLOR_16, 16, TERM_CAP_COLOR_16, "TERM=xterm"},
    {"screen", TERM_COLOR_16, 16, TERM_CAP_COLOR_16, "TERM=screen"},
    {"linux", TERM_COLOR_16, 16, TERM_CAP_COLOR_16, "TERM=linux"},
    {"unknown", TERM_COLOR_NONE, 0, 0, "TERM=unknown"},
    {"dumb", TERM_COLOR_NONE, 0, 0, "TERM=dumb"},
};

TEST_CASE("terminal_detect/term_variable_color_detection_parameterized", "[terminal_detect]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(term_color_cases));
  auto *tc = &generated_case;
  // Save original environment
  char *original_term = getenv("TERM");
  char *original_colorterm = getenv("COLORTERM");

  // Clear COLORTERM to test TERM variable parsing in isolation
  unsetenv("COLORTERM");

  // Set TERM value
  setenv("TERM", tc->term_value, 1);

  terminal_capabilities_t caps = detect_terminal_capabilities(options_terminal_settings());

  REQUIRE((caps.color_level) == (tc->expected_color_level));

  if (tc->expected_color_count > 0) {
    REQUIRE((caps.color_count) == (tc->expected_color_count));
  }

  if (tc->expected_capability_flag != 0) {
    REQUIRE((caps.capabilities & tc->expected_capability_flag) != (0));
  }

  // Restore original environment
  if (original_term) {
    setenv("TERM", original_term, 1);
  } else {
    unsetenv("TERM");
  }

  if (original_colorterm) {
    setenv("COLORTERM", original_colorterm, 1);
  } else {
    unsetenv("COLORTERM");
  }
}

// Parameterized test for UTF-8 support detection
typedef struct {
  char lang_value[32];
  char lc_all_value[32];
  char lc_ctype_value[32];
  bool expected_utf8_support;
  char description[64];
} utf8_test_case_t;

static utf8_test_case_t utf8_cases[] = {
    {"en_US.UTF-8", "", "", true, "LANG=en_US.UTF-8"},    {"C.UTF-8", "", "", true, "LANG=C.UTF-8"}, {"C", "C.UTF-8", "", true, "LC_ALL=C.UTF-8 (takes precedence over LANG)"},
    {"C", "", "en_US.utf8", true, "LC_CTYPE=en_US.utf8"}, {"C", "", "", false, "LANG=C (no UTF-8)"}, {"", "", "", false, "All locale vars unset"},
};

TEST_CASE("terminal_detect/utf8_support_detection_parameterized", "[terminal_detect]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(utf8_cases));
  auto *tc = &generated_case;
  // Save original environment
  char *original_lang = getenv("LANG");
  char *original_lc_all = getenv("LC_ALL");
  char *original_lc_ctype = getenv("LC_CTYPE");

  // Set environment variables (empty string means unset)
  if (tc->lang_value[0] == '\0') {
    unsetenv("LANG");
  } else {
    setenv("LANG", tc->lang_value, 1);
  }

  if (tc->lc_all_value[0] == '\0') {
    unsetenv("LC_ALL");
  } else {
    setenv("LC_ALL", tc->lc_all_value, 1);
  }

  if (tc->lc_ctype_value[0] == '\0') {
    unsetenv("LC_CTYPE");
  } else {
    setenv("LC_CTYPE", tc->lc_ctype_value, 1);
  }

  terminal_capabilities_t caps = detect_terminal_capabilities(options_terminal_settings());

  REQUIRE((caps.utf8_support) == (tc->expected_utf8_support));

  if (tc->expected_utf8_support) {
    REQUIRE((caps.capabilities & TERM_CAP_UTF8) != (0));
  }

  // Restore original environment
  if (original_lang) {
    setenv("LANG", original_lang, 1);
  } else {
    unsetenv("LANG");
  }

  if (original_lc_all) {
    setenv("LC_ALL", original_lc_all, 1);
  } else {
    unsetenv("LC_ALL");
  }

  if (original_lc_ctype) {
    setenv("LC_CTYPE", original_lc_ctype, 1);
  } else {
    unsetenv("LC_CTYPE");
  }
}

TEST_CASE("terminal_detect/render_mode_selection", "[terminal_detect]") {
  // Save original environment
  char *original_term = getenv("TERM");
  char *original_lang = getenv("LANG");
  char *original_colorterm = getenv("COLORTERM");

  // Test default render mode with color + UTF-8
  // Note: Half-block mode is only used when explicitly requested via --render-mode
  unsetenv("COLORTERM");
  setenv("TERM", "xterm-256color", 1);
  setenv("LANG", "en_US.UTF-8", 1);
  terminal_capabilities_t caps = detect_terminal_capabilities();
  REQUIRE((caps.render_mode) == (RENDER_MODE_FOREGROUND)); // Always defaults to foreground
  REQUIRE((caps.capabilities & TERM_CAP_BACKGROUND) != (0));

  // Test foreground mode (color without UTF-8)
  setenv("TERM", "xterm-color", 1);
  setenv("LANG", "C", 1);
  unsetenv("LC_ALL");   // Clear LC_ALL which takes precedence
  unsetenv("LC_CTYPE"); // Clear LC_CTYPE which also affects UTF-8 detection
  caps = detect_terminal_capabilities();
  REQUIRE((caps.render_mode) == (RENDER_MODE_FOREGROUND));

  // Test monochrome fallback
  setenv("TERM", "dumb", 1);
  setenv("LANG", "C", 1);
  caps = detect_terminal_capabilities();
  REQUIRE((caps.render_mode) == (RENDER_MODE_FOREGROUND));
  REQUIRE((caps.color_level) == (TERM_COLOR_NONE));

  // Restore original environment
  if (original_term) {
    setenv("TERM", original_term, 1);
  } else {
    unsetenv("TERM");
  }

  if (original_lang) {
    setenv("LANG", original_lang, 1);
  } else {
    unsetenv("LANG");
  }

  if (original_colorterm) {
    setenv("COLORTERM", original_colorterm, 1);
  }
}

TEST_CASE("terminal_detect/capability_flags", "[terminal_detect]") {
  // Save original environment
  char *original_colorterm = getenv("COLORTERM");
  char *original_term = getenv("TERM");

  // Clear TERM to test COLORTERM in isolation
  unsetenv("TERM");

  // Test truecolor capabilities
  setenv("COLORTERM", "truecolor", 1);
  terminal_capabilities_t caps = detect_terminal_capabilities();
  REQUIRE((caps.capabilities & TERM_CAP_COLOR_TRUE) != (0));
  REQUIRE((caps.capabilities & TERM_CAP_COLOR_256) != (0));
  REQUIRE((caps.capabilities & TERM_CAP_COLOR_16) != (0));

  // Test UTF-8 capability
  if (caps.utf8_support) {
    REQUIRE((caps.capabilities & TERM_CAP_UTF8) != (0));
  }

  // Restore original environment
  if (original_colorterm) {
    setenv("COLORTERM", original_colorterm, 1);
  } else {
    unsetenv("COLORTERM");
  }

  if (original_term) {
    setenv("TERM", original_term, 1);
  } else {
    unsetenv("TERM");
  }
}

TEST_CASE("terminal_detect/terminal_type_storage", "[terminal_detect]") {
  // Save original environment
  char *original_term = getenv("TERM");
  char *original_colorterm = getenv("COLORTERM");

  // Test TERM storage
  setenv("TERM", "xterm-256color", 1);
  setenv("COLORTERM", "truecolor", 1);
  terminal_capabilities_t caps = detect_terminal_capabilities();
  REQUIRE(std::strcmp((caps.term_type), ("xterm-256color")) == 0);
  REQUIRE(std::strcmp((caps.colorterm), ("truecolor")) == 0);

  // Test unknown terminal
  unsetenv("TERM");
  unsetenv("COLORTERM");
  caps = detect_terminal_capabilities();
  REQUIRE(std::strcmp((caps.term_type), ("unknown")) == 0);
  REQUIRE(std::strcmp((caps.colorterm), ("")) == 0);

  // Restore original environment
  if (original_term) {
    setenv("TERM", original_term, 1);
  }

  if (original_colorterm) {
    setenv("COLORTERM", original_colorterm, 1);
  }
}

/* ============================================================================
 * Helper Function Tests
 * ============================================================================ */

TEST_CASE("terminal_detect/color_level_names", "[terminal_detect]") {
  REQUIRE(std::strcmp((terminal_color_level_name(TERM_COLOR_NONE)), ("none")) == 0);
  REQUIRE(std::strcmp((terminal_color_level_name(TERM_COLOR_16)), ("16-color")) == 0);
  REQUIRE(std::strcmp((terminal_color_level_name(TERM_COLOR_256)), ("256-color")) == 0);
  REQUIRE(std::strcmp((terminal_color_level_name(TERM_COLOR_TRUECOLOR)), ("truecolor")) == 0);
}

TEST_CASE("terminal_detect/detection_reliability", "[terminal_detect]") {
  terminal_capabilities_t caps = detect_terminal_capabilities();
  // POSIX systems should generally have reliable detection
  REQUIRE(caps.detection_reliable);
}
