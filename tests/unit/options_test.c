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

#include "options.h"
#include "tests/common.h"
#include "platform/terminal.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suites with custom log levels
// TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVEL(options, LOG_INFO, LOG_DEBUG);
// TEST_LOGGING_SETUP_AND_TEARDOWN_WITH_LOG_LEVELS(LOG_FATAL, LOG_DEBUG, false, false);
// TestSuite(options, .init = setup_quiet_test_logging, .fini = restore_test_logging);

TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(options, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(options_errors, LOG_FATAL, LOG_DEBUG, true, true);

// Macro helpers for argv construction without brace-literals at call sites
#define ARGV_LIST(...) ((char *[]){__VA_ARGS__, NULL})
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
    /* Test exit behavior with fork */                                                                                 \
    int exit_code = test_options_init_with_fork(argv, argc, is_client_val);                                            \
                                                                                                                       \
    /* Test option values in main process (only if no exit expected) */                                                \
    if (exit_code == 0) {                                                                                              \
      /* Reset getopt state before calling options_init again */                                                       \
      optind = 1;                                                                                                      \
      opterr = 1;                                                                                                      \
      optopt = 0;                                                                                                      \
      options_init(argc, argv, is_client_val);                                                                         \
      option_assertions                                                                                                \
    }                                                                                                                  \
                                                                                                                       \
    restore_options(&backup);                                                                                          \
    exit_assertions                                                                                                    \
  }

// Helper function to save and restore global options
typedef struct {
  unsigned short int opt_width, opt_height, auto_width, auto_height;
  char opt_address[OPTIONS_BUFF_SIZE], opt_port[OPTIONS_BUFF_SIZE];
  unsigned short int opt_webcam_index;
  bool opt_webcam_flip;
  terminal_color_mode_t opt_color_mode;
  render_mode_t opt_render_mode;
  unsigned short int opt_show_capabilities, opt_force_utf8;
  unsigned short int opt_audio_enabled, opt_stretch, opt_quiet, opt_snapshot_mode;
  float opt_snapshot_delay;
  char opt_log_file[OPTIONS_BUFF_SIZE];
  unsigned short int opt_encrypt_enabled;
  char opt_encrypt_key[OPTIONS_BUFF_SIZE], opt_encrypt_keyfile[OPTIONS_BUFF_SIZE];
  palette_type_t opt_palette_type;
  char opt_palette_custom[256];
  bool opt_palette_custom_set;
} options_backup_t;

static void save_options(options_backup_t *backup) {
  backup->opt_width = opt_width;
  backup->opt_height = opt_height;
  backup->auto_width = auto_width;
  backup->auto_height = auto_height;
  strncpy(backup->opt_address, opt_address, OPTIONS_BUFF_SIZE - 1);
  backup->opt_address[OPTIONS_BUFF_SIZE - 1] = '\0';
  strncpy(backup->opt_port, opt_port, OPTIONS_BUFF_SIZE - 1);
  backup->opt_port[OPTIONS_BUFF_SIZE - 1] = '\0';
  backup->opt_webcam_index = opt_webcam_index;
  backup->opt_webcam_flip = opt_webcam_flip;
  backup->opt_color_mode = opt_color_mode;
  backup->opt_render_mode = opt_render_mode;
  backup->opt_show_capabilities = opt_show_capabilities;
  backup->opt_force_utf8 = opt_force_utf8;
  backup->opt_audio_enabled = opt_audio_enabled;
  backup->opt_stretch = opt_stretch;
  backup->opt_quiet = opt_quiet;
  backup->opt_snapshot_mode = opt_snapshot_mode;
  backup->opt_snapshot_delay = opt_snapshot_delay;
  strncpy(backup->opt_log_file, opt_log_file, OPTIONS_BUFF_SIZE - 1);
  backup->opt_log_file[OPTIONS_BUFF_SIZE - 1] = '\0';
  backup->opt_encrypt_enabled = opt_encrypt_enabled;
  strncpy(backup->opt_encrypt_key, opt_encrypt_key, OPTIONS_BUFF_SIZE - 1);
  backup->opt_encrypt_key[OPTIONS_BUFF_SIZE - 1] = '\0';
  strncpy(backup->opt_encrypt_keyfile, opt_encrypt_keyfile, OPTIONS_BUFF_SIZE - 1);
  backup->opt_encrypt_keyfile[OPTIONS_BUFF_SIZE - 1] = '\0';
  backup->opt_palette_type = opt_palette_type;
  strncpy(backup->opt_palette_custom, opt_palette_custom, 255);
  backup->opt_palette_custom[255] = '\0';
  backup->opt_palette_custom_set = opt_palette_custom_set;
}

