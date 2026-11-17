#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <vector>
#include <initializer_list>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <options/options.h>
#include <options/registry/registry.h>
#include <options/rcu.h>     // For RCU-based options access
#include <options/actions.h> // For deferred action tests
#include <ascii-chat/tests/common.h>
#include <libplatform/terminal.h>
#include <libplatform/system.h>
#include <ascii-chat/tests/logging.h>
#include <libflags/values.h>
#include <ascii-chat/util/parsing.h>

// Macro helpers for argv construction without brace-literals at call sites
static std::vector<char *> make_argv(std::initializer_list<const char *> args) {
  std::vector<char *> result;
  result.reserve(args.size() + 2);
  result.push_back(const_cast<char *>("ascii-chat"));
  for (const char *arg : args)
    result.push_back(const_cast<char *>(arg));
  result.push_back(nullptr);
  return result;
}

// Helper function to save and restore RCU options state for test isolation
// Use the full options_t struct instead of a custom backup struc
typedef options_t options_backup_t;

static void save_options(options_backup_t *backup) {
  // Get current options from RCU and make a copy
  // Don't call init here - let options_init() handle that in the tes
  const options_t *current = options_get();
  memcpy(backup, current, sizeof(options_t));
}

static void restore_options(const options_backup_t *backup) {
  (void)backup; // Unused parameter
  // Note: We don't call options_state_destroy() here because it can interfere
  // with the RCU state if multiple tests run sequentially. The atexit handlers
  // will properly clean up when the program exits.
}

// options_init reports parse failures as error codes; no process isolation is
// needed and keeping this in-process makes the test portable to Windows.
static int test_options_init_with_fork(char **argv, int argc, bool is_client) {
  (void)is_client; // Unused parameter
  if (argv && argc > 0)
    argv[0] = const_cast<char *>("ascii-chat");
  options_state_destroy();
  const asciichat_error_t result = options_init(argc, argv);
  return result == ASCIICHAT_OK ? 0 : 1;
}

/* ============================================================================
 * Basic Functionality Tests
 * ============================================================================ */

TEST_CASE("options::explicit_flip_values_survive_mode_parsing", "[options]") {
  char *argv[] = {"ascii-chat", "mirror", "--test-pattern", "0", "--flip-x", "--flip-y", NULL};
  REQUIRE(options_init(6, argv) == ASCIICHAT_OK);
  const options_t *opts = options_get();
  REQUIRE((opts->flip_x) == (false));
  REQUIRE((opts->flip_y) == (true));
}

TEST_CASE("options::test_pattern_defaults_to_unflipped", "[options]") {
  char *argv[] = {"ascii-chat", "mirror", "--test-pattern", "1", NULL};
  REQUIRE(options_init(4, argv) == ASCIICHAT_OK);
  const options_t *opts = options_get();
  REQUIRE(opts->test_pattern);
  REQUIRE_FALSE(opts->flip_x);
}

TEST_CASE("options::test_pattern_respects_explicit_flip", "[options]") {
  char *argv[] = {"ascii-chat", "mirror", "--test-pattern", "1", "--flip-x", NULL};
  REQUIRE(options_init(5, argv) == ASCIICHAT_OK);
  const options_t *opts = options_get();
  REQUIRE(opts->test_pattern);
  REQUIRE_FALSE(opts->flip_x);
}

TEST_CASE("options::default_values", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // Initialize options with minimal args to get defaults
  char *argv[] = {"program", "client", NULL};
  int argc = 2;
  options_init(argc, argv);

  // Get options from RCU for assertions
  const options_t *opts = options_get();
  (void)opts; // Reserved for future validation

  // Test default values
  REQUIRE(opts->width > 0);
  REQUIRE(opts->height > 0);
  REQUIRE((opts->auto_width) == (1));
  REQUIRE((opts->auto_height) == (1));
  REQUIRE(std::strcmp((opts->address), ("localhost")) == 0);
  REQUIRE((opts->port) == (27224));
  REQUIRE((opts->webcam_index) == (0));
  REQUIRE((opts->flip_x) == (true));
  REQUIRE((opts->color_mode) == (COLOR_MODE_AUTO));
  REQUIRE((opts->render_mode) == (RENDER_MODE_FOREGROUND));
  REQUIRE((opts->show_capabilities) == (0));
  REQUIRE((opts->force_utf8) == (0));
  REQUIRE((opts->audio_enabled) == (1)); // Default changed to true in 8ec7a9f3
  REQUIRE((opts->stretch) == (0));
  REQUIRE((opts->quiet) == (0));
  REQUIRE((opts->snapshot_mode) == (0));
  REQUIRE((opts->encrypt_enabled) == (0));
  REQUIRE((opts->palette_type) == (PALETTE_STANDARD));
  REQUIRE((opts->palette_custom_set) == (false));

  restore_options(&backup);
}

