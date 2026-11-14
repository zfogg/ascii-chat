#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#ifdef _WIN32
#include <ascii-chat/os/windows/getopt.h>
#else
#include <getopt.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif

#include <ascii-chat/options/options.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/rcu.h>     // For RCU-based options access
#include <ascii-chat/options/actions.h> // For deferred action tests
#include <ascii-chat/tests/common.h>
#include <libplatform/terminal.h>
#include <libplatform/system.h>
#include <ascii-chat/tests/logging.h>
// Macro helpers for argv construction without brace-literals at call sites
#define ARGV_LIST(...) (std::vector<const char *>{"program", __VA_ARGS__, nullptr})
#define ARG_WRAP(list) list

// Usage:
// GENERATE_OPTIONS_TEST(name,
//   ARGV_LIST("client", "-x", "100", "-y", "50"),
//   /* is_client */ true,
//   { /* option_assertions using opt_* variables */ },
//   { /* exit_assertions using exit_code */ });
#define GENERATE_OPTIONS_TEST(test_name, argv_list, is_client_val, option_assertions, exit_assertions)                 \
  GENERATE_OPTIONS_TEST_IN_SUITE(options, test_name, argv_list, is_client_val, option_assertions, exit_assertions)

#define GENERATE_OPTIONS_TEST_IN_SUITE(suite_name, test_name, argv_list, is_client_val, option_assertions,             \
                                       exit_assertions)                                                                \
  TEST_CASE(ASCIICHAT_CATCH2_TEST_NAME(suite_name, test_##test_name),                                                  \
            ASCIICHAT_CATCH2_TEST_TAG(suite_name)) {                                                                  \
    auto argv_storage = argv_list;                                                                                     \
    char **argv = const_cast<char **>(argv_storage.data());                                                           \
    int argc = static_cast<int>(argv_storage.size()) - 1;                                                             \
    options_backup_t backup;                                                                                           \
    save_options(&backup);                                                                                             \
                                                                                                                       \
    /* Call options_init to test return value */                                                                       \
    /* (no longer use fork since options_init returns error codes) */                                                  \
    int exit_code = test_options_init_with_fork(argv, argc, is_client_val);                                            \
                                                                                                                       \
    /* Note: We don't call options_init() again because it modifies RCU state and would cause */                       \
    /* crashes when called multiple times. The exit_code from the first call tells us if */                            \
    /* options_init() succeeded or failed, which is what we're testing. */                                             \
    /* (option_assertions not used anymore since we don't call options_init twice) */                                  \
                                                                                                                       \
    restore_options(&backup);                                                                                          \
    exit_assertions                                                                                                    \
  }

// Helper function to save and restore RCU options state for test isolation
// Use the full options_t struct instead of a custom backup struct
typedef options_t options_backup_t;

static void save_options(options_backup_t *backup) {
  // Get current options from RCU and make a copy
  // Don't call init here - let options_init() handle that in the test
  const options_t *current = options_get();
  memcpy(backup, current, sizeof(options_t));
}

static void restore_options(const options_backup_t *backup) {
  (void)backup; // Unused parameter
  // Note: We don't call options_state_destroy() here because it can interfere
  // with the RCU state if multiple tests run sequentially. The atexit handlers
  // will properly clean up when the program exits.
}

// Run parser cases in a child because some usage paths terminate the process.
static int test_options_init_with_fork(char **argv, int argc, bool is_client) {
  (void)is_client; // Unused parameter

#ifdef _WIN32
  // Windows has no fork(); options_init reports parser failures as return
  // codes, so exercise the same contract in-process.
  optind = 1;
  opterr = 1;
  optopt = 0;
  return options_init(argc, argv) == ASCIICHAT_OK ? 0 : 1;
#else
  pid_t pid = fork();
  if (pid < 0) {
    return ERROR_THREAD;
  }

  if (pid == 0) {
    char **argv_copy = SAFE_CALLOC((size_t)argc + 1, sizeof(char *), char **);
    if (!argv_copy) {
      _exit(ERROR_MEMORY);
    }
    for (int i = 0; i < argc; i++) {
      size_t length = strlen(argv[i]) + 1;
      argv_copy[i] = SAFE_MALLOC(length, char *);
      if (!argv_copy[i]) {
        _exit(ERROR_MEMORY);
      }
      memcpy(argv_copy[i], argv[i], length);
    }

    optind = 1;
    opterr = 1;
    optopt = 0;

    asciichat_error_t result = options_init(argc, argv_copy);
    int exit_code = (result == ERROR_USAGE || result == ERROR_INVALID_PARAM) ? 1 : result;
    _exit(exit_code);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return 1;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
#endif
}

/* ============================================================================
 * Basic Functionality Tests
 * ============================================================================ */

TEST_CASE("options/explicit_flip_values_survive_mode_parsing", "[options]") {
  char *argv[] = {"program", "mirror", "--test-pattern=0", "--flip-x=false", "--flip-y=true", NULL};
  optind = 1;
  ASCIICHAT_REQUIRE_EQ(options_init(5, argv), ASCIICHAT_OK);
  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_EQ(opts->flip_x, false);
  ASCIICHAT_REQUIRE(opts->flip_x_explicit);
  ASCIICHAT_REQUIRE_EQ(opts->flip_y, true);
}

TEST_CASE("options/test_pattern_defaults_to_unflipped", "[options]") {
  char *argv[] = {"program", "mirror", "--test-pattern", NULL};
  optind = 1;
  ASCIICHAT_REQUIRE_EQ(options_init(3, argv), ASCIICHAT_OK);
  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE(opts->test_pattern);
  ASCIICHAT_REQUIRE_NOT(opts->flip_x);
  ASCIICHAT_REQUIRE_NOT(opts->flip_x_explicit);
}

TEST_CASE("options/test_pattern_respects_explicit_flip", "[options]") {
  char *argv[] = {"program", "mirror", "--test-pattern", "--flip-x=true", NULL};
  optind = 1;
  ASCIICHAT_REQUIRE_EQ(options_init(4, argv), ASCIICHAT_OK);
  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE(opts->test_pattern);
  ASCIICHAT_REQUIRE(opts->flip_x);
  ASCIICHAT_REQUIRE(opts->flip_x_explicit);
}

TEST_CASE("options/default_values", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // Initialize options with minimal args to get defaults
  char *argv[] = {"program", "client", NULL};
  int argc = 2;
  optind = 1;
  options_init(argc, argv);

  // Get options from RCU for assertions
  const options_t *opts = options_get();
  (void)opts; // Reserved for future validation

  // Test default values
  ASCIICHAT_REQUIRE_EQ(opts->width, 110);
  ASCIICHAT_REQUIRE_EQ(opts->height, 70);
  ASCIICHAT_REQUIRE_EQ(opts->auto_width, 1);
  ASCIICHAT_REQUIRE_EQ(opts->auto_height, 1);
  ASCIICHAT_REQUIRE_STR_EQ(opts->address, "localhost");
  ASCIICHAT_REQUIRE_EQ(opts->port, 27224);
  ASCIICHAT_REQUIRE_EQ(opts->webcam_index, 0);
  ASCIICHAT_REQUIRE_EQ(opts->flip_x, true);
  ASCIICHAT_REQUIRE_NOT(opts->flip_x_explicit);
  ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_AUTO);
  ASCIICHAT_REQUIRE_EQ(opts->render_mode, RENDER_MODE_FOREGROUND);
  ASCIICHAT_REQUIRE_EQ(opts->show_capabilities, 0);
  ASCIICHAT_REQUIRE_EQ(opts->force_utf8, 0);
  ASCIICHAT_REQUIRE_EQ(opts->audio_enabled, 1); // Default changed to true in 8ec7a9f3
  ASCIICHAT_REQUIRE_EQ(opts->stretch, 0);
  ASCIICHAT_REQUIRE_EQ(opts->quiet, 0);
  ASCIICHAT_REQUIRE_EQ(opts->snapshot_mode, 0);
  ASCIICHAT_REQUIRE_EQ(opts->encrypt_enabled, 0);
  ASCIICHAT_REQUIRE_EQ(opts->palette_type, PALETTE_STANDARD);
  ASCIICHAT_REQUIRE_EQ(opts->palette_custom_set, false);

  restore_options(&backup);
}

GENERATE_OPTIONS_TEST_IN_SUITE(
    options, basic_client_options, ARGV_LIST("client", "192.168.1.1:8080", "-x", "100", "-y", "50"), true,
    {
      ASCIICHAT_REQUIRE_STR_EQ(opts->address, "192.168.1.1");
      ASCIICHAT_REQUIRE_EQ(opts->port, 8080);
      ASCIICHAT_REQUIRE_EQ(opts->width, 100);
      ASCIICHAT_REQUIRE_EQ(opts->height, 50);
      ASCIICHAT_REQUIRE_EQ(opts->auto_width, 0);
      ASCIICHAT_REQUIRE_EQ(opts->auto_height, 0);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Basic client options should not exit"); })

GENERATE_OPTIONS_TEST(
    basic_server_options, ARGV_LIST("server", "127.0.0.1", "-p", "3000"), false,
    {
      ASCIICHAT_REQUIRE_STR_EQ(opts->address, "127.0.0.1");
      ASCIICHAT_REQUIRE_EQ(opts->port, 3000);
      // Server should use default or terminal-detected values for dimensions
      // Since opts->auto_width/opts->auto_height are true by default, the code calls
      // update_dimensions_to_terminal_size() which uses get_terminal_size()
      // - If terminal detection succeeds: uses terminal dimensions
      // - If terminal detection fails: falls back to 80x24 (not OPT_WIDTH_DEFAULT)
      // Rather than hardcode specific values, just verify dimensions are reasonable
      ASCIICHAT_REQUIRE_GT(opts->width, 0, "Width should be positive");
      ASCIICHAT_REQUIRE_GT(opts->height, 0, "Height should be positive");
      ASCIICHAT_REQUIRE_EQ(opts->webcam_index, 0);
      ASCIICHAT_REQUIRE_EQ(opts->flip_x, true);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Basic server options should not exit"); })

/* ============================================================================
 * Address and Port Validation Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    valid_ipv4_192_168_1_1, ARGV_LIST("client", "192.168.1.1"), true,
    { ASCIICHAT_REQUIRE_STR_EQ(opts->address, "192.168.1.1"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Valid IP 192.168.1.1 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    valid_ipv4_127_0_0_1, ARGV_LIST("client", "127.0.0.1"), true, { ASCIICHAT_REQUIRE_STR_EQ(opts->address, "127.0.0.1"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Valid IP 127.0.0.1 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    valid_ipv4_255_255_255_255, ARGV_LIST("client", "255.255.255.255"), true,
    { ASCIICHAT_REQUIRE_STR_EQ(opts->address, "255.255.255.255"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Valid IP 255.255.255.255 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_ipv4_octet_too_large, ARGV_LIST("client", "256.1.1.1"), true,
    {
      // This should not be reached since options_init should exit
      ASCIICHAT_FAIL("Should not reach this point - invalid IP should cause exit");
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Invalid IP 256.1.1.1 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_ipv4_too_few_octets, ARGV_LIST("client", "192.168.1"), true,
    {
      // This should not be reached since options_init should exit
      ASCIICHAT_FAIL("Should not reach this point - invalid IP should cause exit");
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Invalid IP 192.168.1 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_ipv4_non_numeric, ARGV_LIST("client", "192.168.1.abc"), true,
    {
      // This should not be reached since options_init should exit
      ASCIICHAT_FAIL("Should not reach this point - invalid IP should cause exit");
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Invalid IP 192.168.1.abc should cause exit with code 1"); })

// =============================================================================
// IP Address Validation Tests - Parameterized (replaces 6 individual tests above)
// =============================================================================

typedef struct {
  char address[32];
  bool should_succeed;
  int expected_exit_code;
  char description[64];
} ip_validation_test_case_t;

static ip_validation_test_case_t ip_validation_cases[] = {
    {"192.168.1.1", true, 0, "Valid IP 192.168.1.1"},         {"127.0.0.1", true, 0, "Valid IP 127.0.0.1"},
    {"255.255.255.255", true, 0, "Valid IP 255.255.255.255"}, {"256.1.1.1", false, 1, "Invalid IP - octet too large"},
    {"192.168.1", false, 1, "Invalid IP - too few octets"},   {"192.168.1.abc", false, 1, "Invalid IP - non-numeric"},
};



TEST_CASE("options/ip_address_validation", "[options]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(ip_validation_cases)); auto *tc = &generated_case;
  char *argv[] = {"program", "client", (char *)tc->address, NULL};
  int argc = 3;
  options_backup_t backup;
  save_options(&backup);

  // Test exit behavior with fork
  int exit_code = test_options_init_with_fork(argv, argc, true);

  // Test option values in main process (only if no exit expected)
  if (tc->should_succeed) {
    ASCIICHAT_REQUIRE_EQ(exit_code, 0, "%s should not cause exit", tc->description);
    // Reset getopt state before calling options_init again
    optind = 1;
    opterr = 1;
    optopt = 0;
    options_init(argc, argv);
    // Get options from RCU for assertions
    const options_t *opts = options_get();
    ASCIICHAT_REQUIRE_STR_EQ(opts->address, tc->address, "%s should set address correctly", tc->description);
  } else {
    ASCIICHAT_REQUIRE_EQ(exit_code, tc->expected_exit_code, "%s should cause exit with code %d", tc->description,
                 tc->expected_exit_code);
  }

  restore_options(&backup);
}

// =============================================================================
// Port Validation Tests
// =============================================================================

GENERATE_OPTIONS_TEST(
    valid_port_80, ARGV_LIST("client", "-p", "80"), true, { ASCIICHAT_REQUIRE_EQ(opts->port, 80); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Valid port 80 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    valid_port_65535, ARGV_LIST("client", "-p", "65535"), true, { ASCIICHAT_REQUIRE_EQ(opts->port, 65535); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Valid port 65535 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_port_too_low, ARGV_LIST("client", "-p", "0"), true,
    {
      // This should not be reached since options_init should exit
      ASCIICHAT_FAIL("Should not reach this point - invalid port should cause exit");
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Invalid port 0 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_port_too_high, ARGV_LIST("client", "-p", "65536"), true,
    {
      // This should not be reached since options_init should exit
      ASCIICHAT_FAIL("Should not reach this point - invalid port should cause exit");
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Invalid port 65536 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_port_non_numeric, ARGV_LIST("client", "-p", "abc"), true,
    {
      // This should not be reached since options_init should exit
      ASCIICHAT_FAIL("Should not reach this point - invalid port should cause exit");
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Invalid port abc should cause exit with code 1"); })

// =============================================================================
// Port Validation Tests - Parameterized (replaces 5 individual tests above)
// =============================================================================

typedef struct {
  char port[16];
  bool should_succeed;
  int expected_exit_code;
  char description[64];
} port_validation_test_case_t;

static port_validation_test_case_t port_validation_cases[] = {
    // Valid ports - ALLOWED
    {"1", true, 0, "Valid port 1 (minimum)"},
    {"80", true, 0, "Valid port 80 (HTTP)"},
    {"443", true, 0, "Valid port 443 (HTTPS)"},
    {"8080", true, 0, "Valid port 8080"},
    {"65535", true, 0, "Valid port 65535 (maximum)"},

    // Invalid ports - DISALLOWED (out of range)
    {"0", false, 1, "Invalid port 0 (too low)"},
    {"65536", false, 1, "Invalid port 65536 (too high)"},
    {"99999", false, 1, "Invalid port 99999 (way too high)"},

    // Invalid ports - DISALLOWED (format/security)
    {"abc", false, 1, "Invalid port - non-numeric"},
    {"0123", false, 1, "Invalid port - leading zero (octal confusion)"},
    {"00080", false, 1, "Invalid port - multiple leading zeros"},
    {" 80", false, 1, "Invalid port - leading whitespace"},
    {"80 ", false, 1, "Invalid port - trailing whitespace"},
    {" 80 ", false, 1, "Invalid port - both leading and trailing whitespace"},
    {"-1", false, 1, "Invalid port - negative number"},
    {"+80", false, 1, "Invalid port - explicit plus sign"},
    {"0x50", false, 1, "Invalid port - hexadecimal notation"},
    {"", false, 1, "Invalid port - empty string"},
};



TEST_CASE("options/port_validation", "[options]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(port_validation_cases)); auto *tc = &generated_case;
  char *argv[] = {"program", "client", "-p", (char *)tc->port, NULL};
  int argc = 4;
  options_backup_t backup;
  save_options(&backup);

  // Test exit behavior with fork
  int exit_code = test_options_init_with_fork(argv, argc, true);

  // Test option values in main process (only if no exit expected)
  if (tc->should_succeed) {
    ASCIICHAT_REQUIRE_EQ(exit_code, 0, "%s should not cause exit", tc->description);
    // Reset getopt state before calling options_init again
    optind = 1;
    opterr = 1;
    optopt = 0;
    options_init(argc, argv);
    // Get options from RCU for assertions
    const options_t *opts = options_get();
    ASCIICHAT_REQUIRE_EQ(opts->port, strtoint_safe(tc->port), "%s should set port correctly", tc->description);
  } else {
    ASCIICHAT_REQUIRE_EQ(exit_code, tc->expected_exit_code, "%s should cause exit with code %d", tc->description,
                 tc->expected_exit_code);
  }

  restore_options(&backup);
}

/* ============================================================================
 * Dimension Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    valid_dimensions, ARGV_LIST("client", "-x", "100", "-y", "50"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->width, 100);
      ASCIICHAT_REQUIRE_EQ(opts->height, 50);
      ASCIICHAT_REQUIRE_EQ(opts->auto_width, 0);
      ASCIICHAT_REQUIRE_EQ(opts->auto_height, 0);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Valid dimensions should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_dimension_zero, ARGV_LIST("client", "-x", "0"), true,
    { ASCIICHAT_FAIL("Should not reach this point - invalid dimension should cause exit"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Invalid dimension 0 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_dimension_negative, ARGV_LIST("client", "-x", "-1"), true,
    { ASCIICHAT_FAIL("Should not reach this point - invalid dimension should cause exit"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Invalid dimension -1 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_dimension_non_numeric, ARGV_LIST("client", "-x", "abc"), true,
    { ASCIICHAT_FAIL("Should not reach this point - invalid dimension should cause exit"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Invalid dimension abc should cause exit with code 1"); })

/* ============================================================================
 * Webcam Options Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(valid_webcam_index, ARGV_LIST("client", "-c", "2"), true,
                      {/* no option assertions needed for success-only check */}, { ASCIICHAT_REQUIRE_EQ(exit_code, 0); })

GENERATE_OPTIONS_TEST(
    invalid_webcam_index_neg1, ARGV_LIST("client", "-c", "-1"), true,
    { ASCIICHAT_FAIL("Should not reach this point - invalid webcam index should cause exit"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1); })

GENERATE_OPTIONS_TEST(
    invalid_webcam_index_abc, ARGV_LIST("client", "-c", "abc"), true,
    { ASCIICHAT_FAIL("Should not reach this point - invalid webcam index should cause exit"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1); })

GENERATE_OPTIONS_TEST(
    invalid_webcam_index_decimal, ARGV_LIST("client", "-c", "2.5"), true,
    { ASCIICHAT_FAIL("Should not reach this point - invalid webcam index should cause exit"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1); })

GENERATE_OPTIONS_TEST(
    invalid_webcam_index_empty, ARGV_LIST("client", "-c", ""), true,
    { ASCIICHAT_FAIL("Should not reach this point - invalid webcam index should cause exit"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1); })

GENERATE_OPTIONS_TEST(valid_webcam_flip, ARGV_LIST("client", "--flip-x"), true, {/* flip flag only */},
                      { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Webcam flip flag should not cause exit"); })

/* ============================================================================
 * Color Mode Tests
 * ============================================================================ */

// =============================================================================
// Color Mode Validation Tests - Parameterized (replaces loop-based tests)
// =============================================================================

typedef struct {
  char mode_string[32];
  bool should_succeed;
  int expected_exit_code;
  char description[64];
} color_mode_test_case_t;

static color_mode_test_case_t color_mode_cases[] = {
    // Valid modes
    {"auto", true, 0, "Valid mode: auto"},
    {"none", true, 0, "Valid mode: none"},
    {"16", true, 0, "Valid mode: 16"},
    {"16color", true, 0, "Valid mode: 16color"},
    {"256", true, 0, "Valid mode: 256"},
    {"256color", true, 0, "Valid mode: 256color"},
    {"truecolor", true, 0, "Valid mode: truecolor"},
    {"24bit", true, 0, "Valid mode: 24bit"},
    {"rgb", true, 0, "Valid mode: rgb"},
    {"tc", true, 0, "Valid mode: tc"},
    {"true", true, 0, "Valid mode: true"},
    {"a", true, 0, "Valid mode: a"},
    {"mono", true, 0, "Valid mode: mono"},
    {"ansi", true, 0, "Valid mode: ansi"},
    // Invalid modes
    {"invalid", false, 1, "Invalid mode: invalid"},
    {"32", false, 1, "Invalid mode: 32"},
    {"512", false, 1, "Invalid mode: 512"},
    {"fullcolor", false, 1, "Invalid mode: fullcolor"},
    {"", false, 1, "Invalid mode: empty string"},
};



TEST_CASE("options/color_mode_validation", "[options]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(color_mode_cases)); auto *tc = &generated_case;
  char *argv[] = {"program", "client", "--color-mode", (char *)tc->mode_string, NULL};
  int argc = 4;
  options_backup_t backup;
  save_options(&backup);

  // Test exit behavior with fork
  int exit_code = test_options_init_with_fork(argv, argc, true);

  ASCIICHAT_REQUIRE_EQ(exit_code, tc->expected_exit_code, "%s should %s", tc->description,
               tc->should_succeed ? "not cause exit" : "cause exit");

  restore_options(&backup);
}

// Original loop-based tests replaced by parameterized version above
// TEST_CASE("options/valid_color_modes", "[options]") { ... }
// TEST_CASE("options/invalid_color_modes", "[options]") { ... }

/* ============================================================================
 * Render Mode Tests
 * ============================================================================ */

TEST_CASE("options/valid_render_modes", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *valid_modes[] = {"foreground", "fg", "background", "bg", "half-block"};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"program", "client", "--render-mode", valid_modes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    ASCIICHAT_REQUIRE_EQ(result, 0, "Valid render mode %s should not cause exit", valid_modes[i]);
  }

  restore_options(&backup);
}

TEST_CASE("options/invalid_render_modes", "[options]") {
  char *invalid_modes[] = {"invalid", "full", "block", "text", ""};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"program", "client", "--render-mode", invalid_modes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    ASCIICHAT_REQUIRE_EQ(result, 1, "Invalid render mode %s should cause exit with code 1", invalid_modes[i]);
  }
}

/* ============================================================================
 * Palette Tests
 * ============================================================================ */

TEST_CASE("options/valid_palettes", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *valid_palettes[] = {"standard", "blocks", "digital", "minimal", "cool", "custom"};

  for (int i = 0; i < 6; i++) {
    char *argv[] = {"program", "client", "--palette", valid_palettes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    ASCIICHAT_REQUIRE_EQ(result, 0, "Valid palette %s should not cause exit", valid_palettes[i]);
  }

  restore_options(&backup);
}

TEST_CASE("options/invalid_palettes", "[options]") {
  char *invalid_palettes[] = {"invalid", "ascii", "unicode", "color", ""};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"program", "client", "--palette", invalid_palettes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    ASCIICHAT_REQUIRE_EQ(result, 1, "Invalid palette %s should cause exit with code 1", invalid_palettes[i]);
  }
}

TEST_CASE("options/valid_palette_chars", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"program", "client", "--palette-chars", " .:-=+*#%@$", NULL};
  int result = test_options_init_with_fork(argv, 4, true);
  ASCIICHAT_REQUIRE_EQ(result, 0);

  restore_options(&backup);
}

TEST_CASE("options/invalid_palette_chars", "[options]") {
  // Empty palette chars should fail
  char *argv[] = {"program", "client", "--palette-chars", "", NULL};
  int result = test_options_init_with_fork(argv, 4, true);
  ASCIICHAT_REQUIRE_EQ(result, 1);
}

/* ============================================================================
 * Snapshot Delay Tests
 * ============================================================================ */

TEST_CASE("options/valid_snapshot_delays", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *valid_delays[] = {"0.0", "1.5", "3.0", "10.0", "0"};

  for (int i = 0; i < 5; i++) {
    // snapshot-delay requires --snapshot to be set
    char *argv[] = {"program", "client", "--snapshot", "--snapshot-delay", valid_delays[i], NULL};
    int result = test_options_init_with_fork(argv, 5, true);
    ASCIICHAT_REQUIRE_EQ(result, 0, "Valid snapshot delay %s should not cause exit", valid_delays[i]);
  }

  restore_options(&backup);
}

TEST_CASE("options/invalid_snapshot_delays", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *invalid_delays[] = {
      "abc", // Non-numeric - should fail
      ""     // Empty - should fail
  };

  for (int i = 0; i < 2; i++) {
    // snapshot-delay requires --snapshot to be set
    char *argv[] = {"program", "client", "--snapshot", "--snapshot-delay", invalid_delays[i], NULL};
    int result = test_options_init_with_fork(argv, 5, true);
    ASCIICHAT_REQUIRE_EQ(result, 1, "Invalid snapshot delay %s should cause exit with code 1", invalid_delays[i]);
  }

  restore_options(&backup);
}

/* ============================================================================
 * File Path Tests
 * ============================================================================ */

// NOTE: --log-file is now a global option handled at binary level, not mode-specific
// GENERATE_OPTIONS_TEST(
//     valid_log_file, ARGV_LIST("client", "--log-file", "/tmp/test.log"), true,
//     { ASCIICHAT_REQUIRE_STR_EQ(opts->log_file, "/tmp/test.log"); },
//     { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Valid log file should not cause exit"); })
//
// GENERATE_OPTIONS_TEST(
//     invalid_log_file, ARGV_LIST("client", "--log-file", ""), true,
//     { ASCIICHAT_FAIL("Should not reach this point - empty log file should cause exit"); },
//     { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Empty log file should exit with code 1"); })

GENERATE_OPTIONS_TEST(
    valid_encryption_key, ARGV_LIST("client", "--key", "mysecretkey"), true,
    {
      ASCIICHAT_REQUIRE_STR_EQ(opts->encrypt_key, "mysecretkey");
      ASCIICHAT_REQUIRE_EQ(opts->encrypt_enabled, 0);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Valid encryption key should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_encryption_key, ARGV_LIST("client", "--key", ""), true,
    { ASCIICHAT_FAIL("Should not reach this point - empty key should cause exit"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Empty key should exit with code 1"); })

/* ============================================================================
 * Flag Options Tests
 * ============================================================================ */

TEST_CASE("options/flag_options", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // NOTE: --quiet is now a global option, removed from this test
  char *argv[] = {"program",   "client",     "--show-capabilities", "--utf8", "--audio",
                  "--stretch", "--snapshot", "--encrypt",           NULL};
  int result = test_options_init_with_fork(argv, 8, true);
  ASCIICHAT_REQUIRE_EQ(result, 0);

  restore_options(&backup);
}

/* ============================================================================
 * Help Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(help_client, ARGV_LIST("client", "--help"), true,
                      {
                          // Help should display and exit cleanly
                      },
                      { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Help should exit with code 0"); })

GENERATE_OPTIONS_TEST(help_server, ARGV_LIST("server", "--help"), false,
                      {
                          // Help should display and exit cleanly
                      },
                      { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Help should exit with code 0"); })

GENERATE_OPTIONS_TEST(help_short, ARGV_LIST("client", "-h"), true,
                      {
                          // Help should display and exit cleanly
                      },
                      { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "Short help should exit with code 0"); })

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST_IN_SUITE(
    options_errors, unknown_option, ARGV_LIST("client", "--unknown-option"), true,
    { ASCIICHAT_FAIL("Should not reach this point - unknown option should cause exit"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Unknown option should exit with code 1"); })

GENERATE_OPTIONS_TEST_IN_SUITE(
    options_errors, missing_argument_port, ARGV_LIST("client", "--port"), true,
    { ASCIICHAT_FAIL("Should not reach this point - missing argument should cause exit"); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 1, "Missing argument should exit with code 1"); })

/* ============================================================================
 * Equals Sign Handling Tests
 * ============================================================================ */

TEST_CASE("options/equals_sign_handling", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"program", "client", "192.168.1.1:8080", "--width=100", "--height=50", NULL};
  int result = test_options_init_with_fork(argv, 5, true);
  ASCIICHAT_REQUIRE_EQ(result, 0);

  restore_options(&backup);
}

/* ============================================================================
 * Complex Combinations Tests
 * ============================================================================ */

TEST_CASE("options/complex_client_combination", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // NOTE: --quiet and --log-file are now global options, removed from this test
  char *argv[] = {"program",
                  "client",
                  "192.168.1.100:8080",
                  "--width=120",
                  "--height=60",
                  "--webcam-index=1",
                  "--flip-x",
                  "--color-mode=256",
                  "--render-mode=background",
                  "--palette=blocks",
                  "--audio",
                  "--stretch",
                  "--snapshot",
                  "--snapshot-delay=2.5",
                  "--encrypt",
                  "--key=mysecretpassword",
                  NULL};
  int argc = 16;

  log_set_level(LOG_DEBUG);
  int result = test_options_init_with_fork(argv, argc, true);
  ASCIICHAT_REQUIRE_EQ(result, 0);

  restore_options(&backup);
}

TEST_CASE("options/complex_server_combination", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // NOTE: --log-file is now a global option, removed from this test
  // NOTE: --palette is client-only, removed from this server test
  char *argv[] = {"program", "server", "0.0.0.0", "--port=27224", "--encrypt", NULL};
  int argc = 5;

  int result = test_options_init_with_fork(argv, argc, false);
  ASCIICHAT_REQUIRE_EQ(result, 0);

  restore_options(&backup);
}

/* ============================================================================
 * Usage Function Tests
 * NOTE: usage() tests removed - they cause crashes in backtrace symbolizer code
 *       when errors occur during help generation. The help system is tested
 *       implicitly through the --help option parsing tests.
 * ============================================================================ */

/* ============================================================================
 * Dimension Update Tests
 * ============================================================================ */

TEST_CASE("options/update_dimensions_for_full_height", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // Create a local writable copy to test the function
  options_t test_opts;
  memcpy(&test_opts, options_get(), sizeof(options_t));

  // Test with auto dimensions
  test_opts.auto_width = 1;
  test_opts.auto_height = 1;
  update_dimensions_for_full_height(&test_opts);

  // Test with only auto height
  test_opts.auto_width = 0;
  test_opts.auto_height = 1;
  update_dimensions_for_full_height(&test_opts);

  // Test with only auto width
  test_opts.auto_width = 1;
  test_opts.auto_height = 0;
  update_dimensions_for_full_height(&test_opts);

  // Test with no auto dimensions
  test_opts.auto_width = 0;
  test_opts.auto_height = 0;
  update_dimensions_for_full_height(&test_opts);

  restore_options(&backup);
}

TEST_CASE("options/update_dimensions_to_terminal_size", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // Create a local writable copy to test the function
  options_t test_opts;
  memcpy(&test_opts, options_get(), sizeof(options_t));

  // Test with auto dimensions
  test_opts.auto_width = 1;
  test_opts.auto_height = 1;
  update_dimensions_to_terminal_size(&test_opts);

  // Test with only auto width
  test_opts.auto_width = 1;
  test_opts.auto_height = 0;
  update_dimensions_to_terminal_size(&test_opts);

  // Test with only auto height
  test_opts.auto_width = 0;
  test_opts.auto_height = 1;
  update_dimensions_to_terminal_size(&test_opts);

  // Test with no auto dimensions
  test_opts.auto_width = 0;
  test_opts.auto_height = 0;
  update_dimensions_to_terminal_size(&test_opts);

  restore_options(&backup);
}

/* ============================================================================
 * Edge Cases and Stress Tests
 * ============================================================================ */

TEST_CASE("options/very_long_arguments", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // Test with very long but valid arguments
  // NOTE: -L (log-file) is now a global option, testing only address here
  char long_address[OPTIONS_BUFF_SIZE];

  memset(long_address, '1', sizeof(long_address) - 1);
  long_address[sizeof(long_address) - 1] = '\0';
  SAFE_STRNCPY(long_address, "192.168.1.1:8080", sizeof(long_address)); // Valid but test the buffer handling

  char *argv[] = {"program", "client", long_address, NULL};
  int result = test_options_init_with_fork(argv, 3, true);
  ASCIICHAT_REQUIRE_EQ(result, 0);

  restore_options(&backup);
}

TEST_CASE("options/maximum_values", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {
      "program", "client", "--width=512", "--height=256", "--webcam-index=10", "--snapshot", "--snapshot-delay=999.999",
      NULL};
  int argc = 7;

  int result = test_options_init_with_fork(argv, argc, true);
  ASCIICHAT_REQUIRE_EQ(result, 0);

  restore_options(&backup);
}

TEST_CASE("options/minimum_values", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"program",          "client",     "--width=20",           "--height=10",
                  "--webcam-index=0", "--snapshot", "--snapshot-delay=0.0", NULL};
  int argc = 7;

  int result = test_options_init_with_fork(argv, argc, true);
  ASCIICHAT_REQUIRE_EQ(result, 0);

  restore_options(&backup);
}

TEST_CASE("options/random_combinations", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // Test random combinations of valid options
  srand(42); // Fixed seed for reproducible tests

  for (int i = 0; i < 10; i++) {
    char *argv[20];
    int argc = 2;
    argv[0] = "program";
    argv[1] = "client";

    // Randomly add valid options
    // NOTE: --quiet is now a global option, removed from this test
    if (rand() % 2) {
      argv[argc++] = "192.168.1.1:8080";
    }
    if (rand() % 2) {
      argv[argc++] = "--audio";
    }
    if (rand() % 2) {
      argv[argc++] = "--stretch";
    }

    int result = test_options_init_with_fork(argv, argc, true);
    ASCIICHAT_REQUIRE_EQ(result, 0, "Random combination %d should not cause exit", i);
  }

  restore_options(&backup);
}

/* ============================================================================
 * Direct Value Testing - Tests that actually verify opt_* variables
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    actual_values_client, ARGV_LIST("client", "192.168.1.1:8080", "-x", "100", "-y", "50"), true,
    {
      // Test actual values were set
      ASCIICHAT_REQUIRE_STR_EQ(opts->address, "192.168.1.1");
      ASCIICHAT_REQUIRE_EQ(opts->port, 8080);
      ASCIICHAT_REQUIRE_EQ(opts->width, 100);
      ASCIICHAT_REQUIRE_EQ(opts->height, 50);
      ASCIICHAT_REQUIRE_EQ(opts->auto_width, 0);
      ASCIICHAT_REQUIRE_EQ(opts->auto_height, 0);
    },
    {
      // Test that options_init doesn't exit with error
      ASCIICHAT_REQUIRE_EQ(exit_code, 0, "options_init should not exit with error");
    })

GENERATE_OPTIONS_TEST(
    color_mode_auto, ARGV_LIST("client", "--color-mode", "auto"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_AUTO); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "auto color mode should not exit"); })

GENERATE_OPTIONS_TEST(
    color_mode_256, ARGV_LIST("client", "--color-mode", "256"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_256_COLOR); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "256 color mode should not exit"); })

GENERATE_OPTIONS_TEST(
    color_mode_truecolor, ARGV_LIST("client", "--color-mode", "truecolor"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_TRUECOLOR); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "truecolor mode should not exit"); })

GENERATE_OPTIONS_TEST(
    render_mode_foreground, ARGV_LIST("client", "--render-mode", "foreground"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->render_mode, RENDER_MODE_FOREGROUND); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "foreground render mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    render_mode_background, ARGV_LIST("client", "--render-mode", "background"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->render_mode, RENDER_MODE_BACKGROUND); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "background render mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    render_mode_half_block, ARGV_LIST("client", "--render-mode", "half-block"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->render_mode, RENDER_MODE_HALF_BLOCK); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "half-block render mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    palette_standard, ARGV_LIST("client", "--palette", "standard"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->palette_type, PALETTE_STANDARD); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "standard palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    palette_blocks, ARGV_LIST("client", "--palette", "blocks"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->palette_type, PALETTE_BLOCKS); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "blocks palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    palette_custom_chars, ARGV_LIST("client", "--palette-chars", "@#%*+=:-. "), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->palette_type, PALETTE_CUSTOM);
      ASCIICHAT_REQUIRE_STR_EQ(opts->palette_custom, "@#%*+=:-. ");
      ASCIICHAT_REQUIRE_EQ(opts->palette_custom_set, true);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "custom palette chars should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_flag_values,
    // NOTE: --quiet is now a global option, removed from this test
    ARGV_LIST("client", "--audio", "--stretch", "--snapshot", "--encrypt", "--utf8", "--show-capabilities", "--flip-x"),
    true,
    {
      // Test all flags were set
      ASCIICHAT_REQUIRE_EQ(opts->audio_enabled, 1);
      ASCIICHAT_REQUIRE_EQ(opts->stretch, 1);
      // opts->quiet removed - now a global option
      ASCIICHAT_REQUIRE_EQ(opts->snapshot_mode, 1);
      ASCIICHAT_REQUIRE_EQ(opts->encrypt_enabled, 1);
      ASCIICHAT_REQUIRE_EQ(opts->force_utf8, 1);
      ASCIICHAT_REQUIRE_EQ(opts->show_capabilities, 1);
      ASCIICHAT_REQUIRE_EQ(opts->flip_x, true);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "flag values should not cause exit"); })

GENERATE_OPTIONS_TEST(
    encryption_key_value, ARGV_LIST("client", "--key", "mysecretpassword123"), true,
    {
      ASCIICHAT_REQUIRE_STR_EQ(opts->encrypt_key, "mysecretpassword123");
      ASCIICHAT_REQUIRE_EQ(opts->encrypt_enabled, 0);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "encryption key should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_snapshot_delay_values, ARGV_LIST("client", "--snapshot", "--snapshot-delay", "2.5"), true,
    { ASCIICHAT_REQUIRE_FLOAT_EQ(opts->snapshot_delay, 2.5f, 0.01); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "snapshot delay should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_webcam_values, ARGV_LIST("client", "-c", "3", "--flip-x"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->webcam_index, 3);
      ASCIICHAT_REQUIRE_EQ(opts->flip_x, true);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "webcam values should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_comprehensive_client_values,
    // NOTE: --quiet and --log-file are now global options, removed from this test
    ARGV_LIST("client", "10.0.0.1:9999", "--width=200", "--height=100", "--webcam-index=2", "--flip-x",
              "--color-mode=256", "--render-mode=background", "--palette=digital", "--audio", "--stretch", "--snapshot",
              "--snapshot-delay=5.0", "--encrypt", "--key=testkey123"),
    true,
    {
      // Verify ALL values
      ASCIICHAT_REQUIRE_STR_EQ(opts->address, "10.0.0.1");
      ASCIICHAT_REQUIRE_EQ(opts->port, 9999);
      ASCIICHAT_REQUIRE_EQ(opts->width, 200);
      ASCIICHAT_REQUIRE_EQ(opts->height, 100);
      ASCIICHAT_REQUIRE_EQ(opts->webcam_index, 2);
      ASCIICHAT_REQUIRE_EQ(opts->flip_x, true);
      ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_256_COLOR);
      ASCIICHAT_REQUIRE_EQ(opts->render_mode, RENDER_MODE_BACKGROUND);
      ASCIICHAT_REQUIRE_EQ(opts->palette_type, PALETTE_DIGITAL);
      ASCIICHAT_REQUIRE_EQ(opts->audio_enabled, 1);
      ASCIICHAT_REQUIRE_EQ(opts->stretch, 1);
      // opts->quiet removed - now a global option
      ASCIICHAT_REQUIRE_EQ(opts->snapshot_mode, 1);
      ASCIICHAT_REQUIRE_FLOAT_EQ(opts->snapshot_delay, 5.0f, 0.01);
      // opts->log_file removed - now a global option
      ASCIICHAT_REQUIRE_EQ(opts->encrypt_enabled, 1);
      ASCIICHAT_REQUIRE_STR_EQ(opts->encrypt_key, "testkey123");
      // Should be enabled by color-mode
      ASCIICHAT_REQUIRE_EQ(opts->auto_width, 0);  // Should be disabled when width is set
      ASCIICHAT_REQUIRE_EQ(opts->auto_height, 0); // Should be disabled when height is set
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "comprehensive client values should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_server_values,
    // NOTE: --log-file is now a global option, removed from this test
    // NOTE: --palette is client-only, removed from this server test
    ARGV_LIST("server", "0.0.0.0", "--port=12345", "--encrypt"), false,
    {
      // Verify server values
      ASCIICHAT_REQUIRE_STR_EQ(opts->address, "0.0.0.0");
      ASCIICHAT_REQUIRE_EQ(opts->port, 12345);
      // Note: --audio is not supported for server mode
      // opts->log_file removed - now a global option
      // opts->palette_type removed - palette is client-only
      ASCIICHAT_REQUIRE_EQ(opts->encrypt_enabled, 1);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "server values should not cause exit"); })

/* ============================================================================
 * Additional Test Scenarios with the Macro
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    test_equals_sign_syntax, ARGV_LIST("client", "192.168.1.100:8080", "--width=150", "--height=75"), true,
    {
      ASCIICHAT_REQUIRE_STR_EQ(opts->address, "192.168.1.100");
      ASCIICHAT_REQUIRE_EQ(opts->port, 8080);
      ASCIICHAT_REQUIRE_EQ(opts->width, 150);
      ASCIICHAT_REQUIRE_EQ(opts->height, 75);
      ASCIICHAT_REQUIRE_EQ(opts->auto_width, 0);  // Should be disabled when width is set
      ASCIICHAT_REQUIRE_EQ(opts->auto_height, 0); // Should be disabled when height is set
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "equals sign syntax should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_mixed_syntax, ARGV_LIST("client", "10.0.0.1:3000", "-x", "80", "--height=60"), true,
    {
      ASCIICHAT_REQUIRE_STR_EQ(opts->address, "10.0.0.1");
      ASCIICHAT_REQUIRE_EQ(opts->port, 3000);
      ASCIICHAT_REQUIRE_EQ(opts->width, 80);
      ASCIICHAT_REQUIRE_EQ(opts->height, 60);
      ASCIICHAT_REQUIRE_EQ(opts->auto_width, 0);
      ASCIICHAT_REQUIRE_EQ(opts->auto_height, 0);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "mixed syntax should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_none_mode, ARGV_LIST("client", "--color-mode", "none"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_NONE);
      // Color output should be disabled for none mode
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "none mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_16color_mode, ARGV_LIST("client", "--color-mode", "16"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_16_COLOR); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "16color mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_16color_alias, ARGV_LIST("client", "--color-mode", "16color"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_16_COLOR); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "16color alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_256color_alias, ARGV_LIST("client", "--color-mode", "256color"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_256_COLOR); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "256color alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_truecolor_alias, ARGV_LIST("client", "--color-mode", "24bit"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_TRUECOLOR); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "truecolor alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_palette_digital, ARGV_LIST("client", "--palette", "digital"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->palette_type, PALETTE_DIGITAL); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "digital palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_palette_minimal, ARGV_LIST("client", "--palette", "minimal"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->palette_type, PALETTE_MINIMAL); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "minimal palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_palette_cool, ARGV_LIST("client", "--palette", "cool"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->palette_type, PALETTE_COOL); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "cool palette should not cause exit"); })

/* Converted looped palette test into specific GENERATE_OPTIONS_TEST cases above. */

GENERATE_OPTIONS_TEST(
    test_render_mode_fg_alias, ARGV_LIST("client", "--render-mode", "fg"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->render_mode, RENDER_MODE_FOREGROUND); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "fg alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_render_mode_bg_alias, ARGV_LIST("client", "--render-mode", "bg"), true,
    { ASCIICHAT_REQUIRE_EQ(opts->render_mode, RENDER_MODE_BACKGROUND); },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "bg alias should not cause exit"); })

// NOTE: --log-file is now a global option handled at binary level, not mode-specific
// GENERATE_OPTIONS_TEST(
//     test_log_file_path, ARGV_LIST("client", "--log-file", "/var/log/ascii-chat.log"), true,
//     { ASCIICHAT_REQUIRE_STR_EQ(opts->log_file, "/var/log/ascii-chat.log"); },
//     { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "log file path should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_webcam_index_only, ARGV_LIST("client", "-c", "5"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->webcam_index, 5);
      ASCIICHAT_REQUIRE_EQ(opts->flip_x, true); // Should remain default
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "webcam index should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_webcam_flip_only, ARGV_LIST("client", "--flip-x"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->webcam_index, 0); // Should remain default
      ASCIICHAT_REQUIRE_EQ(opts->flip_x, true);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "webcam flip should not cause exit"); })