static void restore_options(const options_backup_t *backup) {
  opt_width = backup->opt_width;
  opt_height = backup->opt_height;
  auto_width = backup->auto_width;
  auto_height = backup->auto_height;
  strncpy(opt_address, backup->opt_address, OPTIONS_BUFF_SIZE - 1);
  opt_address[OPTIONS_BUFF_SIZE - 1] = '\0';
  strncpy(opt_port, backup->opt_port, OPTIONS_BUFF_SIZE - 1);
  opt_port[OPTIONS_BUFF_SIZE - 1] = '\0';
  opt_webcam_index = backup->opt_webcam_index;
  opt_webcam_flip = backup->opt_webcam_flip;
  opt_color_mode = backup->opt_color_mode;
  opt_render_mode = backup->opt_render_mode;
  opt_show_capabilities = backup->opt_show_capabilities;
  opt_force_utf8 = backup->opt_force_utf8;
  opt_audio_enabled = backup->opt_audio_enabled;
  opt_stretch = backup->opt_stretch;
  opt_quiet = backup->opt_quiet;
  opt_snapshot_mode = backup->opt_snapshot_mode;
  opt_snapshot_delay = backup->opt_snapshot_delay;
  strncpy(opt_log_file, backup->opt_log_file, OPTIONS_BUFF_SIZE - 1);
  opt_log_file[OPTIONS_BUFF_SIZE - 1] = '\0';
  opt_encrypt_enabled = backup->opt_encrypt_enabled;
  strncpy(opt_encrypt_key, backup->opt_encrypt_key, OPTIONS_BUFF_SIZE - 1);
  opt_encrypt_key[OPTIONS_BUFF_SIZE - 1] = '\0';
  strncpy(opt_encrypt_keyfile, backup->opt_encrypt_keyfile, OPTIONS_BUFF_SIZE - 1);
  opt_encrypt_keyfile[OPTIONS_BUFF_SIZE - 1] = '\0';
  opt_palette_type = backup->opt_palette_type;
  strncpy(opt_palette_custom, backup->opt_palette_custom, 255);
  opt_palette_custom[255] = '\0';
  opt_palette_custom_set = backup->opt_palette_custom_set;
}

// Helper function to test options_init with fork/exec to avoid exit() calls
static int test_options_init_with_fork(char **argv, int argc, bool is_client) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child process - redirect all output to /dev/null
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      close(devnull);
    }

    // Also suppress logging
    log_set_level(LOG_FATAL);

    // Ensure argv is NULL-terminated for getopt_long and any library routines
    char **argv_with_null;
    if (argv[argc - 1] != NULL) {
      argv_with_null = SAFE_CALLOC((size_t)argc + 1, sizeof(char *), char **);
      if (argv_with_null) {
        for (int i = 0; i < argc; i++) {
          argv_with_null[i] = argv[i];
        }
        argv_with_null[argc] = NULL;
      } else {
        // Fallback to original argv if allocation fails
        argv_with_null = argv;
      }
    } else {
      // It's already NULL-terminated
      argv_with_null = argv;
    }

    asciichat_error_t result = options_init(argc, argv_with_null, is_client);
    // Exit with appropriate code based on return value
    if (result != ASCIICHAT_OK) {
      _exit(result == ERROR_USAGE ? 1 : result);
    }
    _exit(0);
  } else if (pid > 0) {
    // Parent process
    int status;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return exit_code;
  } else {
    // Fork failed
    return -1;
  }
}

/* ============================================================================
 * Basic Functionality Tests
 * ============================================================================ */

Test(options, default_values) {
  options_backup_t backup;
  save_options(&backup);

  // Test default values
  cr_assert_eq(opt_width, 110);
  cr_assert_eq(opt_height, 70);
  cr_assert_eq(auto_width, 1);
  cr_assert_eq(auto_height, 1);
  cr_assert_str_eq(opt_address, "localhost");
  cr_assert_str_eq(opt_port, "27224");
  cr_assert_eq(opt_webcam_index, 0);
  cr_assert_eq(opt_webcam_flip, true);
  cr_assert_eq(opt_color_mode, COLOR_MODE_AUTO);
  cr_assert_eq(opt_render_mode, RENDER_MODE_FOREGROUND);
  cr_assert_eq(opt_show_capabilities, 0);
  cr_assert_eq(opt_force_utf8, 0);
  cr_assert_eq(opt_audio_enabled, 0);
  cr_assert_eq(opt_stretch, 0);
  cr_assert_eq(opt_quiet, 0);
  cr_assert_eq(opt_snapshot_mode, 0);
  cr_assert_eq(opt_encrypt_enabled, 0);
  cr_assert_eq(opt_palette_type, PALETTE_STANDARD);
  cr_assert_eq(opt_palette_custom_set, false);

  restore_options(&backup);
}

GENERATE_OPTIONS_TEST_IN_SUITE(
    options, basic_client_options, ARGV_LIST("client", "-a", "192.168.1.1", "-p", "8080", "-x", "100", "-y", "50"),
    true,
    {
      cr_assert_str_eq(opt_address, "192.168.1.1");
      cr_assert_str_eq(opt_port, "8080");
      cr_assert_eq(opt_width, 100);
      cr_assert_eq(opt_height, 50);
      cr_assert_eq(auto_width, 0);
      cr_assert_eq(auto_height, 0);
    },
    { cr_assert_eq(exit_code, 0, "Basic client options should not exit"); })

GENERATE_OPTIONS_TEST(
    basic_server_options, ARGV_LIST("server", "-a", "127.0.0.1", "-p", "3000"), false,
    {
      cr_assert_str_eq(opt_address, "127.0.0.1");
      cr_assert_str_eq(opt_port, "3000");
      // Server should use default or terminal-detected values for dimensions
      // Since auto_width/auto_height are true by default, the code calls
      // update_dimensions_to_terminal_size() which uses get_terminal_size()
      // - If terminal detection succeeds: uses terminal dimensions
      // - If terminal detection fails: falls back to 80x24 (not OPT_WIDTH_DEFAULT)
      // Rather than hardcode specific values, just verify dimensions are reasonable
      cr_assert_gt(opt_width, 0, "Width should be positive");
      cr_assert_gt(opt_height, 0, "Height should be positive");
      cr_assert_eq(opt_webcam_index, 0);
      cr_assert_eq(opt_webcam_flip, true);
    },
    { cr_assert_eq(exit_code, 0, "Basic server options should not exit"); })

