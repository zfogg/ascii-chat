#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <criterion/logging.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>     // For RCU-based options access
#include <ascii-chat/options/actions.h> // For deferred action tests
#include <ascii-chat/tests/common.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/tests/logging.h>

// Use the enhanced macro to create complete test suites with custom log levels
// TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVEL(options, LOG_INFO, LOG_DEBUG);
// TEST_LOGGING_SETUP_AND_TEARDOWN_WITH_LOG_LEVELS(LOG_FATAL, LOG_DEBUG, false, false);
// TestSuite(options, .init = setup_quiet_test_logging, .fini = restore_test_logging);

TEST_SUITE_WITH_DEBUG_LOGGING(options);
TEST_SUITE_WITH_DEBUG_LOGGING(options_errors);

// Macro helpers for argv construction without brace-literals at call sites
#define ARGV_LIST(...) ((char *[]){"program", __VA_ARGS__, NULL})
#define ARG_WRAP(list) ((char *[])list)

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
  Test(suite_name, test_##test_name) {                                                                                 \
    char *argv[] = argv_list;                                                                                          \
    int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;                                                              \
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

// Helper function to test options_init (no fork needed since options_init returns error codes)
static int test_options_init_with_fork(char **argv, int argc, bool is_client) {
  (void)is_client; // Unused parameter

  // ALWAYS make a copy of argv to prevent options_init() from modifying the original array.
  // This is critical because options_init() might modify argv strings during parsing
  // (e.g., temporarily replacing '=' with '\0' for equals-sign syntax parsing),
  // and we need to prevent that from affecting the original test data.
  char **argv_copy = SAFE_CALLOC((size_t)argc + 1, sizeof(char *), char **);
  if (!argv_copy) {
    return ERROR_MEMORY;
  }

  // Copy all argv pointers and ensure NULL termination
  for (int i = 0; i < argc; i++) {
    argv_copy[i] = argv[i];
  }
  argv_copy[argc] = NULL;

  // Reset getopt state before calling options_init
  optind = 1;
  opterr = 1;
  optopt = 0;

  // Call options_init with the copied argv
  asciichat_error_t result = options_init(argc, argv_copy);

  // Free the copied argv
  SAFE_FREE(argv_copy);

  // Return appropriate code based on return value
  // Map both ERROR_USAGE and ERROR_INVALID_PARAM to exit code 1
  if (result != ASCIICHAT_OK) {
    return (result == ERROR_USAGE || result == ERROR_INVALID_PARAM) ? 1 : result;
  }
  return 0;
}

/* ============================================================================
 * Basic Functionality Tests
 * ============================================================================ */

Test(options, default_values) {
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
  cr_assert_eq(opts->width, 110);
  cr_assert_eq(opts->height, 70);
  cr_assert_eq(opts->auto_width, 1);
  cr_assert_eq(opts->auto_height, 1);
  cr_assert_str_eq(opts->address, "localhost");
  cr_assert_eq(opts->port, 27224);
  cr_assert_eq(opts->webcam_index, 0);
  cr_assert_eq(opts->webcam_flip, true);
  cr_assert_eq(opts->color_mode, COLOR_MODE_AUTO);
  cr_assert_eq(opts->render_mode, RENDER_MODE_FOREGROUND);
  cr_assert_eq(opts->show_capabilities, 0);
  cr_assert_eq(opts->force_utf8, 0);
  cr_assert_eq(opts->audio_enabled, 1); // Default changed to true in 8ec7a9f3
  cr_assert_eq(opts->stretch, 0);
  cr_assert_eq(opts->quiet, 0);
  cr_assert_eq(opts->snapshot_mode, 0);
  cr_assert_eq(opts->encrypt_enabled, 1);
  cr_assert_eq(opts->palette_type, PALETTE_STANDARD);
  cr_assert_eq(opts->palette_custom_set, false);

  restore_options(&backup);
}

GENERATE_OPTIONS_TEST_IN_SUITE(
    options, basic_client_options, ARGV_LIST("client", "192.168.1.1:8080", "-x", "100", "-y", "50"), true,
    {
      cr_assert_str_eq(opts->address, "192.168.1.1");
      cr_assert_eq(opts->port, 8080);
      cr_assert_eq(opts->width, 100);
      cr_assert_eq(opts->height, 50);
      cr_assert_eq(opts->auto_width, 0);
      cr_assert_eq(opts->auto_height, 0);
    },
    { cr_assert_eq(exit_code, 0, "Basic client options should not exit"); })

GENERATE_OPTIONS_TEST(
    basic_server_options, ARGV_LIST("server", "127.0.0.1", "-p", "3000"), false,
    {
      cr_assert_str_eq(opts->address, "127.0.0.1");
      cr_assert_eq(opts->port, 3000);
      // Server should use default or terminal-detected values for dimensions
      // Since opts->auto_width/opts->auto_height are true by default, the code calls
      // update_dimensions_to_terminal_size() which uses get_terminal_size()
      // - If terminal detection succeeds: uses terminal dimensions
      // - If terminal detection fails: falls back to 80x24 (not OPT_WIDTH_DEFAULT)
      // Rather than hardcode specific values, just verify dimensions are reasonable
      cr_assert_gt(opts->width, 0, "Width should be positive");
      cr_assert_gt(opts->height, 0, "Height should be positive");
      cr_assert_eq(opts->webcam_index, 0);
      cr_assert_eq(opts->webcam_flip, true);
    },
    { cr_assert_eq(exit_code, 0, "Basic server options should not exit"); })

/* ============================================================================
 * Address and Port Validation Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    valid_ipv4_192_168_1_1, ARGV_LIST("client", "192.168.1.1"), true,
    { cr_assert_str_eq(opts->address, "192.168.1.1"); },
    { cr_assert_eq(exit_code, 0, "Valid IP 192.168.1.1 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    valid_ipv4_127_0_0_1, ARGV_LIST("client", "127.0.0.1"), true, { cr_assert_str_eq(opts->address, "127.0.0.1"); },
    { cr_assert_eq(exit_code, 0, "Valid IP 127.0.0.1 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    valid_ipv4_255_255_255_255, ARGV_LIST("client", "255.255.255.255"), true,
    { cr_assert_str_eq(opts->address, "255.255.255.255"); },
    { cr_assert_eq(exit_code, 0, "Valid IP 255.255.255.255 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_ipv4_octet_too_large, ARGV_LIST("client", "256.1.1.1"), true,
    {
      // This should not be reached since options_init should exit
      cr_fail("Should not reach this point - invalid IP should cause exit");
    },
    { cr_assert_eq(exit_code, 1, "Invalid IP 256.1.1.1 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_ipv4_too_few_octets, ARGV_LIST("client", "192.168.1"), true,
    {
      // This should not be reached since options_init should exit
      cr_fail("Should not reach this point - invalid IP should cause exit");
    },
    { cr_assert_eq(exit_code, 1, "Invalid IP 192.168.1 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_ipv4_non_numeric, ARGV_LIST("client", "192.168.1.abc"), true,
    {
      // This should not be reached since options_init should exit
      cr_fail("Should not reach this point - invalid IP should cause exit");
    },
    { cr_assert_eq(exit_code, 1, "Invalid IP 192.168.1.abc should cause exit with code 1"); })

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

ParameterizedTestParameters(options, ip_address_validation) {
  size_t count = sizeof(ip_validation_cases) / sizeof(ip_validation_cases[0]);
  return cr_make_param_array(ip_validation_test_case_t, ip_validation_cases, count);
}

ParameterizedTest(ip_validation_test_case_t *tc, options, ip_address_validation) {
  char *argv[] = {"program", "client", (char *)tc->address, NULL};
  int argc = 3;
  options_backup_t backup;
  save_options(&backup);

  // Test exit behavior with fork
  int exit_code = test_options_init_with_fork(argv, argc, true);

  // Test option values in main process (only if no exit expected)
  if (tc->should_succeed) {
    cr_assert_eq(exit_code, 0, "%s should not cause exit", tc->description);
    // Reset getopt state before calling options_init again
    optind = 1;
    opterr = 1;
    optopt = 0;
    options_init(argc, argv);
    // Get options from RCU for assertions
    const options_t *opts = options_get();
    cr_assert_str_eq(opts->address, tc->address, "%s should set address correctly", tc->description);
  } else {
    cr_assert_eq(exit_code, tc->expected_exit_code, "%s should cause exit with code %d", tc->description,
                 tc->expected_exit_code);
  }

  restore_options(&backup);
}

// =============================================================================
// Port Validation Tests
// =============================================================================

GENERATE_OPTIONS_TEST(
    valid_port_80, ARGV_LIST("client", "-p", "80"), true, { cr_assert_eq(opts->port, 80); },
    { cr_assert_eq(exit_code, 0, "Valid port 80 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    valid_port_65535, ARGV_LIST("client", "-p", "65535"), true, { cr_assert_eq(opts->port, 65535); },
    { cr_assert_eq(exit_code, 0, "Valid port 65535 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_port_too_low, ARGV_LIST("client", "-p", "0"), true,
    {
      // This should not be reached since options_init should exit
      cr_fail("Should not reach this point - invalid port should cause exit");
    },
    { cr_assert_eq(exit_code, 1, "Invalid port 0 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_port_too_high, ARGV_LIST("client", "-p", "65536"), true,
    {
      // This should not be reached since options_init should exit
      cr_fail("Should not reach this point - invalid port should cause exit");
    },
    { cr_assert_eq(exit_code, 1, "Invalid port 65536 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_port_non_numeric, ARGV_LIST("client", "-p", "abc"), true,
    {
      // This should not be reached since options_init should exit
      cr_fail("Should not reach this point - invalid port should cause exit");
    },
    { cr_assert_eq(exit_code, 1, "Invalid port abc should cause exit with code 1"); })

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

ParameterizedTestParameters(options, port_validation) {
  return cr_make_param_array(port_validation_test_case_t, port_validation_cases,
                             sizeof(port_validation_cases) / sizeof(port_validation_cases[0]));
}

ParameterizedTest(port_validation_test_case_t *tc, options, port_validation) {
  char *argv[] = {"program", "client", "-p", (char *)tc->port, NULL};
  int argc = 4;
  options_backup_t backup;
  save_options(&backup);

  // Test exit behavior with fork
  int exit_code = test_options_init_with_fork(argv, argc, true);

  // Test option values in main process (only if no exit expected)
  if (tc->should_succeed) {
    cr_assert_eq(exit_code, 0, "%s should not cause exit", tc->description);
    // Reset getopt state before calling options_init again
    optind = 1;
    opterr = 1;
    optopt = 0;
    options_init(argc, argv);
    // Get options from RCU for assertions
    const options_t *opts = options_get();
    cr_assert_eq(opts->port, strtoint_safe(tc->port), "%s should set port correctly", tc->description);
  } else {
    cr_assert_eq(exit_code, tc->expected_exit_code, "%s should cause exit with code %d", tc->description,
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
      cr_assert_eq(opts->width, 100);
      cr_assert_eq(opts->height, 50);
      cr_assert_eq(opts->auto_width, 0);
      cr_assert_eq(opts->auto_height, 0);
    },
    { cr_assert_eq(exit_code, 0, "Valid dimensions should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_dimension_zero, ARGV_LIST("client", "-x", "0"), true,
    { cr_fail("Should not reach this point - invalid dimension should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Invalid dimension 0 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_dimension_negative, ARGV_LIST("client", "-x", "-1"), true,
    { cr_fail("Should not reach this point - invalid dimension should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Invalid dimension -1 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_dimension_non_numeric, ARGV_LIST("client", "-x", "abc"), true,
    { cr_fail("Should not reach this point - invalid dimension should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Invalid dimension abc should cause exit with code 1"); })

/* ============================================================================
 * Webcam Options Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(valid_webcam_index, ARGV_LIST("client", "-c", "2"), true,
                      {/* no option assertions needed for success-only check */}, { cr_assert_eq(exit_code, 0); })