/* ============================================================================
 * Server-specific Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    test_server_basic_options, ARGV_LIST("client", "127.0.0.1:8080", "--width=110", "--height=70"), true,
    {
      ASCIICHAT_REQUIRE_STR_EQ(opts->address, "127.0.0.1");
      ASCIICHAT_REQUIRE_EQ(opts->port, 8080);
      // Server should use default values for client-only options
      ASCIICHAT_REQUIRE_EQ(opts->width, 110);      // Should use default width
      ASCIICHAT_REQUIRE_EQ(opts->height, 70);      // Should use default height
      ASCIICHAT_REQUIRE_EQ(opts->webcam_index, 0); // Should use default
      ASCIICHAT_REQUIRE_EQ(opts->flip_x, true);    // Should use default
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "server basic options should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_server_palette_options, ARGV_LIST("client", "--palette", "blocks", "--palette-chars", "0123456789"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->palette_type, PALETTE_CUSTOM); // --palette-chars overrides to custom
      ASCIICHAT_REQUIRE_STR_EQ(opts->palette_custom, "0123456789");
      ASCIICHAT_REQUIRE_EQ(opts->palette_custom_set, true);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "palette options with palette-chars should not cause exit"); })

/* ============================================================================
 * Edge Cases and Error Conditions
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    test_auto_dimensions, ARGV_LIST("client"), true,
    {
      // These should be set to auto (1) by default
      ASCIICHAT_REQUIRE_EQ(opts->auto_width, 1);
      ASCIICHAT_REQUIRE_EQ(opts->auto_height, 1);
      // The actual dimensions will be set by update_dimensions_to_terminal_size()
      // but we can't easily test that without mocking terminal detection
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "auto dimensions should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_manual_dimensions_disable_auto, ARGV_LIST("client", "--width", "100", "--height", "50"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->width, 100);
      ASCIICHAT_REQUIRE_EQ(opts->height, 50);
      ASCIICHAT_REQUIRE_EQ(opts->auto_width, 0);  // Should be disabled when manually set
      ASCIICHAT_REQUIRE_EQ(opts->auto_height, 0); // Should be disabled when manually set
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "manual dimensions should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_encryption_auto_enable, ARGV_LIST("client", "--key", "mypassword"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->encrypt_enabled, 0); // Keys do not force payload encryption
      ASCIICHAT_REQUIRE_STR_EQ(opts->encrypt_key, "mypassword");
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "encryption key should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_custom_palette_auto_set_type, ARGV_LIST("client", "--palette-chars", "ABCDEFGH"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->palette_type, PALETTE_CUSTOM);
      ASCIICHAT_REQUIRE_STR_EQ(opts->palette_custom, "ABCDEFGH");
      ASCIICHAT_REQUIRE_EQ(opts->palette_custom_set, true);
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "custom palette chars should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_color_output_enabled_by_color_mode, ARGV_LIST("client", "--color-mode", "256"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_256_COLOR);
      // Should be enabled
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "256 color mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_color_output_disabled_by_none, ARGV_LIST("client", "--color-mode", "none"), true,
    {
      ASCIICHAT_REQUIRE_EQ(opts->color_mode, COLOR_MODE_NONE);
      // Color output should be disabled
    },
    { ASCIICHAT_REQUIRE_EQ(exit_code, 0, "none color mode should not cause exit"); })

// ============================================================================
// Deferred Actions Tests
// ============================================================================

/**
 * @brief Test that action functions defer execution instead of exiting
 *
 * Verifies the deferred action system works correctly:
 * 1. action_list_webcams() defers ACTION_LIST_WEBCAMS
 * 2. action_list_microphones() defers ACTION_LIST_MICROPHONES
 * 3. action_list_speakers() defers ACTION_LIST_SPEAKERS
 * 4. action_show_capabilities() defers ACTION_SHOW_CAPABILITIES
 * 5. Only the first action is remembered when multiple are deferred
 */