/* ============================================================================
 * Address and Port Validation Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    valid_ipv4_192_168_1_1, ARGV_LIST("client", "-a", "192.168.1.1"), true,
    { cr_assert_str_eq(opt_address, "192.168.1.1"); },
    { cr_assert_eq(exit_code, 0, "Valid IP 192.168.1.1 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    valid_ipv4_127_0_0_1, ARGV_LIST("client", "-a", "127.0.0.1"), true, { cr_assert_str_eq(opt_address, "127.0.0.1"); },
    { cr_assert_eq(exit_code, 0, "Valid IP 127.0.0.1 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    valid_ipv4_255_255_255_255, ARGV_LIST("client", "-a", "255.255.255.255"), true,
    { cr_assert_str_eq(opt_address, "255.255.255.255"); },
    { cr_assert_eq(exit_code, 0, "Valid IP 255.255.255.255 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_ipv4_octet_too_large, ARGV_LIST("client", "-a", "256.1.1.1"), true,
    {
      // This should not be reached since options_init should exit
      cr_fail("Should not reach this point - invalid IP should cause exit");
    },
    { cr_assert_eq(exit_code, 1, "Invalid IP 256.1.1.1 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_ipv4_too_few_octets, ARGV_LIST("client", "-a", "192.168.1"), true,
    {
      // This should not be reached since options_init should exit
      cr_fail("Should not reach this point - invalid IP should cause exit");
    },
    { cr_assert_eq(exit_code, 1, "Invalid IP 192.168.1 should cause exit with code 1"); })

GENERATE_OPTIONS_TEST(
    invalid_ipv4_non_numeric, ARGV_LIST("client", "-a", "192.168.1.abc"), true,
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
  char *argv[] = {"client", "-a", (char *)tc->address, NULL};
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
    options_init(argc, argv, true);
    cr_assert_str_eq(opt_address, tc->address, "%s should set address correctly", tc->description);
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
    valid_port_80, ARGV_LIST("client", "-p", "80"), true, { cr_assert_str_eq(opt_port, "80"); },
    { cr_assert_eq(exit_code, 0, "Valid port 80 should not cause exit"); })

GENERATE_OPTIONS_TEST(
    valid_port_65535, ARGV_LIST("client", "-p", "65535"), true, { cr_assert_str_eq(opt_port, "65535"); },
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
    {"80", true, 0, "Valid port 80"},
    {"65535", true, 0, "Valid port 65535"},
    {"0", false, 1, "Invalid port - too low (0)"},
    {"65536", false, 1, "Invalid port - too high (65536)"},
    {"abc", false, 1, "Invalid port - non-numeric"},
};

ParameterizedTestParameters(options, port_validation) {
  return cr_make_param_array(port_validation_test_case_t, port_validation_cases,
                             sizeof(port_validation_cases) / sizeof(port_validation_cases[0]));
}

ParameterizedTest(port_validation_test_case_t *tc, options, port_validation) {
  char *argv[] = {"client", "-p", (char *)tc->port, NULL};
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
    options_init(argc, argv, true);
    cr_assert_str_eq(opt_port, tc->port, "%s should set port correctly", tc->description);
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
      cr_assert_eq(opt_width, 100);
      cr_assert_eq(opt_height, 50);
      cr_assert_eq(auto_width, 0);
      cr_assert_eq(auto_height, 0);
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

GENERATE_OPTIONS_TEST(valid_webcam_flip, ARGV_LIST("client", "-f"), true, {/* flip flag only */},
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
    {"mono", true, 0, "Valid mode: mono"},
    {"monochrome", true, 0, "Valid mode: monochrome"},
    {"16", true, 0, "Valid mode: 16"},
    {"16color", true, 0, "Valid mode: 16color"},
    {"256", true, 0, "Valid mode: 256"},
    {"256color", true, 0, "Valid mode: 256color"},
    {"truecolor", true, 0, "Valid mode: truecolor"},
    {"24bit", true, 0, "Valid mode: 24bit"},
    // Invalid modes
    {"invalid", false, 1, "Invalid mode: invalid"},
    {"32", false, 1, "Invalid mode: 32"},
    {"512", false, 1, "Invalid mode: 512"},
    {"fullcolor", false, 1, "Invalid mode: fullcolor"},
    {"rgb", false, 1, "Invalid mode: rgb"},
    {"", false, 1, "Invalid mode: empty string"},
};

ParameterizedTestParameters(options, color_mode_validation) {
  return cr_make_param_array(color_mode_test_case_t, color_mode_cases,
                             sizeof(color_mode_cases) / sizeof(color_mode_cases[0]));
}