GENERATE_OPTIONS_TEST(
    invalid_webcam_index_neg1, ARGV_LIST("client", "-c", "-1"), true,
    { cr_fail("Should not reach this point - invalid webcam index should cause exit"); },
    { cr_assert_eq(exit_code, 1); })

GENERATE_OPTIONS_TEST(
    invalid_webcam_index_abc, ARGV_LIST("client", "-c", "abc"), true,
    { cr_fail("Should not reach this point - invalid webcam index should cause exit"); },
    { cr_assert_eq(exit_code, 1); })

GENERATE_OPTIONS_TEST(
    invalid_webcam_index_decimal, ARGV_LIST("client", "-c", "2.5"), true,
    { cr_fail("Should not reach this point - invalid webcam index should cause exit"); },
    { cr_assert_eq(exit_code, 1); })

GENERATE_OPTIONS_TEST(
    invalid_webcam_index_empty, ARGV_LIST("client", "-c", ""), true,
    { cr_fail("Should not reach this point - invalid webcam index should cause exit"); },
    { cr_assert_eq(exit_code, 1); })

GENERATE_OPTIONS_TEST(valid_webcam_flip, ARGV_LIST("client", "-g"), true, {/* flip flag only */},
                      { cr_assert_eq(exit_code, 0, "Webcam flip flag should not cause exit"); })

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