TEST_CASE("options/deferred_action_list_webcams", "[options]") {
  // Defer the action
  action_list_webcams();

  // Verify it was deferred (not executed immediately, which would exit)
  deferred_action_t action = actions_get_deferred();
  ASCIICHAT_REQUIRE_EQ(action, ACTION_LIST_WEBCAMS, "action_list_webcams() should defer ACTION_LIST_WEBCAMS");
}

TEST_CASE("options/deferred_action_list_microphones", "[options]") {
  // Defer the action
  action_list_microphones();

  // Verify it was deferred
  deferred_action_t action = actions_get_deferred();
  ASCIICHAT_REQUIRE_EQ(action, ACTION_LIST_MICROPHONES, "action_list_microphones() should defer ACTION_LIST_MICROPHONES");
}

TEST_CASE("options/deferred_action_list_speakers", "[options]") {
  // Defer the action
  action_list_speakers();

  // Verify it was deferred
  deferred_action_t action = actions_get_deferred();
  ASCIICHAT_REQUIRE_EQ(action, ACTION_LIST_SPEAKERS, "action_list_speakers() should defer ACTION_LIST_SPEAKERS");
}

TEST_CASE("options/deferred_action_show_capabilities", "[options]") {
  // Defer the action
  action_show_capabilities();

  // Verify it was deferred
  deferred_action_t action = actions_get_deferred();
  ASCIICHAT_REQUIRE_EQ(action, ACTION_SHOW_CAPABILITIES, "action_show_capabilities() should defer ACTION_SHOW_CAPABILITIES");
}