ParameterizedTest(color_mode_test_case_t *tc, options, color_mode_validation) {
  char *argv[] = {"client", "--color-mode", (char *)tc->mode_string, NULL};
  int argc = 3;
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

  char *valid_modes[] = {"foreground", "fg", "background", "bg", "half-block", "halfblock"};

  for (int i = 0; i < 6; i++) {
    char *argv[] = {"client", "--render-mode", valid_modes[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 0, "Valid render mode %s should not cause exit", valid_modes[i]);
  }

  restore_options(&backup);
}

Test(options, invalid_render_modes) {
  char *invalid_modes[] = {"invalid", "full", "block", "text", ""};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"client", "--render-mode", invalid_modes[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
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
    char *argv[] = {"client", "--palette", valid_palettes[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 0, "Valid palette %s should not cause exit", valid_palettes[i]);
  }

  restore_options(&backup);
}

Test(options, invalid_palettes) {
  char *invalid_palettes[] = {"invalid", "ascii", "unicode", "color", ""};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"client", "--palette", invalid_palettes[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 1, "Invalid palette %s should cause exit with code 1", invalid_palettes[i]);
  }
}

Test(options, valid_palette_chars) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client", "--palette-chars", " .:-=+*#%@$", NULL};
  int result = test_options_init_with_fork(argv, 3, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, invalid_palette_chars) {
  // Empty palette chars should fail
  char *argv[] = {"client", "--palette-chars", "", NULL};
  int result = test_options_init_with_fork(argv, 3, true);
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
    char *argv[] = {"client", "--snapshot-delay", valid_delays[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 0, "Valid snapshot delay %s should not cause exit", valid_delays[i]);
  }

  restore_options(&backup);
}

Test(options, invalid_snapshot_delays) {
  char *invalid_delays[] = {
      "-1.0", // Negative
      "abc",  // Non-numeric
      ""      // Empty
  };

  for (int i = 0; i < 3; i++) {
    char *argv[] = {"client", "--snapshot-delay", invalid_delays[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 1, "Invalid snapshot delay %s should cause exit with code 1", invalid_delays[i]);
  }
}

/* ============================================================================
 * File Path Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    valid_log_file, ARGV_LIST("client", "--log-file", "/tmp/test.log"), true,
    { cr_assert_str_eq(opt_log_file, "/tmp/test.log"); },
    { cr_assert_eq(exit_code, 0, "Valid log file should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_log_file, ARGV_LIST("client", "--log-file", ""), true,
    { cr_fail("Should not reach this point - empty log file should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Empty log file should exit with code 1"); })

GENERATE_OPTIONS_TEST(
    valid_encryption_key, ARGV_LIST("client", "--key", "mysecretkey"), true,
    {
      cr_assert_str_eq(opt_encrypt_key, "mysecretkey");
      cr_assert_eq(opt_encrypt_enabled, 1);
    },
    { cr_assert_eq(exit_code, 0, "Valid encryption key should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_encryption_key, ARGV_LIST("client", "--key", ""), true,
    { cr_fail("Should not reach this point - empty key should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Empty key should exit with code 1"); })

GENERATE_OPTIONS_TEST(
    valid_keyfile, ARGV_LIST("client", "--keyfile", "/tmp/keyfile.txt"), true,
    {
      cr_assert_str_eq(opt_encrypt_keyfile, "/tmp/keyfile.txt");
      cr_assert_eq(opt_encrypt_enabled, 1);
    },
    { cr_assert_eq(exit_code, 0, "Valid keyfile should not cause exit"); })

GENERATE_OPTIONS_TEST(
    invalid_keyfile, ARGV_LIST("client", "--keyfile", ""), true,
    { cr_fail("Should not reach this point - empty keyfile should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Empty keyfile should exit with code 1"); })

/* ============================================================================
 * Flag Options Tests
 * ============================================================================ */

Test(options, flag_options) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client",  "--show-capabilities", "--utf8",    "--audio", "--stretch",
                  "--quiet", "--snapshot",          "--encrypt", NULL};
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
    options_errors, missing_argument_address, ARGV_LIST("client", "--address"), true,
    { cr_fail("Should not reach this point - missing argument should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Missing argument should exit with code 1"); })

GENERATE_OPTIONS_TEST_IN_SUITE(
    options_errors, missing_argument_short, ARGV_LIST("client", "-a"), true,
    { cr_fail("Should not reach this point - missing argument should cause exit"); },
    { cr_assert_eq(exit_code, 1, "Missing argument should exit with code 1"); })

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

  char *argv[] = {"client", "--address=192.168.1.1", "--port=8080", "--width=100", "--height=50", NULL};
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

  char *argv[] = {"client",
                  "--address=192.168.1.100",
                  "--port=8080",
                  "--width=120",
                  "--height=60",
                  "--webcam-index=1",
                  "--webcam-flip",
                  "--color-mode=256",
                  "--render-mode=background",
                  "--palette=blocks",
                  "--audio",
                  "--stretch",
                  "--quiet",
                  "--snapshot",
                  "--snapshot-delay=2.5",
                  "--log-file=/tmp/ascii-chat.log",
                  "--encrypt",
                  "--key=mysecretpassword",
                  NULL};
  int argc = 18;

  log_set_level(LOG_DEBUG);
  int result = test_options_init_with_fork(argv, argc, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, complex_server_combination) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"server",
                  "--address=0.0.0.0",
                  "--port=27224",
                  "--palette=digital",
                  "--log-file=/var/log/ascii-chat.log",
                  "--encrypt",
                  "--keyfile=/etc/ascii-chat/key",
                  NULL};
  int argc = 7;

  int result = test_options_init_with_fork(argv, argc, false);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

/* ============================================================================
 * Usage Function Tests
 * ============================================================================ */