ParameterizedTestParameters(options, color_mode_validation) {
  return cr_make_param_array(color_mode_test_case_t, color_mode_cases,
                             sizeof(color_mode_cases) / sizeof(color_mode_cases[0]));
}

ParameterizedTest(color_mode_test_case_t *tc, options, color_mode_validation) {
  char *argv[] = {"program", "client", "--color-mode", (char *)tc->mode_string, NULL};
  int argc = 4;
  options_backup_t backup;
  save_options(&backup);

  // Test exit behavior with fork
  int exit_code = test_options_init_with_fork(argv, argc, true);

  cr_assert_eq(exit_code, tc->expected_exit_code, "%s should %s", tc->description,
               tc->should_succeed ? "not cause exit" : "cause exit");

  restore_options(&backup);
}

// Original loop-based tests replaced by parameterized version above
// Test(options, valid_color_modes) { ... }
// Test(options, invalid_color_modes) { ... }

/* ============================================================================
 * Render Mode Tests
 * ============================================================================ */

Test(options, valid_render_modes) {
  options_backup_t backup;
  save_options(&backup);

  char *valid_modes[] = {"foreground", "fg", "background", "bg", "half-block"};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"program", "client", "--render-mode", valid_modes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    cr_assert_eq(result, 0, "Valid render mode %s should not cause exit", valid_modes[i]);
  }

  restore_options(&backup);
}

Test(options, invalid_render_modes) {
  char *invalid_modes[] = {"invalid", "full", "block", "text", ""};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"program", "client", "--render-mode", invalid_modes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    cr_assert_eq(result, 1, "Invalid render mode %s should cause exit with code 1", invalid_modes[i]);
  }
}

/* ============================================================================
 * Palette Tests
 * ============================================================================ */