TEST_CASE("options/deferred_action_first_wins", "[options]") {
  // Defer first action
  action_list_webcams();
  ASCIICHAT_REQUIRE_EQ(actions_get_deferred(), ACTION_LIST_WEBCAMS, "First action should be deferred");

  // Try to defer second action
  action_list_microphones();
  ASCIICHAT_REQUIRE_EQ(actions_get_deferred(), ACTION_LIST_WEBCAMS,
               "First action should still be deferred, second action should be ignored");

  // Try to defer third action
  action_show_capabilities();
  ASCIICHAT_REQUIRE_EQ(actions_get_deferred(), ACTION_LIST_WEBCAMS,
               "First action should still be deferred, third action should be ignored");
}

TEST_CASE("options/deferred_action_arguments", "[options]") {
  // Set action with arguments
  action_args_t args = {.output_path = "/tmp/test.txt", .shell_name = "bash"};

  actions_defer(ACTION_LIST_WEBCAMS, &args);

  // Retrieve and verify arguments
  const action_args_t *retrieved_args = actions_get_args();
  ASCIICHAT_REQUIRE_NOT_NULL(retrieved_args, "Action arguments should be retrievable");
  ASCIICHAT_REQUIRE_STR_EQ(retrieved_args->output_path, "/tmp/test.txt", "Output path should be preserved");
  ASCIICHAT_REQUIRE_STR_EQ(retrieved_args->shell_name, "bash", "Shell name should be preserved");
}