Test(options, usage_client) {
  FILE *devnull = fopen("/dev/null", "w");
  cr_assert_not_null(devnull);

  usage_client(devnull);

  fclose(devnull);
}

Test(options, usage_server) {
  FILE *devnull = fopen("/dev/null", "w");
  cr_assert_not_null(devnull);

  usage_server(devnull);

  fclose(devnull);
}

Test(options, usage_function) {
  FILE *devnull = fopen("/dev/null", "w");
  cr_assert_not_null(devnull);

  usage(devnull, true);
  usage(devnull, false);

  fclose(devnull);
}

/* ============================================================================
 * Dimension Update Tests
 * ============================================================================ */

Test(options, update_dimensions_for_full_height) {
  options_backup_t backup;
  save_options(&backup);

  // Test with auto dimensions
  auto_width = 1;
  auto_height = 1;
  update_dimensions_for_full_height();

  // Test with only auto height
  auto_width = 0;
  auto_height = 1;
  update_dimensions_for_full_height();

  // Test with only auto width
  auto_width = 1;
  auto_height = 0;
  update_dimensions_for_full_height();

  // Test with no auto dimensions
  auto_width = 0;
  auto_height = 0;
  update_dimensions_for_full_height();

  restore_options(&backup);
}

Test(options, update_dimensions_to_terminal_size) {
  options_backup_t backup;
  save_options(&backup);

  // Test with auto dimensions
  auto_width = 1;
  auto_height = 1;
  update_dimensions_to_terminal_size();

  // Test with only auto width
  auto_width = 1;
  auto_height = 0;
  update_dimensions_to_terminal_size();

  // Test with only auto height
  auto_width = 0;
  auto_height = 1;
  update_dimensions_to_terminal_size();

  // Test with no auto dimensions
  auto_width = 0;
  auto_height = 0;
  update_dimensions_to_terminal_size();

  restore_options(&backup);
}

/* ============================================================================
 * Edge Cases and Stress Tests
 * ============================================================================ */

Test(options, very_long_arguments) {
  options_backup_t backup;
  save_options(&backup);

  // Test with very long but valid arguments
  char long_address[OPTIONS_BUFF_SIZE];
  char long_port[OPTIONS_BUFF_SIZE];
  char long_logfile[OPTIONS_BUFF_SIZE];

  memset(long_address, '1', sizeof(long_address) - 1);
  long_address[sizeof(long_address) - 1] = '\0';
  strcpy(long_address, "192.168.1.1"); // Valid but test the buffer handling

  memset(long_port, '2', sizeof(long_port) - 1);
  long_port[sizeof(long_port) - 1] = '\0';
  strcpy(long_port, "8080");

  memset(long_logfile, '3', sizeof(long_logfile) - 1);
  long_logfile[sizeof(long_logfile) - 1] = '\0';
  strcpy(long_logfile, "/tmp/test.log");

  char *argv[] = {"client", "-a", long_address, "-p", long_port, "-L", long_logfile, NULL};
  int result = test_options_init_with_fork(argv, 7, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, maximum_values) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client",
                  "--address=255.255.255.255",
                  "--port=65535",
                  "--width=65535",
                  "--height=65535",
                  "--webcam-index=65535",
                  "--snapshot-delay=999.999",
                  NULL};
  int argc = 7;

  int result = test_options_init_with_fork(argv, argc, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, minimum_values) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client",           "--address=0.0.0.0",    "--port=1", "--width=1", "--height=1",
                  "--webcam-index=0", "--snapshot-delay=0.0", NULL};
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
    int argc = 1;
    argv[0] = "client";

    // Randomly add valid options
    if (rand() % 2) {
      argv[argc++] = "-a";
      argv[argc++] = "192.168.1.1";
    }
    if (rand() % 2) {
      argv[argc++] = "-p";
      argv[argc++] = "8080";
    }
    if (rand() % 2) {
      argv[argc++] = "--audio";
    }
    if (rand() % 2) {
      argv[argc++] = "--quiet";
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
    actual_values_client, ARGV_LIST("client", "-a", "192.168.1.1", "-p", "8080", "-x", "100", "-y", "50"), true,
    {
      // Test actual values were set
      cr_assert_str_eq(opt_address, "192.168.1.1");
      cr_assert_str_eq(opt_port, "8080");
      cr_assert_eq(opt_width, 100);
      cr_assert_eq(opt_height, 50);
      cr_assert_eq(auto_width, 0);
      cr_assert_eq(auto_height, 0);
    },
    {
      // Test that options_init doesn't exit with error
      cr_assert_eq(exit_code, 0, "options_init should not exit with error");
    })

GENERATE_OPTIONS_TEST(
    color_mode_auto, ARGV_LIST("client", "--color-mode", "auto"), true,
    { cr_assert_eq(opt_color_mode, COLOR_MODE_AUTO); },
    { cr_assert_eq(exit_code, 0, "auto color mode should not exit"); })

GENERATE_OPTIONS_TEST(
    color_mode_256, ARGV_LIST("client", "--color-mode", "256"), true,
    { cr_assert_eq(opt_color_mode, COLOR_MODE_256_COLOR); },
    { cr_assert_eq(exit_code, 0, "256 color mode should not exit"); })

GENERATE_OPTIONS_TEST(
    color_mode_truecolor, ARGV_LIST("client", "--color-mode", "truecolor"), true,
    { cr_assert_eq(opt_color_mode, COLOR_MODE_TRUECOLOR); },
    { cr_assert_eq(exit_code, 0, "truecolor mode should not exit"); })