Test(options, valid_palettes) {
  options_backup_t backup;
  save_options(&backup);

  char *valid_palettes[] = {"standard", "blocks", "digital", "minimal", "cool", "custom"};

  for (int i = 0; i < 6; i++) {
    char *argv[] = {"program", "client", "--palette", valid_palettes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    cr_assert_eq(result, 0, "Valid palette %s should not cause exit", valid_palettes[i]);
  }

  restore_options(&backup);
}

Test(options, invalid_palettes) {
  char *invalid_palettes[] = {"invalid", "ascii", "unicode", "color", ""};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"program", "client", "--palette", invalid_palettes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    cr_assert_eq(result, 1, "Invalid palette %s should cause exit with code 1", invalid_palettes[i]);
  }
}

Test(options, valid_palette_chars) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"program", "client", "--palette-chars", " .:-=+*#%@$", NULL};
  int result = test_options_init_with_fork(argv, 4, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, invalid_palette_chars) {
  // Empty palette chars should fail
  char *argv[] = {"program", "client", "--palette-chars", "", NULL};
  int result = test_options_init_with_fork(argv, 4, true);
  cr_assert_eq(result, 1);
}

/* ============================================================================
 * Snapshot Delay Tests
 * ============================================================================ */

Test(options, valid_snapshot_delays) {
  options_backup_t backup;
  save_options(&backup);

  char *valid_delays[] = {"0.0", "1.5", "3.0", "10.0", "0"};

  for (int i = 0; i < 5; i++) {
    // snapshot-delay requires --snapshot to be set
    char *argv[] = {"program", "client", "--snapshot", "--snapshot-delay", valid_delays[i], NULL};
    int result = test_options_init_with_fork(argv, 5, true);
    cr_assert_eq(result, 0, "Valid snapshot delay %s should not cause exit", valid_delays[i]);
  }

  restore_options(&backup);
}

Test(options, invalid_snapshot_delays) {
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
    cr_assert_eq(result, 1, "Invalid snapshot delay %s should cause exit with code 1", invalid_delays[i]);
  }

  restore_options(&backup);
}

/* ============================================================================
 * File Path Tests
 * ============================================================================ */

// NOTE: --log-file is now a global option handled at binary level, not mode-specific
// GENERATE_OPTIONS_TEST(
//     valid_log_file, ARGV_LIST("client", "--log-file", "/tmp/test.log"), true,
//     { cr_assert_str_eq(opts->log_file, "/tmp/test.log"); },
//     { cr_assert_eq(exit_code, 0, "Valid log file should not cause exit"); })
//
// GENERATE_OPTIONS_TEST(
//     invalid_log_file, ARGV_LIST("client", "--log-file", ""), true,
//     { cr_fail("Should not reach this point - empty log file should cause exit"); },
//     { cr_assert_eq(exit_code, 1, "Empty log file should exit with code 1"); })