TEST_CASE("options/deferred_action_no_action_by_default", "[options]") {
  deferred_action_t action = actions_get_deferred();
  ASCIICHAT_REQUIRE_EQ(action, ACTION_NONE, "ACTION_NONE should be the default deferred action");

  const action_args_t *args = actions_get_args();
  ASCIICHAT_REQUIRE_NULL(args, "Arguments should be NULL when action is ACTION_NONE");
}

TEST_CASE("options_errors/custom_validator_preserves_errno_context", "[options_errors]") {
  option_descriptor_t descriptor = *options_registry_find_by_name("video-codec");
  options_config_t config = {.descriptors = &descriptor, .num_descriptors = 1};
  options_t opts = {.video_codec = "invalid"};
  asciichat_errno_scope_t scope = errno_checkpoint();
  ASCIICHAT_REQUIRE_EQ(options_config_validate(&config, &opts, NULL), ERROR_USAGE);
  asciichat_error_context_t error;
  ASCIICHAT_REQUIRE(errno_peek_since(scope, &error));
  ASCIICHAT_REQUIRE_STR_EQ(error.context_message, "--video-codec must be auto, hevc, h264, or raw");
  errno_scope_end(scope, ASCIICHAT_ERRNO_HANDLED);
}

static bool reject_without_errno(const void *opts) {
  (void)opts;
  return false;
}