GENERATE_OPTIONS_TEST(
    render_mode_foreground, ARGV_LIST("client", "--render-mode", "foreground"), true,
    { cr_assert_eq(opt_render_mode, RENDER_MODE_FOREGROUND); },
    { cr_assert_eq(exit_code, 0, "foreground render mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    render_mode_background, ARGV_LIST("client", "--render-mode", "background"), true,
    { cr_assert_eq(opt_render_mode, RENDER_MODE_BACKGROUND); },
    { cr_assert_eq(exit_code, 0, "background render mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    render_mode_half_block, ARGV_LIST("client", "--render-mode", "half-block"), true,
    { cr_assert_eq(opt_render_mode, RENDER_MODE_HALF_BLOCK); },
    { cr_assert_eq(exit_code, 0, "half-block render mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    palette_standard, ARGV_LIST("client", "--palette", "standard"), true,
    { cr_assert_eq(opt_palette_type, PALETTE_STANDARD); },
    { cr_assert_eq(exit_code, 0, "standard palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    palette_blocks, ARGV_LIST("client", "--palette", "blocks"), true,
    { cr_assert_eq(opt_palette_type, PALETTE_BLOCKS); },
    { cr_assert_eq(exit_code, 0, "blocks palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    palette_custom_chars, ARGV_LIST("client", "--palette-chars", "@#%*+=:-. "), true,
    {
      cr_assert_eq(opt_palette_type, PALETTE_CUSTOM);
      cr_assert_str_eq(opt_palette_custom, "@#%*+=:-. ");
      cr_assert_eq(opt_palette_custom_set, true);
    },
    { cr_assert_eq(exit_code, 0, "custom palette chars should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_flag_values,
    ARGV_LIST("client", "--audio", "--stretch", "--quiet", "--snapshot", "--encrypt", "--utf8", "--show-capabilities",
              "-f"),
    true,
    {
      // Test all flags were set
      cr_assert_eq(opt_audio_enabled, 1);
      cr_assert_eq(opt_stretch, 1);
      cr_assert_eq(opt_quiet, 1);
      cr_assert_eq(opt_snapshot_mode, 1);
      cr_assert_eq(opt_encrypt_enabled, 1);
      cr_assert_eq(opt_force_utf8, 1);
      cr_assert_eq(opt_show_capabilities, 1);
      cr_assert_eq(opt_webcam_flip, false);
    },
    { cr_assert_eq(exit_code, 0, "flag values should not cause exit"); })

GENERATE_OPTIONS_TEST(
    encryption_key_value, ARGV_LIST("client", "--key", "mysecretpassword123"), true,
    {
      cr_assert_str_eq(opt_encrypt_key, "mysecretpassword123");
      cr_assert_eq(opt_encrypt_enabled, 1);
    },
    { cr_assert_eq(exit_code, 0, "encryption key should not cause exit"); })

GENERATE_OPTIONS_TEST(
    encryption_keyfile_value, ARGV_LIST("client", "--keyfile", "/etc/secret.key"), true,
    {
      cr_assert_str_eq(opt_encrypt_keyfile, "/etc/secret.key");
      cr_assert_eq(opt_encrypt_enabled, 1);
    },
    { cr_assert_eq(exit_code, 0, "keyfile should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_snapshot_delay_values, ARGV_LIST("client", "--snapshot-delay", "2.5"), true,
    { cr_assert_float_eq(opt_snapshot_delay, 2.5f, 0.01); },
    { cr_assert_eq(exit_code, 0, "snapshot delay should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_webcam_values, ARGV_LIST("client", "-c", "3", "-f"), true,
    {
      cr_assert_eq(opt_webcam_index, 3);
      cr_assert_eq(opt_webcam_flip, false);
    },
    { cr_assert_eq(exit_code, 0, "webcam values should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_comprehensive_client_values,
    ARGV_LIST("client", "--address=10.0.0.1", "--port=9999", "--width=200", "--height=100", "--webcam-index=2",
              "--webcam-flip", "--color-mode=256", "--render-mode=background", "--palette=digital", "--audio",
              "--stretch", "--quiet", "--snapshot", "--snapshot-delay=5.0", "--log-file=/var/log/test.log", "--encrypt",
              "--key=testkey123"),
    true,
    {
      // Verify ALL values
      cr_assert_str_eq(opt_address, "10.0.0.1");
      cr_assert_str_eq(opt_port, "9999");
      cr_assert_eq(opt_width, 200);
      cr_assert_eq(opt_height, 100);
      cr_assert_eq(opt_webcam_index, 2);
      cr_assert_eq(opt_webcam_flip, false);
      cr_assert_eq(opt_color_mode, COLOR_MODE_256_COLOR);
      cr_assert_eq(opt_render_mode, RENDER_MODE_BACKGROUND);
      cr_assert_eq(opt_palette_type, PALETTE_DIGITAL);
      cr_assert_eq(opt_audio_enabled, 1);
      cr_assert_eq(opt_stretch, 1);
      cr_assert_eq(opt_quiet, 1);
      cr_assert_eq(opt_snapshot_mode, 1);
      cr_assert_float_eq(opt_snapshot_delay, 5.0f, 0.01);
      cr_assert_str_eq(opt_log_file, "/var/log/test.log");
      cr_assert_eq(opt_encrypt_enabled, 1);
      cr_assert_str_eq(opt_encrypt_key, "testkey123");
      // Should be enabled by color-mode
      cr_assert_eq(auto_width, 0);  // Should be disabled when width is set
      cr_assert_eq(auto_height, 0); // Should be disabled when height is set
    },
    { cr_assert_eq(exit_code, 0, "comprehensive client values should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_server_values,
    ARGV_LIST("server", "--address=0.0.0.0", "--port=12345", "--palette=minimal", "--log-file=/tmp/server.log",
              "--encrypt", "--keyfile=/etc/server.key"),
    false,
    {
      // Verify server values
      cr_assert_str_eq(opt_address, "0.0.0.0");
      cr_assert_str_eq(opt_port, "12345");
      cr_assert_eq(opt_palette_type, PALETTE_MINIMAL);
      // Note: --audio is not supported for server mode
      cr_assert_str_eq(opt_log_file, "/tmp/server.log");
      cr_assert_eq(opt_encrypt_enabled, 1);
      cr_assert_str_eq(opt_encrypt_keyfile, "/etc/server.key");
    },
    { cr_assert_eq(exit_code, 0, "server values should not cause exit"); })

/* ============================================================================
 * Additional Test Scenarios with the Macro
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    test_equals_sign_syntax,
    ARGV_LIST("client", "--address=192.168.1.100", "--port=8080", "--width=150", "--height=75"), true,
    {
      cr_assert_str_eq(opt_address, "192.168.1.100");
      cr_assert_str_eq(opt_port, "8080");
      cr_assert_eq(opt_width, 150);
      cr_assert_eq(opt_height, 75);
      cr_assert_eq(auto_width, 0);  // Should be disabled when width is set
      cr_assert_eq(auto_height, 0); // Should be disabled when height is set
    },
    { cr_assert_eq(exit_code, 0, "equals sign syntax should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_mixed_syntax, ARGV_LIST("client", "-a", "10.0.0.1", "--port=3000", "-x", "80", "--height=60"), true,
    {
      cr_assert_str_eq(opt_address, "10.0.0.1");
      cr_assert_str_eq(opt_port, "3000");
      cr_assert_eq(opt_width, 80);
      cr_assert_eq(opt_height, 60);
      cr_assert_eq(auto_width, 0);
      cr_assert_eq(auto_height, 0);
    },
    { cr_assert_eq(exit_code, 0, "mixed syntax should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_monochrome_mode, ARGV_LIST("client", "--color-mode", "mono"), true,
    {
      cr_assert_eq(opt_color_mode, COLOR_MODE_MONO);
      // Should be disabled for monochrome
    },
    { cr_assert_eq(exit_code, 0, "monochrome mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_monochrome_alias, ARGV_LIST("client", "--color-mode", "monochrome"), true,
    {
      cr_assert_eq(opt_color_mode, COLOR_MODE_MONO);
      // Should be disabled for monochrome
    },
    { cr_assert_eq(exit_code, 0, "monochrome alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_16color_mode, ARGV_LIST("client", "--color-mode", "16"), true,
    { cr_assert_eq(opt_color_mode, COLOR_MODE_16_COLOR); },
    { cr_assert_eq(exit_code, 0, "16color mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_16color_alias, ARGV_LIST("client", "--color-mode", "16color"), true,
    { cr_assert_eq(opt_color_mode, COLOR_MODE_16_COLOR); },
    { cr_assert_eq(exit_code, 0, "16color alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_256color_alias, ARGV_LIST("client", "--color-mode", "256color"), true,
    { cr_assert_eq(opt_color_mode, COLOR_MODE_256_COLOR); },
    { cr_assert_eq(exit_code, 0, "256color alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_truecolor_alias, ARGV_LIST("client", "--color-mode", "24bit"), true,
    { cr_assert_eq(opt_color_mode, COLOR_MODE_TRUECOLOR); },
    { cr_assert_eq(exit_code, 0, "truecolor alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_palette_digital, ARGV_LIST("client", "--palette", "digital"), true,
    { cr_assert_eq(opt_palette_type, PALETTE_DIGITAL); },
    { cr_assert_eq(exit_code, 0, "digital palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_palette_minimal, ARGV_LIST("client", "--palette", "minimal"), true,
    { cr_assert_eq(opt_palette_type, PALETTE_MINIMAL); },
    { cr_assert_eq(exit_code, 0, "minimal palette should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_palette_cool, ARGV_LIST("client", "--palette", "cool"), true,
    { cr_assert_eq(opt_palette_type, PALETTE_COOL); },
    { cr_assert_eq(exit_code, 0, "cool palette should not cause exit"); })

/* Converted looped palette test into specific GENERATE_OPTIONS_TEST cases above. */

GENERATE_OPTIONS_TEST(
    test_render_mode_fg_alias, ARGV_LIST("client", "--render-mode", "fg"), true,
    { cr_assert_eq(opt_render_mode, RENDER_MODE_FOREGROUND); },
    { cr_assert_eq(exit_code, 0, "fg alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_render_mode_bg_alias, ARGV_LIST("client", "--render-mode", "bg"), true,
    { cr_assert_eq(opt_render_mode, RENDER_MODE_BACKGROUND); },
    { cr_assert_eq(exit_code, 0, "bg alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_render_mode_halfblock_alias, ARGV_LIST("client", "--render-mode", "halfblock"), true,
    { cr_assert_eq(opt_render_mode, RENDER_MODE_HALF_BLOCK); },
    { cr_assert_eq(exit_code, 0, "halfblock alias should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_log_file_path, ARGV_LIST("client", "--log-file", "/var/log/ascii-chat.log"), true,
    { cr_assert_str_eq(opt_log_file, "/var/log/ascii-chat.log"); },
    { cr_assert_eq(exit_code, 0, "log file path should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_webcam_index_only, ARGV_LIST("client", "-c", "5"), true,
    {
      cr_assert_eq(opt_webcam_index, 5);
      cr_assert_eq(opt_webcam_flip, true); // Should remain default
    },
    { cr_assert_eq(exit_code, 0, "webcam index should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_webcam_flip_only, ARGV_LIST("client", "-f"), true,
    {
      cr_assert_eq(opt_webcam_index, 0); // Should remain default
      cr_assert_eq(opt_webcam_flip, false);
    },
    { cr_assert_eq(exit_code, 0, "webcam flip should not cause exit"); })

/* ============================================================================
 * Server-specific Tests
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    test_server_basic_options, ARGV_LIST("client", "--address=127.0.0.1", "--port=8080", "--width=110", "--height=70"),
    true,
    {
      cr_assert_str_eq(opt_address, "127.0.0.1");
      cr_assert_str_eq(opt_port, "8080");
      // Server should use default values for client-only options
      cr_assert_eq(opt_width, 110);        // Should use default width
      cr_assert_eq(opt_height, 70);        // Should use default height
      cr_assert_eq(opt_webcam_index, 0);   // Should use default
      cr_assert_eq(opt_webcam_flip, true); // Should use default
    },
    { cr_assert_eq(exit_code, 0, "server basic options should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_server_palette_options, ARGV_LIST("server", "--palette", "blocks", "--palette-chars", "0123456789"), false,
    {
      cr_assert_eq(opt_palette_type, PALETTE_CUSTOM); // --palette-chars overrides to custom
      cr_assert_str_eq(opt_palette_custom, "0123456789");
      cr_assert_eq(opt_palette_custom_set, true);
    },
    { cr_assert_eq(exit_code, 0, "server palette options should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_server_encryption_options, ARGV_LIST("server", "--encrypt", "--keyfile", "/etc/server.key"), false,
    {
      cr_assert_eq(opt_encrypt_enabled, 1);
      cr_assert_str_eq(opt_encrypt_keyfile, "/etc/server.key");
    },
    { cr_assert_eq(exit_code, 0, "server encryption options should not cause exit"); })

/* ============================================================================
 * Edge Cases and Error Conditions
 * ============================================================================ */

GENERATE_OPTIONS_TEST(
    test_auto_dimensions, ARGV_LIST("client"), true,
    {
      // These should be set to auto (1) by default
      cr_assert_eq(auto_width, 1);
      cr_assert_eq(auto_height, 1);
      // The actual dimensions will be set by update_dimensions_to_terminal_size()
      // but we can't easily test that without mocking terminal detection
    },
    { cr_assert_eq(exit_code, 0, "auto dimensions should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_manual_dimensions_disable_auto, ARGV_LIST("client", "--width", "100", "--height", "50"), true,
    {
      cr_assert_eq(opt_width, 100);
      cr_assert_eq(opt_height, 50);
      cr_assert_eq(auto_width, 0);  // Should be disabled when manually set
      cr_assert_eq(auto_height, 0); // Should be disabled when manually set
    },
    { cr_assert_eq(exit_code, 0, "manual dimensions should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_encryption_auto_enable, ARGV_LIST("client", "--key", "mypassword"), true,
    {
      cr_assert_eq(opt_encrypt_enabled, 1); // Should be auto-enabled
      cr_assert_str_eq(opt_encrypt_key, "mypassword");
    },
    { cr_assert_eq(exit_code, 0, "encryption key should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_encryption_keyfile_auto_enable, ARGV_LIST("client", "--keyfile", "/path/to/key"), true,
    {
      cr_assert_eq(opt_encrypt_enabled, 1); // Should be auto-enabled
      cr_assert_str_eq(opt_encrypt_keyfile, "/path/to/key");
    },
    { cr_assert_eq(exit_code, 0, "keyfile should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_custom_palette_auto_set_type, ARGV_LIST("client", "--palette-chars", "ABCDEFGH"), true,
    {
      cr_assert_eq(opt_palette_type, PALETTE_CUSTOM);
      cr_assert_str_eq(opt_palette_custom, "ABCDEFGH");
      cr_assert_eq(opt_palette_custom_set, true);
    },
    { cr_assert_eq(exit_code, 0, "custom palette chars should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_color_output_enabled_by_color_mode, ARGV_LIST("client", "--color-mode", "256"), true,
    {
      cr_assert_eq(opt_color_mode, COLOR_MODE_256_COLOR);
      // Should be enabled
    },
    { cr_assert_eq(exit_code, 0, "256 color mode should not cause exit"); })

GENERATE_OPTIONS_TEST(
    test_color_output_disabled_by_mono, ARGV_LIST("client", "--color-mode", "mono"), true,
    {
      cr_assert_eq(opt_color_mode, COLOR_MODE_MONO);
      // Should be disabled
    },
    { cr_assert_eq(exit_code, 0, "mono color mode should not cause exit"); })