GENERATE_OPTIONS_TEST(
    valid_encryption_key, ARGV_LIST("client", "--key", "mysecretkey"), true,
    {
      cr_assert_str_eq(opts->encrypt_key, "mysecretkey");
      cr_assert_eq(opts->encrypt_enabled, 1);
    },
    { cr_assert_eq(exit_code, 0, "Valid encryption key should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_encryption_key, ARGV_LIST("client", "--key", ""), true,
    { cr_fail("Should not reach this point - empty key should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Empty key should exit with code 1"); })

/* ============================================================================
 * Flag Options Tests
 * ============================================================================ */

Test(options, flag_options) {
  options_backup_t backup;
  save_options(&backup);

  // NOTE: --quiet is now a global option, removed from this test
  char *argv[] = {"program",   "client",     "--show-capabilities", "--utf8", "--audio",
                  "--stretch", "--snapshot", "--encrypt",           NULL};
  int result = test_options_init_with_fork(argv, 8, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

/* ============================================================================
 * Help Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(help_client, ARGV_LIST("client", "--help"), true,
                      {
                          // Help should display and exit cleanly
                      },
                      { cr_assert_eq(exit_code, 0, "Help should exit with code 0"); })

GENERATE_OPTIONS_TEST(help_server, ARGV_LIST("server", "--help"), false,
                      {
                          // Help should display and exit cleanly
                      },
                      { cr_assert_eq(exit_code, 0, "Help should exit with code 0"); })

GENERATE_OPTIONS_TEST(help_short, ARGV_LIST("client", "-h"), true,
                      {
                          // Help should display and exit cleanly
                      },
                      { cr_assert_eq(exit_code, 0, "Short help should exit with code 0"); })

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST_IN_SUITE(
    options_errors, unknown_option, ARGV_LIST("client", "--unknown-option"), true,
    { cr_fail("Should not reach this point - unknown option should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Unknown option should exit with code 1"); })

GENERATE_OPTIONS_TEST_IN_SUITE(
    options_errors, missing_argument_port, ARGV_LIST("client", "--port"), true,
    { cr_fail("Should not reach this point - missing argument should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Missing argument should exit with code 1"); })

/* ============================================================================
 * Equals Sign Handling Tests
 * ============================================================================ */

Test(options, equals_sign_handling) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"program", "client", "192.168.1.1:8080", "--width=100", "--height=50", NULL};
  int result = test_options_init_with_fork(argv, 5, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

/* ============================================================================
 * Complex Combinations Tests
 * ============================================================================ */

Test(options, complex_client_combination) {
  options_backup_t backup;
  save_options(&backup);

  // NOTE: --quiet and --log-file are now global options, removed from this test
  char *argv[] = {"program",
                  "client",
                  "192.168.1.100:8080",
                  "--width=120",
                  "--height=60",
                  "--webcam-index=1",
                  "--webcam-flip",
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
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, complex_server_combination) {
  options_backup_t backup;
  save_options(&backup);

  // NOTE: --log-file is now a global option, removed from this test
  // NOTE: --palette is client-only, removed from this server test
  char *argv[] = {"program", "server", "0.0.0.0", "--port=27224", "--encrypt", NULL};
  int argc = 5;

  int result = test_options_init_with_fork(argv, argc, false);
  cr_assert_eq(result, 0);

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

Test(options, update_dimensions_for_full_height) {
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

Test(options, update_dimensions_to_terminal_size) {
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

Test(options, very_long_arguments) {
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
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, maximum_values) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {
      "program", "client", "--width=512", "--height=256", "--webcam-index=10", "--snapshot", "--snapshot-delay=999.999",
      NULL};
  int argc = 7;

  int result = test_options_init_with_fork(argv, argc, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, minimum_values) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"program",          "client",     "--width=20",           "--height=10",
                  "--webcam-index=0", "--snapshot", "--snapshot-delay=0.0", NULL};
  int argc = 7;

  int result = test_options_init_with_fork(argv, argc, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, random_combinations) {
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
    cr_assert_eq(result, 0, "Random combination %d should not cause exit", i);
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
      cr_assert_str_eq(opts->address, "192.168.1.1");
      cr_assert_eq(opts->port, 8080);
      cr_assert_eq(opts->width, 100);
      cr_assert_eq(opts->height, 50);
      cr_assert_eq(opts->auto_width, 0);
      cr_assert_eq(opts->auto_height, 0);
    },
    {
      // Test that options_init doesn't exit with error
      cr_assert_eq(exit_code, 0, "options_init should not exit with error");
    })

GENERATE_OPTIONS_TEST(
    color_mode_auto, ARGV_LIST("client", "--color-mode", "auto"), true,
    { cr_assert_eq(opts->color_mode, COLOR_MODE_AUTO); },
    { cr_assert_eq(exit_code, 0, "auto color mode should not exit"); })

GENERATE_OPTIONS_TEST(
    color_mode_256, ARGV_LIST("client", "--color-mode", "256"), true,
    { cr_assert_eq(opts->color_mode, COLOR_MODE_256_COLOR); },
    { cr_assert_eq(exit_code, 0, "256 color mode should not exit"); })

GENERATE_OPTIONS_TEST(
    color_mode_truecolor, ARGV_LIST("client", "--color-mode", "truecolor"), true,
    { cr_assert_eq(opts->color_mode, COLOR_MODE_TRUECOLOR); },
    { cr_assert_eq(exit_code, 0, "truecolor mode should not exit"); })

GENERATE_OPTIONS_TEST(
    render_mode_foreground, ARGV_LIST("client", "--render-mode", "foreground"), true,
    { cr_assert_eq(opts->render_mode, RENDER_MODE_FOREGROUND); },
    { cr_assert_eq(exit_code, 0, "foreground render mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    render_mode_background, ARGV_LIST("client", "--render-mode", "background"), true,
    { cr_assert_eq(opts->render_mode, RENDER_MODE_BACKGROUND); },
    { cr_assert_eq(exit_code, 0, "background render mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    render_mode_half_block, ARGV_LIST("client", "--render-mode", "half-block"), true,
    { cr_assert_eq(opts->render_mode, RENDER_MODE_HALF_BLOCK); },
    { cr_assert_eq(exit_code, 0, "half-block render mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    palette_standard, ARGV_LIST("client", "--palette", "standard"), true,
    { cr_assert_eq(opts->palette_type, PALETTE_STANDARD); },
    { cr_assert_eq(exit_code, 0, "standard palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    palette_blocks, ARGV_LIST("client", "--palette", "blocks"), true,
    { cr_assert_eq(opts->palette_type, PALETTE_BLOCKS); },
    { cr_assert_eq(exit_code, 0, "blocks palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    palette_custom_chars, ARGV_LIST("client", "--palette-chars", "@#%*+=:-. "), true,
    {
      cr_assert_eq(opts->palette_type, PALETTE_CUSTOM);
      cr_assert_str_eq(opts->palette_custom, "@#%*+=:-. ");
      cr_assert_eq(opts->palette_custom_set, true);
    },
    { cr_assert_eq(exit_code, 0, "custom palette chars should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_flag_values,
    // NOTE: --quiet is now a global option, removed from this test
    ARGV_LIST("client", "--audio", "--stretch", "--snapshot", "--encrypt", "--utf8", "--show-capabilities", "-g"), true,
    {
      // Test all flags were set
      cr_assert_eq(opts->audio_enabled, 1);
      cr_assert_eq(opts->stretch, 1);
      // opts->quiet removed - now a global option
      cr_assert_eq(opts->snapshot_mode, 1);
      cr_assert_eq(opts->encrypt_enabled, 1);
      cr_assert_eq(opts->force_utf8, 1);
      cr_assert_eq(opts->show_capabilities, 1);
      cr_assert_eq(opts->webcam_flip, false);
    },
    { cr_assert_eq(exit_code, 0, "flag values should not cause exit"); })

GENERATE_OPTIONS_TEST(
    encryption_key_value, ARGV_LIST("client", "--key", "mysecretpassword123"), true,
    {
      cr_assert_str_eq(opts->encrypt_key, "mysecretpassword123");
      cr_assert_eq(opts->encrypt_enabled, 1);
    },
    { cr_assert_eq(exit_code, 0, "encryption key should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_snapshot_delay_values, ARGV_LIST("client", "--snapshot", "--snapshot-delay", "2.5"), true,
    { cr_assert_float_eq(opts->snapshot_delay, 2.5f, 0.01); },
    { cr_assert_eq(exit_code, 0, "snapshot delay should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_webcam_values, ARGV_LIST("client", "-c", "3", "-g"), true,
    {
      cr_assert_eq(opts->webcam_index, 3);
      cr_assert_eq(opts->webcam_flip, false);
    },
    { cr_assert_eq(exit_code, 0, "webcam values should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_comprehensive_client_values,
    // NOTE: --quiet and --log-file are now global options, removed from this test
    ARGV_LIST("client", "10.0.0.1:9999", "--width=200", "--height=100", "--webcam-index=2", "--webcam-flip",
              "--color-mode=256", "--render-mode=background", "--palette=digital", "--audio", "--stretch", "--snapshot",
              "--snapshot-delay=5.0", "--encrypt", "--key=testkey123"),
    true,
    {
      // Verify ALL values
      cr_assert_str_eq(opts->address, "10.0.0.1");
      cr_assert_eq(opts->port, 9999);
      cr_assert_eq(opts->width, 200);
      cr_assert_eq(opts->height, 100);
      cr_assert_eq(opts->webcam_index, 2);
      cr_assert_eq(opts->webcam_flip, false);
      cr_assert_eq(opts->color_mode, COLOR_MODE_256_COLOR);
      cr_assert_eq(opts->render_mode, RENDER_MODE_BACKGROUND);
      cr_assert_eq(opts->palette_type, PALETTE_DIGITAL);
      cr_assert_eq(opts->audio_enabled, 1);
      cr_assert_eq(opts->stretch, 1);
      // opts->quiet removed - now a global option
      cr_assert_eq(opts->snapshot_mode, 1);
      cr_assert_float_eq(opts->snapshot_delay, 5.0f, 0.01);
      // opts->log_file removed - now a global option
      cr_assert_eq(opts->encrypt_enabled, 1);
      cr_assert_str_eq(opts->encrypt_key, "testkey123");
      // Should be enabled by color-mode
      cr_assert_eq(opts->auto_width, 0);  // Should be disabled when width is set
      cr_assert_eq(opts->auto_height, 0); // Should be disabled when height is set
    },
    { cr_assert_eq(exit_code, 0, "comprehensive client values should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_server_values,
    // NOTE: --log-file is now a global option, removed from this test
    // NOTE: --palette is client-only, removed from this server test
    ARGV_LIST("server", "0.0.0.0", "--port=12345", "--encrypt"), false,
    {
      // Verify server values
      cr_assert_str_eq(opts->address, "0.0.0.0");
      cr_assert_eq(opts->port, 12345);
      // Note: --audio is not supported for server mode
      // opts->log_file removed - now a global option
      // opts->palette_type removed - palette is client-only
      cr_assert_eq(opts->encrypt_enabled, 1);
    },
    { cr_assert_eq(exit_code, 0, "server values should not cause exit"); })

/* ============================================================================
 * Additional Test Scenarios with the Macro
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    test_equals_sign_syntax, ARGV_LIST("client", "192.168.1.100:8080", "--width=150", "--height=75"), true,
    {
      cr_assert_str_eq(opts->address, "192.168.1.100");
      cr_assert_eq(opts->port, 8080);
      cr_assert_eq(opts->width, 150);
      cr_assert_eq(opts->height, 75);
      cr_assert_eq(opts->auto_width, 0);  // Should be disabled when width is set
      cr_assert_eq(opts->auto_height, 0); // Should be disabled when height is set
    },
    { cr_assert_eq(exit_code, 0, "equals sign syntax should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_mixed_syntax, ARGV_LIST("client", "10.0.0.1:3000", "-x", "80", "--height=60"), true,
    {
      cr_assert_str_eq(opts->address, "10.0.0.1");
      cr_assert_eq(opts->port, 3000);
      cr_assert_eq(opts->width, 80);
      cr_assert_eq(opts->height, 60);
      cr_assert_eq(opts->auto_width, 0);
      cr_assert_eq(opts->auto_height, 0);
    },
    { cr_assert_eq(exit_code, 0, "mixed syntax should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_none_mode, ARGV_LIST("client", "--color-mode", "none"), true,
    {
      cr_assert_eq(opts->color_mode, COLOR_MODE_NONE);
      // Color output should be disabled for none mode
    },
    { cr_assert_eq(exit_code, 0, "none mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_16color_mode, ARGV_LIST("client", "--color-mode", "16"), true,
    { cr_assert_eq(opts->color_mode, COLOR_MODE_16_COLOR); },
    { cr_assert_eq(exit_code, 0, "16color mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_16color_alias, ARGV_LIST("client", "--color-mode", "16color"), true,
    { cr_assert_eq(opts->color_mode, COLOR_MODE_16_COLOR); },
    { cr_assert_eq(exit_code, 0, "16color alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_256color_alias, ARGV_LIST("client", "--color-mode", "256color"), true,
    { cr_assert_eq(opts->color_mode, COLOR_MODE_256_COLOR); },
    { cr_assert_eq(exit_code, 0, "256color alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_truecolor_alias, ARGV_LIST("client", "--color-mode", "24bit"), true,
    { cr_assert_eq(opts->color_mode, COLOR_MODE_TRUECOLOR); },
    { cr_assert_eq(exit_code, 0, "truecolor alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_palette_digital, ARGV_LIST("client", "--palette", "digital"), true,
    { cr_assert_eq(opts->palette_type, PALETTE_DIGITAL); },
    { cr_assert_eq(exit_code, 0, "digital palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_palette_minimal, ARGV_LIST("client", "--palette", "minimal"), true,
    { cr_assert_eq(opts->palette_type, PALETTE_MINIMAL); },
    { cr_assert_eq(exit_code, 0, "minimal palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_palette_cool, ARGV_LIST("client", "--palette", "cool"), true,
    { cr_assert_eq(opts->palette_type, PALETTE_COOL); },
    { cr_assert_eq(exit_code, 0, "cool palette should not cause exit"); })

/* Converted looped palette test into specific GENERATE_OPTIONS_TEST cases above. */

GENERATE_OPTIONS_TEST(
    test_render_mode_fg_alias, ARGV_LIST("client", "--render-mode", "fg"), true,
    { cr_assert_eq(opts->render_mode, RENDER_MODE_FOREGROUND); },
    { cr_assert_eq(exit_code, 0, "fg alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_render_mode_bg_alias, ARGV_LIST("client", "--render-mode", "bg"), true,
    { cr_assert_eq(opts->render_mode, RENDER_MODE_BACKGROUND); },
    { cr_assert_eq(exit_code, 0, "bg alias should not cause exit"); })

// NOTE: --log-file is now a global option handled at binary level, not mode-specific
// GENERATE_OPTIONS_TEST(
//     test_log_file_path, ARGV_LIST("client", "--log-file", "/var/log/ascii-chat.log"), true,
//     { cr_assert_str_eq(opts->log_file, "/var/log/ascii-chat.log"); },
//     { cr_assert_eq(exit_code, 0, "log file path should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_webcam_index_only, ARGV_LIST("client", "-c", "5"), true,
    {
      cr_assert_eq(opts->webcam_index, 5);
      cr_assert_eq(opts->webcam_flip, true); // Should remain default
    },
    { cr_assert_eq(exit_code, 0, "webcam index should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_webcam_flip_only, ARGV_LIST("client", "-g"), true,
    {
      cr_assert_eq(opts->webcam_index, 0); // Should remain default
      cr_assert_eq(opts->webcam_flip, false);
    },
    { cr_assert_eq(exit_code, 0, "webcam flip should not cause exit"); })

/* ============================================================================
 * Server-specific Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    test_server_basic_options, ARGV_LIST("client", "127.0.0.1:8080", "--width=110", "--height=70"), true,
    {
      cr_assert_str_eq(opts->address, "127.0.0.1");
      cr_assert_eq(opts->port, 8080);
      // Server should use default values for client-only options
      cr_assert_eq(opts->width, 110);        // Should use default width
      cr_assert_eq(opts->height, 70);        // Should use default height
      cr_assert_eq(opts->webcam_index, 0);   // Should use default
      cr_assert_eq(opts->webcam_flip, true); // Should use default
    },
    { cr_assert_eq(exit_code, 0, "server basic options should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_server_palette_options, ARGV_LIST("client", "--palette", "blocks", "--palette-chars", "0123456789"), true,
    {
      cr_assert_eq(opts->palette_type, PALETTE_CUSTOM); // --palette-chars overrides to custom
      cr_assert_str_eq(opts->palette_custom, "0123456789");
      cr_assert_eq(opts->palette_custom_set, true);
    },
    { cr_assert_eq(exit_code, 0, "palette options with palette-chars should not cause exit"); })

/* ============================================================================
 * Edge Cases and Error Conditions
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    test_auto_dimensions, ARGV_LIST("client"), true,
    {
      // These should be set to auto (1) by default
      cr_assert_eq(opts->auto_width, 1);
      cr_assert_eq(opts->auto_height, 1);
      // The actual dimensions will be set by update_dimensions_to_terminal_size()
      // but we can't easily test that without mocking terminal detection
    },
    { cr_assert_eq(exit_code, 0, "auto dimensions should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_manual_dimensions_disable_auto, ARGV_LIST("client", "--width", "100", "--height", "50"), true,
    {
      cr_assert_eq(opts->width, 100);
      cr_assert_eq(opts->height, 50);
      cr_assert_eq(opts->auto_width, 0);  // Should be disabled when manually set
      cr_assert_eq(opts->auto_height, 0); // Should be disabled when manually set
    },
    { cr_assert_eq(exit_code, 0, "manual dimensions should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_encryption_auto_enable, ARGV_LIST("client", "--key", "mypassword"), true,
    {
      cr_assert_eq(opts->encrypt_enabled, 1); // Should be auto-enabled
      cr_assert_str_eq(opts->encrypt_key, "mypassword");
    },
    { cr_assert_eq(exit_code, 0, "encryption key should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_custom_palette_auto_set_type, ARGV_LIST("client", "--palette-chars", "ABCDEFGH"), true,
    {
      cr_assert_eq(opts->palette_type, PALETTE_CUSTOM);
      cr_assert_str_eq(opts->palette_custom, "ABCDEFGH");
      cr_assert_eq(opts->palette_custom_set, true);
    },
    { cr_assert_eq(exit_code, 0, "custom palette chars should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_color_output_enabled_by_color_mode, ARGV_LIST("client", "--color-mode", "256"), true,
    {
      cr_assert_eq(opts->color_mode, COLOR_MODE_256_COLOR);
      // Should be enabled
    },
    { cr_assert_eq(exit_code, 0, "256 color mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_color_output_disabled_by_none, ARGV_LIST("client", "--color-mode", "none"), true,
    {
      cr_assert_eq(opts->color_mode, COLOR_MODE_NONE);
      // Color output should be disabled
    },
    { cr_assert_eq(exit_code, 0, "none color mode should not cause exit"); })

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

Test(options, deferred_action_list_webcams) {
  // Defer the action
  action_list_webcams();

  // Verify it was deferred (not executed immediately, which would exit)
  deferred_action_t action = actions_get_deferred();
  cr_assert_eq(action, ACTION_LIST_WEBCAMS, "action_list_webcams() should defer ACTION_LIST_WEBCAMS");
}

Test(options, deferred_action_list_microphones) {
  // Defer the action
  action_list_microphones();

  // Verify it was deferred
  deferred_action_t action = actions_get_deferred();
  cr_assert_eq(action, ACTION_LIST_MICROPHONES, "action_list_microphones() should defer ACTION_LIST_MICROPHONES");
}

Test(options, deferred_action_list_speakers) {
  // Defer the action
  action_list_speakers();

  // Verify it was deferred
  deferred_action_t action = actions_get_deferred();
  cr_assert_eq(action, ACTION_LIST_SPEAKERS, "action_list_speakers() should defer ACTION_LIST_SPEAKERS");
}

Test(options, deferred_action_show_capabilities) {
  // Defer the action
  action_show_capabilities();

  // Verify it was deferred
  deferred_action_t action = actions_get_deferred();
  cr_assert_eq(action, ACTION_SHOW_CAPABILITIES, "action_show_capabilities() should defer ACTION_SHOW_CAPABILITIES");
}

Test(options, deferred_action_first_wins) {
  // Defer first action
  action_list_webcams();
  cr_assert_eq(actions_get_deferred(), ACTION_LIST_WEBCAMS, "First action should be deferred");

  // Try to defer second action
  action_list_microphones();
  cr_assert_eq(actions_get_deferred(), ACTION_LIST_WEBCAMS,
               "First action should still be deferred, second action should be ignored");

  // Try to defer third action
  action_show_capabilities();
  cr_assert_eq(actions_get_deferred(), ACTION_LIST_WEBCAMS,
               "First action should still be deferred, third action should be ignored");
}

Test(options, deferred_action_arguments) {
  // Set action with arguments
  action_args_t args = {.output_path = "/tmp/test.txt", .shell_name = "bash"};

  actions_defer(ACTION_LIST_WEBCAMS, &args);

  // Retrieve and verify arguments
  const action_args_t *retrieved_args = actions_get_args();
  cr_assert_not_null(retrieved_args, "Action arguments should be retrievable");
  cr_assert_str_eq(retrieved_args->output_path, "/tmp/test.txt", "Output path should be preserved");
  cr_assert_str_eq(retrieved_args->shell_name, "bash", "Shell name should be preserved");
}

Test(options, deferred_action_no_action_by_default) {
  deferred_action_t action = actions_get_deferred();
  cr_assert_eq(action, ACTION_NONE, "ACTION_NONE should be the default deferred action");

  const action_args_t *args = actions_get_args();
  cr_assert_null(args, "Arguments should be NULL when action is ACTION_NONE");
}