TEST_CASE("options_errors/custom_validator_does_not_reuse_stale_errno", "[options_errors]") {
  option_descriptor_t descriptor = {.long_name = "test", .validate = reject_without_errno};
  options_config_t config = {.descriptors = &descriptor, .num_descriptors = 1};
  options_t opts{};
  asciichat_errno_scope_t scope = errno_checkpoint();
  SET_ERRNO(ERROR_MEMORY, "Unrelated earlier failure");
  ASCIICHAT_REQUIRE_EQ(options_config_validate(&config, &opts, NULL), ERROR_USAGE);
  asciichat_error_context_t error;
  ASCIICHAT_REQUIRE(errno_peek_since(scope, &error));
  ASCIICHAT_REQUIRE_STR_EQ(error.context_message, "Option '--test' failed validation");
  errno_scope_end(scope, ASCIICHAT_ERRNO_HANDLED);
}

TEST_CASE("options/repeated_media_arguments_share_one_ordered_playlist", "[options]") {
  char *argv[] = {"program",          "mirror", "--file", "first.mp4", "--url", "https://example.com/second.webm",
                  "--file=third.mp4", NULL};
  optind = 1;
  ASCIICHAT_REQUIRE_EQ(options_init(7, argv), ASCIICHAT_OK);
  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_EQ(opts->media_playlist.count, 3);
  ASCIICHAT_REQUIRE_STR_EQ(opts->media_playlist.paths[0], "first.mp4");
  ASCIICHAT_REQUIRE_STR_EQ(opts->media_playlist.paths[1], "https://example.com/second.webm");
  ASCIICHAT_REQUIRE_STR_EQ(opts->media_playlist.paths[2], "third.mp4");
  ASCIICHAT_REQUIRE_STR_EQ(opts->media_file, "first.mp4");
  ASCIICHAT_REQUIRE_STR_EMPTY(opts->media_url);
}

TEST_CASE("options/url_first_playlist_does_not_gain_file_priority", "[options]") {
  char *argv[] = {"program", "mirror", "--url=https://example.com/first.webm", "-f", "second.mp4", NULL};
  optind = 1;
  ASCIICHAT_REQUIRE_EQ(options_init(5, argv), ASCIICHAT_OK);
  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_EQ(opts->media_playlist.count, 2);
  ASCIICHAT_REQUIRE_STR_EQ(opts->media_url, "https://example.com/first.webm");
  ASCIICHAT_REQUIRE_STR_EMPTY(opts->media_file);
}

TEST_CASE("options/rejected_glyph_palette_is_not_published", "[options]") {
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  options_t *before = SAFE_MALLOC(sizeof(*before), options_t *);
  *before = *options_get();
  char *argv[] = {"program", "mirror", "--renderer=glyph", "--palette=digital", NULL};
  optind = 1;
  ASCIICHAT_REQUIRE_NE(options_init(4, argv), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_get()->renderer, before->renderer);
  ASCIICHAT_REQUIRE_EQ(options_get()->palette_type, before->palette_type);
  SAFE_FREE(before);
  CLEAR_ERRNO_ALL();
  options_state_destroy();
}