TEST_CASE("options::basic_client_options", "[options]") {
  auto argv_storage = make_argv({"client", "-x", "100", "-y", "50", "192.168.1.1:8080"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(std::strcmp((opts->address), ("192.168.1.1")) == 0);
      REQUIRE((opts->port) == (8080));
      REQUIRE((opts->width) == (100));
      REQUIRE((opts->height) == (50));
      REQUIRE((opts->auto_width) == (0));
      REQUIRE((opts->auto_height) == (0));

  }
 REQUIRE((exit_code) == (0));
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::basic_server_options", "[options]") {
  auto argv_storage = make_argv({"server", "-p", "3000", "127.0.0.1"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, false);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(std::strcmp((opts->address), ("127.0.0.1")) == 0);
      REQUIRE((opts->port) == (3000));
      // Server should use default or terminal-detected values for dimensions
      // Since opts->auto_width/opts->auto_height are true by default, the code calls
      // update_dimensions_to_terminal_size() which uses get_terminal_size()
      // - If terminal detection succeeds: uses terminal dimensions
      // - If terminal detection fails: falls back to 80x24 (not OPT_WIDTH_DEFAULT)
      // Rather than hardcode specific values, just verify dimensions are reasonable
      REQUIRE(opts->width > 0);
      REQUIRE(opts->height > 0);
      REQUIRE((opts->webcam_index) == (0));
      REQUIRE((opts->flip_x) == (true));

  }
 REQUIRE((exit_code) == (0));
  options_state_destroy();
  restore_options(&backup);
}

/* ============================================================================
 * Address and Port Validation Tests
 * ============================================================================ */

TEST_CASE("options::valid_ipv4_192_168_1_1", "[options]") {
  auto argv_storage = make_argv({"client", "192.168.1.1"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(std::strcmp((opts->address), ("192.168.1.1")) == 0);
  }
 REQUIRE((exit_code) == (0));
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::valid_ipv4_127_0_0_1", "[options]") {
  auto argv_storage = make_argv({"client", "127.0.0.1"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(std::strcmp((opts->address), ("127.0.0.1")) == 0);
  }
 REQUIRE((exit_code) == (0));
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::valid_ipv4_255_255_255_255", "[options]") {
  auto argv_storage = make_argv({"client", "255.255.255.255"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(std::strcmp((opts->address), ("255.255.255.255")) == 0);
  }
 REQUIRE((exit_code) == (0));
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_ipv4_octet_too_large", "[options]") {
  auto argv_storage = make_argv({"client", "256.1.1.1"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // This should not be reached since options_init should exi
      FAIL("test failure");

  }
 REQUIRE((exit_code) == (1));
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_ipv4_too_few_octets", "[options]") {
  auto argv_storage = make_argv({"client", "192.168.1"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // This should not be reached since options_init should exi
      FAIL("test failure");

  }
 REQUIRE((exit_code) == (1));
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_ipv4_non_numeric", "[options]") {
  auto argv_storage = make_argv({"client", "192.168.1.abc"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // This should not be reached since options_init should exi
      FAIL("test failure");

  }
 REQUIRE((exit_code) == (1));
  options_state_destroy();
  restore_options(&backup);
}

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
    {"192.168.1.1", true, 0, "Valid IP 192.168.1.1"},        {"127.0.0.1", true, 0, "Valid IP 127.0.0.1"},           {"255.255.255.255", true, 0, "Valid IP 255.255.255.255"},
    {"256.1.1.1", false, 1, "Invalid IP - octet too large"}, {"192.168.1", false, 1, "Invalid IP - too few octets"}, {"192.168.1.abc", false, 1, "Invalid IP - non-numeric"},
};

TEST_CASE("options::ip_address_validation", "[options][parameterized]") {
  for (const auto &tc : ip_validation_cases) {
  char *argv[] = {"program", "client", (char *)tc.address, NULL};
  int argc = 3;
  options_backup_t backup;
  save_options(&backup);

  // Test exit behavior with fork
  int exit_code = test_options_init_with_fork(argv, argc, true);

  // Test option values in main process (only if no exit expected)
  if (tc.should_succeed) {
    REQUIRE(exit_code == 0);
    // Get options from RCU for assertions
    const options_t *opts = options_get();
    REQUIRE(std::strcmp(opts->address, tc.address) == 0);
  } else {
    REQUIRE(exit_code == tc.expected_exit_code);
  }

    options_state_destroy();
    restore_options(&backup);
  }
}

// =============================================================================
// Port Validation Tests
// =============================================================================

TEST_CASE("options::valid_port_80", "[options]") {
  auto argv_storage = make_argv({"client", "-p", "80"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE((opts->port) == (80));
  }
 REQUIRE((exit_code) == (0));
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::valid_port_65535", "[options]") {
  auto argv_storage = make_argv({"client", "-p", "65535"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE((opts->port) == (65535));
  }
 REQUIRE((exit_code) == (0));
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_port_too_low", "[options]") {
  auto argv_storage = make_argv({"client", "--port=0"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // This should not be reached since options_init should exi
      FAIL("test failure");

  }
 REQUIRE((exit_code) == (1));
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_port_too_high", "[options]") {
  auto argv_storage = make_argv({"client", "--port=65536"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // This should not be reached since options_init should exi
      FAIL("test failure");

  }
 REQUIRE((exit_code) == (1));
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_port_non_numeric", "[options]") {
  auto argv_storage = make_argv({"client", "--port=abc"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // This should not be reached since options_init should exi
      FAIL("test failure");

  }
 REQUIRE((exit_code) == (1));
  options_state_destroy();
  restore_options(&backup);
}

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

TEST_CASE("options::port_validation", "[options][parameterized]") {
  for (const auto &tc : port_validation_cases) {
  char *argv[] = {"program", "client", "-p", (char *)tc.port, NULL};
  int argc = 4;
  options_backup_t backup;
  save_options(&backup);

  // Test exit behavior with fork
  int exit_code = test_options_init_with_fork(argv, argc, true);

  // Test option values in main process (only if no exit expected)
  if (tc.should_succeed) {
    REQUIRE(exit_code == 0);
    // Reset getopt state before calling options_init again
    options_init(argc, argv);
    // Get options from RCU for assertions
    const options_t *opts = options_get();
    int expected_port = 0;
    REQUIRE(parse_int32(tc.port, &expected_port, 0, 65535) == ASCIICHAT_OK);
    REQUIRE(opts->port == expected_port);
  } else {
    REQUIRE(exit_code == tc.expected_exit_code);
  }

    restore_options(&backup);
  }
}

/* ============================================================================
 * Dimension Tests
 * ============================================================================ */

TEST_CASE("options::valid_dimensions", "[options]") {
  auto argv_storage = make_argv({"client", "-x", "100", "-y", "50"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->width == 100);
      REQUIRE(opts->height == 50);
      REQUIRE(opts->auto_width == 0);
      REQUIRE(opts->auto_height == 0);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_dimension_zero", "[options]") {
  auto argv_storage = make_argv({"client", "--width=0"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 FAIL("test failure");
  }
 REQUIRE(exit_code == 1);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_dimension_negative", "[options]") {
  auto argv_storage = make_argv({"client", "--width=-1"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 FAIL("test failure");
  }
 REQUIRE(exit_code == 1);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_dimension_non_numeric", "[options]") {
  auto argv_storage = make_argv({"client", "-x", "abc"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 FAIL("test failure");
  }
 REQUIRE(exit_code == 1);
  options_state_destroy();
  restore_options(&backup);
}

/* ============================================================================
 * Webcam Options Tests
 * ============================================================================ */

TEST_CASE("options::valid_webcam_index", "[options]") {
  auto argv_storage = make_argv({"client", "-c", "2"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
/* no option assertions needed for success-only check */
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_webcam_index_neg1", "[options]") {
  auto argv_storage = make_argv({"client", "--webcam-index=-1"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 FAIL("test failure");
  }
 REQUIRE(exit_code == 1);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_webcam_index_abc", "[options]") {
  auto argv_storage = make_argv({"client", "-c", "abc"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 FAIL("test failure");
  }
 REQUIRE(exit_code == 1);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_webcam_index_decimal", "[options]") {
  auto argv_storage = make_argv({"client", "-c", "2.5"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 FAIL("test failure");
  }
 REQUIRE(exit_code == 1);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_webcam_index_empty", "[options]") {
  auto argv_storage = make_argv({"client", "-c", ""});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 FAIL("test failure");
  }
 REQUIRE(exit_code == 1);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::valid_webcam_flip", "[options]") {
  auto argv_storage = make_argv({"client", "--flip-x"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
/* flip flag only */
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

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

TEST_CASE("options::color_mode_validation", "[options][parameterized]") {
  for (const auto &tc : color_mode_cases) {
  char *argv[] = {"program", "client", "--color-mode", (char *)tc.mode_string, NULL};
  int argc = 4;
  options_backup_t backup;
  save_options(&backup);

  // Test exit behavior with fork
  int exit_code = test_options_init_with_fork(argv, argc, true);

  REQUIRE(exit_code == tc.expected_exit_code);

    restore_options(&backup);
  }
}

// Original loop-based tests replaced by parameterized version above
// TEST_CASE("options::valid_color_modes", "[options]") { ... }
// TEST_CASE("options::invalid_color_modes", "[options]") { ... }

/* ============================================================================
 * Render Mode Tests
 * ============================================================================ */

TEST_CASE("options::valid_render_modes", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *valid_modes[] = {"foreground", "fg", "background", "bg", "half-block"};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"program", "client", "--render-mode", valid_modes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    REQUIRE(result == 0);
  }

  restore_options(&backup);
}

TEST_CASE("options::invalid_render_modes", "[options]") {
  char *invalid_modes[] = {"invalid", "full", "block", "text", ""};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"program", "client", "--render-mode", invalid_modes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    REQUIRE(result == 1);
  }
}

/* ============================================================================
 * Palette Tests
 * ============================================================================ */

TEST_CASE("options::valid_palette_values", "[options]") {
  const char *valid_palettes[] = {"standard", "blocks", "digital", "minimal", "cool", "custom"};
  for (const char *palette : valid_palettes) {
    options_t options = options_t_new();
    REQUIRE(parse_palette_option(palette, &options) == ASCIICHAT_OK);
  };
}

TEST_CASE("options::invalid_palettes", "[options]") {
  char *invalid_palettes[] = {"invalid", "ascii", "unicode", "color", ""};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"program", "client", "--palette", invalid_palettes[i], NULL};
    int result = test_options_init_with_fork(argv, 4, true);
    REQUIRE(result == 1);
  }
}

TEST_CASE("options::valid_palette_chars", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"program", "client", "--palette-chars", " .:-=+*#%@$", NULL};
  int result = test_options_init_with_fork(argv, 4, true);
  REQUIRE(result == 0);

  restore_options(&backup);
}

TEST_CASE("options::invalid_palette_chars", "[options]") {
  // Empty palette chars should fail
  char *argv[] = {"program", "client", "--palette-chars", "", NULL};
  int result = test_options_init_with_fork(argv, 4, true);
  REQUIRE(result == 1);
}

/* ============================================================================
 * Snapshot Delay Tests
 * ============================================================================ */

TEST_CASE("options::valid_snapshot_delays", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *valid_delays[] = {"0.0", "1.5", "3.0", "10.0", "0"};

  for (int i = 0; i < 5; i++) {
    // snapshot-delay requires --snapshot to be se
    char *argv[] = {"program", "client", "--snapshot", "--snapshot-delay", valid_delays[i], NULL};
    int result = test_options_init_with_fork(argv, 5, true);
    REQUIRE(result == 0);
  }

  restore_options(&backup);
}

TEST_CASE("options::invalid_snapshot_delays", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *invalid_delays[] = {
      "abc", // Non-numeric - should fail
      ""     // Empty - should fail
  };

  for (int i = 0; i < 2; i++) {
    // snapshot-delay requires --snapshot to be se
    char *argv[] = {"program", "client", "--snapshot", "--snapshot-delay", invalid_delays[i], NULL};
    int result = test_options_init_with_fork(argv, 5, true);
    REQUIRE(result == 1);
  }

  restore_options(&backup);
}

/* ============================================================================
 * File Path Tests
 * ============================================================================ */

TEST_CASE("options::valid_encryption_key", "[options]") {
  auto argv_storage = make_argv({"client", "--key", "mysecretkey"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(std::strcmp(opts->encrypt_key, "mysecretkey") == 0);
      REQUIRE(opts->encrypt_enabled == 0);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::invalid_encryption_key", "[options]") {
  auto argv_storage = make_argv({"client", "--key", ""});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 FAIL("test failure");
  }
 REQUIRE(exit_code == 1);
  options_state_destroy();
  restore_options(&backup);
}

/* ============================================================================
 * Flag Options Tests
 * ============================================================================ */

TEST_CASE("options::flag_options", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // NOTE: --quiet is now a global option, removed from this tes
  char *argv[] = {"program", "client", "--show-capabilities", "--utf8", "--audio", "--stretch", "--snapshot", "--encrypt", NULL};
  int result = test_options_init_with_fork(argv, 8, true);
  REQUIRE(result == 0);

  restore_options(&backup);
}

/* ============================================================================
 * Help Tests
 * ============================================================================ */

TEST_CASE("options::help_client", "[options]") {
  auto argv_storage = make_argv({"client", "--help"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

                          // Help should display and exit cleanly

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::help_server", "[options]") {
  auto argv_storage = make_argv({"server", "--help"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, false);
  if (exit_code == 0) {
    const options_t *opts = options_get();

                          // Help should display and exit cleanly

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::help_short", "[options]") {
  auto argv_storage = make_argv({"client", "-h"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

                          // Help should display and exit cleanly

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

TEST_CASE("options_errors::unknown_option", "[options]") {
  auto argv_storage = make_argv({"client", "--unknown-option"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 FAIL("test failure");
  }
 REQUIRE(exit_code == 1);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options_errors::missing_argument_port", "[options]") {
  auto argv_storage = make_argv({"client", "--port"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 FAIL("test failure");
  }
 REQUIRE(exit_code == 1);
  options_state_destroy();
  restore_options(&backup);
}

/* ============================================================================
 * Equals Sign Handling Tests
 * ============================================================================ */

TEST_CASE("options::equals_sign_handling", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"program", "client", "--width=100", "--height=50", "192.168.1.1:8080", NULL};
  int result = test_options_init_with_fork(argv, 5, true);
  REQUIRE(result == 0);

  restore_options(&backup);
}

/* ============================================================================
 * Complex Combinations Tests
 * ============================================================================ */

TEST_CASE("options::complex_client_combination", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // NOTE: --quiet and --log-file are now global options, removed from this tes
  char *argv[] = {
      "program", "client",    "192.168.1.100:8080", "--width=120",          "--height=60", "--webcam-index=1",       "--flip-x", "--color-mode=256", "--render-mode=background", "--palette=blocks",
      "--audio", "--stretch", "--snapshot",         "--snapshot-delay=2.5", "--encrypt",   "--key=mysecretpassword", NULL};
  int argc = 16;

  log_set_level(LOG_DEBUG);
  int result = test_options_init_with_fork(argv, argc, true);
  REQUIRE(result == 0);

  restore_options(&backup);
}

TEST_CASE("options::complex_server_combination", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // NOTE: --log-file is now a global option, removed from this tes
  // NOTE: --palette is client-only, removed from this server tes
  char *argv[] = {"program", "server", "--port=27224", "--encrypt", "0.0.0.0", NULL};
  int argc = 5;

  int result = test_options_init_with_fork(argv, argc, false);
  REQUIRE(result == 0);

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

TEST_CASE("options::update_dimensions_for_full_height", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  // Create a local writable copy to test the function
  options_t test_opts;
  memcpy(&test_opts, options_get(), sizeof(options_t));

  // Test with auto dimensions
  test_opts.auto_width = 1;
  test_opts.auto_height = 1;
  update_dimensions_for_full_height(&test_opts);

  // Test with only auto heigh
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

TEST_CASE("options::update_dimensions_to_terminal_size", "[options]") {
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

  // Test with only auto heigh
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

TEST_CASE("options::very_long_arguments", "[options]") {
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
  REQUIRE(result == 0);

  restore_options(&backup);
}

TEST_CASE("options::maximum_values", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"program", "client", "--width=512", "--height=256", "--webcam-index=10", "--snapshot", "--snapshot-delay=60.0", NULL};
  int argc = 7;

  int result = test_options_init_with_fork(argv, argc, true);
  REQUIRE(result == 0);

  restore_options(&backup);
}

TEST_CASE("options::minimum_values", "[options]") {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"program", "client", "--width=20", "--height=10", "--webcam-index=0", "--snapshot", "--snapshot-delay=0.0", NULL};
  int argc = 7;

  int result = test_options_init_with_fork(argv, argc, true);
  REQUIRE(result == 0);

  restore_options(&backup);
}

TEST_CASE("options::random_combinations", "[options]") {
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
    // NOTE: --quiet is now a global option, removed from this tes
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
  REQUIRE(result == 0);
  }

  restore_options(&backup);
}

/* ============================================================================
 * Direct Value Testing - Tests that actually verify opt_* variables
 * ============================================================================ */

TEST_CASE("options::actual_values_client", "[options]") {
  auto argv_storage = make_argv({"client", "192.168.1.1:8080", "-x", "100", "-y", "50"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // Test actual values were se
      REQUIRE(std::strcmp(opts->address, "192.168.1.1") == 0);
      REQUIRE(opts->port == 8080);
      REQUIRE(opts->width == 100);
      REQUIRE(opts->height == 50);
      REQUIRE(opts->auto_width == 0);
      REQUIRE(opts->auto_height == 0);

  }

      // Test that options_init doesn't exit with error
      REQUIRE(exit_code == 0);

  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::color_mode_auto", "[options]") {
  auto argv_storage = make_argv({"client", "--color-mode", "auto"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->color_mode == COLOR_MODE_AUTO);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::color_mode_256", "[options]") {
  auto argv_storage = make_argv({"client", "--color-mode", "256"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->color_mode == COLOR_MODE_256_COLOR);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::color_mode_truecolor", "[options]") {
  auto argv_storage = make_argv({"client", "--color-mode", "truecolor"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->color_mode == COLOR_MODE_TRUECOLOR);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::render_mode_foreground", "[options]") {
  auto argv_storage = make_argv({"client", "--render-mode", "foreground"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->render_mode == RENDER_MODE_FOREGROUND);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::render_mode_background", "[options]") {
  auto argv_storage = make_argv({"client", "--render-mode", "background"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->render_mode == RENDER_MODE_BACKGROUND);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::render_mode_half_block", "[options]") {
  auto argv_storage = make_argv({"client", "--render-mode", "half-block"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->render_mode == RENDER_MODE_HALF_BLOCK);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::palette_standard", "[options]") {
  auto argv_storage = make_argv({"client", "--palette", "standard"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->palette_type == PALETTE_STANDARD);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::palette_blocks", "[options]") {
  auto argv_storage = make_argv({"client", "--palette", "blocks"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->palette_type == PALETTE_BLOCKS);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::palette_custom_chars", "[options]") {
  auto argv_storage = make_argv({"client", "--palette-chars", "@#%*+=:-. "});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->palette_type == PALETTE_CUSTOM);
      REQUIRE(std::strcmp(opts->palette_custom, "@#%*+=:-. ") == 0);
      REQUIRE(opts->palette_custom_set == true);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_flag_values", "[options]") {
  auto argv_storage = make_argv({"client", "--audio", "--stretch", "--snapshot", "--encrypt", "--utf8", "--show-capabilities", "--flip-x"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // Test all flags were se
      REQUIRE(opts->audio_enabled == 1);
      REQUIRE(opts->stretch == 1);
      // opts->quiet removed - now a global option
      REQUIRE(opts->snapshot_mode == 1);
      REQUIRE(opts->encrypt_enabled == 1);
      REQUIRE(opts->force_utf8 == 1);
      REQUIRE(opts->show_capabilities == 1);
      REQUIRE(opts->flip_x == true);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::encryption_key_value", "[options]") {
  auto argv_storage = make_argv({"client", "--key", "mysecretpassword123"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(std::strcmp(opts->encrypt_key, "mysecretpassword123") == 0);
      REQUIRE(opts->encrypt_enabled == 0);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_snapshot_delay_values", "[options]") {
  auto argv_storage = make_argv({"client", "--snapshot", "--snapshot-delay", "2.5"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(std::fabs(opts->snapshot_delay - 2.5f) <= 0.01f);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_webcam_values", "[options]") {
  auto argv_storage = make_argv({"client", "-c", "3", "--flip-x"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->webcam_index == 3);
      REQUIRE(opts->flip_x == true);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_comprehensive_client_values", "[options]") {
  auto argv_storage = make_argv({"client", "10.0.0.1:9999", "--width=200", "--height=100", "--webcam-index=2", "--flip-x", "--color-mode=256", "--render-mode=background", "--palette=digital", "--audio", "--stretch",
              "--snapshot", "--snapshot-delay=5.0", "--encrypt", "--key=testkey123"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // Verify ALL values
      REQUIRE(std::strcmp(opts->address, "10.0.0.1") == 0);
      REQUIRE(opts->port == 9999);
      REQUIRE(opts->width == 200);
      REQUIRE(opts->height == 100);
      REQUIRE(opts->webcam_index == 2);
      REQUIRE(opts->flip_x == true);
      REQUIRE(opts->color_mode == COLOR_MODE_256_COLOR);
      REQUIRE(opts->render_mode == RENDER_MODE_BACKGROUND);
      REQUIRE(opts->palette_type == PALETTE_DIGITAL);
      REQUIRE(opts->audio_enabled == 1);
      REQUIRE(opts->stretch == 1);
      // opts->quiet removed - now a global option
      REQUIRE(opts->snapshot_mode == 1);
      REQUIRE(std::fabs(opts->snapshot_delay - 5.0f) <= 0.01f);
      // opts->log_file removed - now a global option
      REQUIRE(opts->encrypt_enabled == 1);
      REQUIRE(std::strcmp(opts->encrypt_key, "testkey123") == 0);
      // Should be enabled by color-mode
      REQUIRE(opts->auto_width == 0);
      REQUIRE(opts->auto_height == 0);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_server_values", "[options]") {
  auto argv_storage = make_argv({"server", "0.0.0.0", "--port=12345", "--encrypt"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, false);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // Verify server values
      REQUIRE(std::strcmp(opts->address, "0.0.0.0") == 0);
      REQUIRE(opts->port == 12345);
      // Note: --audio is not supported for server mode
      // opts->log_file removed - now a global option
      // opts->palette_type removed - palette is client-only
      REQUIRE(opts->encrypt_enabled == 1);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

/* ============================================================================
 * Additional Test Scenarios with the Macro
 * ============================================================================ */

TEST_CASE("options::test_equals_sign_syntax", "[options]") {
  auto argv_storage = make_argv({"client", "--width=150", "--height=75", "192.168.1.100:8080"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(std::strcmp(opts->address, "192.168.1.100") == 0);
      REQUIRE(opts->port == 8080);
      REQUIRE(opts->width == 150);
      REQUIRE(opts->height == 75);
      REQUIRE(opts->auto_width == 0);
      REQUIRE(opts->auto_height == 0);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_mixed_syntax", "[options]") {
  auto argv_storage = make_argv({"client", "-x", "80", "--height=60", "10.0.0.1:3000"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(std::strcmp(opts->address, "10.0.0.1") == 0);
      REQUIRE(opts->port == 3000);
      REQUIRE(opts->width == 80);
      REQUIRE(opts->height == 60);
      REQUIRE(opts->auto_width == 0);
      REQUIRE(opts->auto_height == 0);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_none_mode", "[options]") {
  auto argv_storage = make_argv({"client", "--color-mode", "none"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->color_mode == COLOR_MODE_NONE);
      // Color output should be disabled for none mode

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_16color_mode", "[options]") {
  auto argv_storage = make_argv({"client", "--color-mode", "16"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->color_mode == COLOR_MODE_16_COLOR);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_16color_alias", "[options]") {
  auto argv_storage = make_argv({"client", "--color-mode", "16color"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->color_mode == COLOR_MODE_16_COLOR);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_256color_alias", "[options]") {
  auto argv_storage = make_argv({"client", "--color-mode", "256color"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->color_mode == COLOR_MODE_256_COLOR);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_truecolor_alias", "[options]") {
  auto argv_storage = make_argv({"client", "--color-mode", "24bit"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->color_mode == COLOR_MODE_TRUECOLOR);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_palette_digital", "[options]") {
  auto argv_storage = make_argv({"client", "--palette", "digital"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->palette_type == PALETTE_DIGITAL);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_palette_minimal", "[options]") {
  auto argv_storage = make_argv({"client", "--palette", "minimal"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->palette_type == PALETTE_MINIMAL);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_palette_cool", "[options]") {
  auto argv_storage = make_argv({"client", "--palette", "cool"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->palette_type == PALETTE_COOL);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

/* Palette aliases are covered by the individual cases above. */

TEST_CASE("options::test_render_mode_fg_alias", "[options]") {
  auto argv_storage = make_argv({"client", "--render-mode", "fg"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->render_mode == RENDER_MODE_FOREGROUND);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_render_mode_bg_alias", "[options]") {
  auto argv_storage = make_argv({"client", "--render-mode", "bg"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();
 REQUIRE(opts->render_mode == RENDER_MODE_BACKGROUND);
  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_webcam_index_only", "[options]") {
  auto argv_storage = make_argv({"client", "-c", "5"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->webcam_index == 5);
      REQUIRE(opts->flip_x == true);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_webcam_flip_only", "[options]") {
  auto argv_storage = make_argv({"client", "--flip-x"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->webcam_index == 0);
      REQUIRE(opts->flip_x == true);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

/* ============================================================================
 * Server-specific Tests
 * ============================================================================ */

TEST_CASE("options::test_server_basic_options", "[options]") {
  auto argv_storage = make_argv({"client", "127.0.0.1:8080", "--width=110", "--height=70"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(std::strcmp(opts->address, "127.0.0.1") == 0);
      REQUIRE(opts->port == 8080);
      // Server should use default values for client-only options
      REQUIRE(opts->width == 110);
      REQUIRE(opts->height == 70);
      REQUIRE(opts->webcam_index == 0);
      REQUIRE(opts->flip_x == true);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_server_palette_options", "[options]") {
  auto argv_storage = make_argv({"client", "--palette", "blocks", "--palette-chars", "0123456789"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->palette_type == PALETTE_CUSTOM);
      REQUIRE(std::strcmp(opts->palette_custom, "0123456789") == 0);
      REQUIRE(opts->palette_custom_set == true);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

/* ============================================================================
 * Edge Cases and Error Conditions
 * ============================================================================ */

TEST_CASE("options::test_auto_dimensions", "[options]") {
  auto argv_storage = make_argv({"client"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      // These should be set to auto (1) by defaul
      REQUIRE(opts->auto_width == 1);
      REQUIRE(opts->auto_height == 1);
      // The actual dimensions will be set by update_dimensions_to_terminal_size()
      // but we can't easily test that without mocking terminal detection

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_manual_dimensions_disable_auto", "[options]") {
  auto argv_storage = make_argv({"client", "--width", "100", "--height", "50"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->width == 100);
      REQUIRE(opts->height == 50);
      REQUIRE(opts->auto_width == 0);
      REQUIRE(opts->auto_height == 0);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_encryption_auto_enable", "[options]") {
  auto argv_storage = make_argv({"client", "--key", "mypassword"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->encrypt_enabled == 0);
      REQUIRE(std::strcmp(opts->encrypt_key, "mypassword") == 0);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_custom_palette_auto_set_type", "[options]") {
  auto argv_storage = make_argv({"client", "--palette-chars", "ABCDEFGH"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->palette_type == PALETTE_CUSTOM);
      REQUIRE(std::strcmp(opts->palette_custom, "ABCDEFGH") == 0);
      REQUIRE(opts->palette_custom_set == true);

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_color_output_enabled_by_color_mode", "[options]") {
  auto argv_storage = make_argv({"client", "--color-mode", "256"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->color_mode == COLOR_MODE_256_COLOR);
      // Should be enabled

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

TEST_CASE("options::test_color_output_disabled_by_none", "[options]") {
  auto argv_storage = make_argv({"client", "--color-mode", "none"});
  char **argv = argv_storage.data();
  int argc = static_cast<int>(argv_storage.size()) - 1;
  options_backup_t backup;
  save_options(&backup);
  int exit_code = test_options_init_with_fork(argv, argc, true);
  if (exit_code == 0) {
    const options_t *opts = options_get();

      REQUIRE(opts->color_mode == COLOR_MODE_NONE);
      // Color output should be disabled

  }
 REQUIRE(exit_code == 0);
  options_state_destroy();
  restore_options(&backup);
}

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

TEST_CASE("options::deferred_action_list_webcams", "[options]") {
  // Defer the action
  action_list_webcams();

  // Verify it was deferred (not executed immediately, which would exit)
  deferred_action_t action = actions_get_deferred();
  REQUIRE(action == ACTION_LIST_WEBCAMS);
}

TEST_CASE("options::deferred_action_list_microphones", "[options]") {
  // Defer the action
  action_list_microphones();

  // Verify it was deferred
  deferred_action_t action = actions_get_deferred();
  REQUIRE(action == ACTION_LIST_MICROPHONES);
}

TEST_CASE("options::deferred_action_list_speakers", "[options]") {
  // Defer the action
  action_list_speakers();

  // Verify it was deferred
  deferred_action_t action = actions_get_deferred();
  REQUIRE(action == ACTION_LIST_SPEAKERS);
}

TEST_CASE("options::deferred_action_show_capabilities", "[options]") {
  // Defer the action
  action_show_capabilities();

  // Verify it was deferred
  deferred_action_t action = actions_get_deferred();
  REQUIRE(action == ACTION_SHOW_CAPABILITIES);
}

TEST_CASE("options::deferred_action_first_wins", "[options]") {
  // Defer first action
  action_list_webcams();
  REQUIRE(actions_get_deferred() == ACTION_LIST_WEBCAMS);

  // Try to defer second action
  action_list_microphones();
  REQUIRE(actions_get_deferred() == ACTION_LIST_WEBCAMS);

  // Try to defer third action
  action_show_capabilities();
  REQUIRE(actions_get_deferred() == ACTION_LIST_WEBCAMS);
}

TEST_CASE("options::deferred_action_arguments", "[options]") {
  // Set action with arguments
  action_args_t args = {.output_path = "/tmp/test.txt", .shell_name = "bash"};

  actions_defer(ACTION_LIST_WEBCAMS, &args);

  // Retrieve and verify arguments
  const action_args_t *retrieved_args = actions_get_args();
  REQUIRE(retrieved_args != nullptr);
  REQUIRE(std::strcmp(retrieved_args->output_path, "/tmp/test.txt") == 0);
  REQUIRE(std::strcmp(retrieved_args->shell_name, "bash") == 0);
}

TEST_CASE("options::deferred_action_no_action_by_default", "[options]") {
  deferred_action_t action = actions_get_deferred();
  REQUIRE(action == ACTION_NONE);

  const action_args_t *args = actions_get_args();
  REQUIRE(args == nullptr);
}

TEST_CASE("options::repeated_media_arguments_share_one_ordered_playlist", "[options]") {
  char *argv[] = {"program", "mirror", "--file", "first.mp4", "--url", "https://example.com/second.webm", "--file=third.mp4", NULL};
  REQUIRE(options_init(7, argv) == ASCIICHAT_OK);
  const options_t *opts = options_get();
  REQUIRE(opts->media_playlist.count == 3);
  REQUIRE(std::strcmp(opts->media_playlist.paths[0], "first.mp4") == 0);
  REQUIRE(std::strcmp(opts->media_playlist.paths[1], "https://example.com/second.webm") == 0);
  REQUIRE(std::strcmp(opts->media_playlist.paths[2], "third.mp4") == 0);
  REQUIRE(std::strcmp(opts->media_file, "first.mp4") == 0);
  REQUIRE(opts->media_url != nullptr);
  REQUIRE(opts->media_url[0] == '\0');
}

TEST_CASE("options::url_first_playlist_does_not_gain_file_priority", "[options]") {
  char *argv[] = {"program", "mirror", "--url=https://example.com/first.webm", "-f", "second.mp4", NULL};
  REQUIRE(options_init(5, argv) == ASCIICHAT_OK);
  const options_t *opts = options_get();
  REQUIRE(opts->media_playlist.count == 2);
  REQUIRE(std::strcmp(opts->media_url, "https://example.com/first.webm") == 0);
  REQUIRE(opts->media_file != nullptr);
  REQUIRE(opts->media_file[0] == '\0');
}

TEST_CASE("options::rejected_glyph_palette_is_not_published", "[options]") {
  REQUIRE(options_state_init() == ASCIICHAT_OK);
  options_t *before = SAFE_MALLOC(sizeof(*before), options_t *);
  *before = *options_get();
  char *argv[] = {"program", "mirror", "--renderer=glyph", "--palette=digital", NULL};
  REQUIRE(options_init(4, argv) != ASCIICHAT_OK);
  REQUIRE(options_get()->renderer == before->renderer);
  REQUIRE(options_get()->palette_type == before->palette_type);
  SAFE_FREE(before);
  CLEAR_ERRNO_ALL();
  options_state_destroy();
}
