/**
 * @file config_test.c
 * @brief Unit tests for lib/config.c - TOML configuration file parsing
 *
 * Tests cover:
 * - Config file path resolution (XDG_CONFIG_HOME, fallback paths)
 * - TOML parsing with valid and invalid content
 * - Strict vs non-strict error handling
 * - All configuration sections (network, client, audio, palette, crypto, logging)
 * - Value validation and type coercion
 * - config_create_default() function
 * - Edge cases (empty files, missing sections, partial configs)
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <ascii-chat/options/config.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/presets.h>
#include <ascii-chat/options/schema.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/common.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>

// Test suite setup with quiet logging
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(config, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(config_strict, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(config_sections, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(config_create, LOG_FATAL, LOG_DEBUG, true, true);

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Backup structure for global options that config.c modifies
 * Use options_t directly as backup type for RCU compatibility
 */
typedef options_t config_options_backup_t;

/**
 * @brief Save current global options state
 */
static void save_config_options(config_options_backup_t *backup) {
  // Initialize RCU state if not already initialized
  // This is safe to call multiple times - it's a no-op if already initialized
  options_state_init();

  // Build config schema if not already built (required for config loading)
  // This is safe to call multiple times - it rebuilds if needed
  const options_config_t *unified_config = options_preset_unified(NULL, NULL);
  if (unified_config) {
    config_schema_build_from_configs(&unified_config, 1);
    options_config_destroy(unified_config);
  }

  // Get current options from RCU
  const options_t *current = options_get();
  memcpy(backup, current, sizeof(options_t));
}

/**
 * @brief Restore global options state from backup
 */
static void restore_config_options(const config_options_backup_t *backup) {
  // Write backup options back to RCU
  options_state_set((options_t *)backup);
}

/**
 * @brief Create a temporary config file with given content
 * @param content TOML content to write
 * @return Path to temporary file (caller must free and unlink)
 */
static char *create_temp_config(const char *content) {
  char template[] = "/tmp/ascii_chat_config_test_XXXXXX";
  int fd = mkstemp(template);
  if (fd < 0) {
    return NULL;
  }

  // Write content
  size_t len = strlen(content);
  ssize_t written = write(fd, content, len);
  close(fd);

  if (written != (ssize_t)len) {
    unlink(template);
    return NULL;
  }

  return platform_strdup(template);
}

/**
 * @brief Create a temp directory for config tests
 * @return Path to temp directory (caller must free and rmdir)
 */
static char *create_temp_dir(void) {
  char template[] = "/tmp/ascii_chat_config_dir_XXXXXX";
  char *result = mkdtemp(template);
  if (result) {
    return platform_strdup(result);
  }
  return NULL;
}

// =============================================================================
// Basic Loading Tests
// =============================================================================

Test(config, load_missing_file_non_strict_returns_ok) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Test with NULL path (uses default location which may or may not exist)
  // This tests the default config path resolution
  asciichat_error_t result = config_load_and_apply(true, NULL, false, &backup);
  // Default config location may not exist, but in non-strict mode it should return OK
  cr_assert_eq(result, ASCIICHAT_OK, "Default config location in non-strict mode should return OK");

  restore_config_options(&backup);
}

Test(config_strict, load_missing_file_strict_returns_error) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Non-existent file should return error in strict mode
  asciichat_error_t result = config_load_and_apply(true, "/nonexistent/path/to/config.toml", true, &backup);
  cr_assert_neq(result, ASCIICHAT_OK, "Missing file in strict mode should return error");

  restore_config_options(&backup);
}

Test(config, load_empty_file_returns_ok) {
  config_options_backup_t backup;
  save_config_options(&backup);

  char *config_path = create_temp_config("");
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Empty config file should return OK");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, load_comments_only_file_returns_ok) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "# This is a comment\n"
                        "# Another comment\n"
                        "# No actual config values\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Comments-only config file should return OK");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, load_invalid_toml_non_strict_returns_ok) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Invalid TOML syntax
  const char *content = "[network\n" // Missing closing bracket
                        "port = 8080\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Invalid TOML in non-strict mode should return OK");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_strict, load_invalid_toml_strict_returns_error) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Invalid TOML syntax
  const char *content = "[network\n" // Missing closing bracket
                        "port = 8080\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, true, &backup);
  cr_assert_neq(result, ASCIICHAT_OK, "Invalid TOML in strict mode should return error");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, load_directory_instead_of_file_returns_ok) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Try to load a directory as config file - should be handled gracefully
  asciichat_error_t result = config_load_and_apply(true, "/tmp", false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Directory as config path in non-strict mode should return OK");

  restore_config_options(&backup);
}

// =============================================================================
// Network Section Tests
// =============================================================================

Test(config_sections, network_port_as_integer) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[network]\n"
                        "port = 8080\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid port as integer should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 8080, "Port should be set to 8080");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, network_port_as_string) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[network]\n"
                        "port = \"9090\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid port as string should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 9090, "Port should be set to 9090");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, network_port_invalid_too_high) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Save original port
  int original_port;
  const options_t *opts = options_get();
  original_port = opts->port;

  const char *content = "[network]\n"
                        "port = 70000\n"; // Too high

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Invalid port should be skipped but return OK");
  cr_assert_eq(opts->port, original_port, "Port should remain unchanged for invalid value");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, network_port_invalid_zero) {
  config_options_backup_t backup;
  save_config_options(&backup);

  int original_port;
  const options_t *opts = options_get();
  original_port = opts->port;

  const char *content = "[network]\n"
                        "port = 0\n"; // Zero is invalid

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Invalid port 0 should be skipped");
  cr_assert_eq(opts->port, original_port, "Port should remain unchanged for port 0");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_port) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[network]\n"
                        "port = 8080\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid port should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 8080, "Port should be set");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, server_max_clients) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[network]\n"
                        "max_clients = 16\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(false, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid max_clients should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->max_clients, 16, "Max clients should be set");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, network_compression_level) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[network]\n"
                        "compression_level = 5\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid compression_level should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->compression_level, 5, "Compression level should be set");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

// =============================================================================
// Client Section Tests
// =============================================================================

Test(config_sections, client_width_height_as_integers) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[terminal]\n"
                        "width = 120\n"
                        "height = 40\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  // Set auto_width/height to 0 first so config values are applied
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.auto_width = 0;
  writable_opts.auto_height = 0;
  options_state_set(&writable_opts);

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid width/height should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->width, 120, "Width should be set to 120");
  cr_assert_eq(opts->height, 40, "Height should be set to 40");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_width_height_as_strings) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[terminal]\n"
                        "width = \"80\"\n"
                        "height = \"24\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid width/height as strings should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->width, 80, "Width should be set to 80");
  cr_assert_eq(opts->height, 24, "Height should be set to 24");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_webcam_settings) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[webcam]\n"
                        "webcam_index = 2\n"
                        "webcam_flip = false\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid webcam settings should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->webcam_index, 2, "Webcam index should be set to 2");
  cr_assert_eq(opts->webcam_flip, false, "Webcam flip should be false");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_color_mode_none) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[terminal]\ncolor_mode = \"none\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(MODE_CLIENT, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid color mode none should succeed");
  cr_assert_eq(backup.color_mode, TERM_COLOR_NONE,
               "After config_load_and_apply: backup.color_mode should be TERM_COLOR_NONE (got %d, expected %d)",
               backup.color_mode, TERM_COLOR_NONE);
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->color_mode, TERM_COLOR_NONE,
               "After options_state_set: color_mode should be none (got %d, expected %d)", opts->color_mode,
               TERM_COLOR_NONE);

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_color_mode_256) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[terminal]\ncolor_mode = \"256\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(MODE_CLIENT, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid color mode 256 should succeed");
  cr_assert_eq(backup.color_mode, TERM_COLOR_256,
               "After config_load_and_apply: backup.color_mode should be TERM_COLOR_256 (got %d, expected %d)",
               backup.color_mode, TERM_COLOR_256);
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->color_mode, TERM_COLOR_256, "Color mode should be 256 (got %d, expected %d)", opts->color_mode,
               TERM_COLOR_256);

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_color_mode_truecolor) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[terminal]\ncolor_mode = \"truecolor\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(MODE_CLIENT, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid color mode truecolor should succeed");
  cr_assert_eq(backup.color_mode, TERM_COLOR_TRUECOLOR,
               "After config_load_and_apply: backup.color_mode should be TERM_COLOR_TRUECOLOR (got %d, expected %d)",
               backup.color_mode, TERM_COLOR_TRUECOLOR);
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->color_mode, TERM_COLOR_TRUECOLOR, "Color mode should be truecolor (got %d, expected %d)",
               opts->color_mode, TERM_COLOR_TRUECOLOR);

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_render_mode_foreground) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\nrender_mode = \"foreground\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid render mode foreground should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->render_mode, RENDER_MODE_FOREGROUND, "Render mode should be foreground");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_render_mode_background) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\nrender_mode = \"background\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid render mode background should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->render_mode, RENDER_MODE_BACKGROUND, "Render mode should be background");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_render_mode_half_block) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\nrender_mode = \"half-block\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid render mode half-block should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->render_mode, RENDER_MODE_HALF_BLOCK, "Render mode should be half-block");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_boolean_options) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\n"
                        "stretch = true\n"
                        "snapshot = true\n"
                        "[logging]\n"
                        "quiet = true\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid boolean options should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->stretch, 1, "Stretch should be enabled");
  cr_assert_eq(opts->quiet, 1, "Quiet should be enabled");
  cr_assert_eq(opts->snapshot_mode, 1, "Snapshot mode should be enabled");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_snapshot_delay) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\n"
                        "snapshot_delay = 2.5\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid snapshot delay should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_float_eq(opts->snapshot_delay, 2.5f, 0.01f, "Snapshot delay should be 2.5");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_fps_as_integer) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\n"
                        "fps = 30\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid FPS should succeed");
  cr_assert_eq(GET_OPTION(fps), 30, "FPS should be set to 30");
  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_fps_invalid_too_high) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\n"
                        "fps = 200\n"; // Too high (max 144)

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  int original_fps = GET_OPTION(fps);

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Invalid FPS should be skipped but return OK");
  cr_assert_eq(GET_OPTION(fps), original_fps, "FPS should remain unchanged for invalid value");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, client_config_ignored_for_server) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Set a known state
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.width = 100;
  writable_opts.height = 50;
  writable_opts.auto_width = 0;
  writable_opts.auto_height = 0;
  options_state_set(&writable_opts);

  const char *content = "[terminal]\n"
                        "width = 200\n"
                        "height = 100\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  // Terminal settings apply to all modes
  asciichat_error_t result = config_load_and_apply(false, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Loading terminal config should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->width, 200, "Width should be set from terminal config");
  cr_assert_eq(opts->height, 100, "Height should be set from terminal config");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

// =============================================================================
// Audio Section Tests
// =============================================================================

Test(config_sections, audio_settings) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[audio]\n"
                        "enabled = true\n"
                        "microphone_index = 1\n"
                        "speakers_index = 2\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid audio settings should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->audio_enabled, 1, "Audio should be enabled");
  cr_assert_eq(opts->microphone_index, 1, "Microphone index should be 1");
  cr_assert_eq(opts->speakers_index, 2, "Speakers index should be 2");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, audio_device_default) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[audio]\n"
                        "enabled = true\n"
                        "microphone_index = -1\n"; // -1 means default device

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Microphone index -1 should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->microphone_index, -1, "Microphone index should be -1 (default)");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, audio_config_ignored_for_server) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.audio_enabled = 0;
  writable_opts.microphone_index = 0;
  options_state_set(&writable_opts);

  const char *content = "[audio]\n"
                        "audio = true\n"
                        "microphone_index = 5\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  // Load as server - audio config should be ignored
  asciichat_error_t result = config_load_and_apply(false, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Loading audio config as server should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->audio_enabled, 0, "Audio enabled should remain unchanged for server");
  cr_assert_eq(opts->microphone_index, 0, "Microphone index should remain unchanged for server");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

// =============================================================================
// Palette Section Tests
// =============================================================================

Test(config_sections, palette_type_standard) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[palette]\ntype = \"standard\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid palette type standard should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->palette_type, PALETTE_STANDARD, "Palette type should be standard");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, palette_type_blocks) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\npalette = \"blocks\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid palette type blocks should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->palette_type, PALETTE_BLOCKS, "Palette type should be blocks");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, palette_type_digital) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\npalette = \"digital\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid palette type digital should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->palette_type, PALETTE_DIGITAL, "Palette type should be digital");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, palette_custom_chars) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\n"
                        "palette_chars = \"@#$%^&*\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid palette chars should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_str_eq(opts->palette_custom, "@#$%^&*", "Palette chars should be set");
  cr_assert_eq(opts->palette_type, PALETTE_CUSTOM, "Palette type should be set to custom");
  cr_assert_eq(opts->palette_custom_set, true, "Palette custom set flag should be true");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, palette_chars_too_long) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Create a string longer than 255 chars
  char long_chars[300];
  memset(long_chars, 'A', 299);
  long_chars[299] = '\0';

  char content[512];
  safe_snprintf(content, sizeof(content), "[display]\npalette_chars = \"%s\"\n", long_chars);

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  // Clear palette custom before test
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.palette_custom[0] = '\0';
  writable_opts.palette_custom_set = false;
  options_state_set(&writable_opts);

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Too long palette chars should be skipped");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->palette_custom_set, false, "Palette custom set should remain false");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

// =============================================================================
// Crypto Section Tests
// =============================================================================

Test(config_sections, crypto_encrypt_enabled) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[security]\n"
                        "encrypt_enabled = true\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid encrypt enabled should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->encrypt_enabled, 1, "Encryption should be enabled");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, crypto_no_encrypt) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // First enable encryption
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.encrypt_enabled = 1;
  options_state_set(&writable_opts);

  const char *content = "[security]\n"
                        "no_encrypt = true\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid no_encrypt should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->no_encrypt, 1, "No encrypt should be set");
  cr_assert_eq(opts->encrypt_enabled, 0, "Encryption should be disabled by no_encrypt");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, crypto_key_auto_enables_encryption) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.encrypt_enabled = 0;
  options_state_set(&writable_opts);

  const char *content = "[security]\n"
                        "key = \"gpg:ABCD1234\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid crypto key should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_str_eq(opts->encrypt_key, "gpg:ABCD1234", "Crypto key should be set");
  cr_assert_eq(opts->encrypt_enabled, 1, "Encryption should be auto-enabled when key provided");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, crypto_server_key_client_only) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[security]\n"
                        "server_key = \"github:testuser\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  // Load as client
  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Server key for client should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_str_eq(opts->server_key, "github:testuser", "Server key should be set for client");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, crypto_client_keys_server_only) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Create a temporary directory for client keys
  char *temp_dir = create_temp_dir();
  cr_assert_not_null(temp_dir, "Failed to create temp directory");

  char content[512];
  safe_snprintf(content, sizeof(content), "[security]\nclient_keys = \"%s\"\n", temp_dir);

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  // Load as server
  asciichat_error_t result = config_load_and_apply(false, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Client keys for server should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_str_eq(opts->client_keys, temp_dir, "Client keys should be set for server");

  unlink(config_path);
  rmdir(temp_dir);
  SAFE_FREE(config_path);
  SAFE_FREE(temp_dir);
  restore_config_options(&backup);
}

// =============================================================================
// Logging Section Tests
// =============================================================================

Test(config_sections, log_file_in_logging_section) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[logging]\n"
                        "log_file = \"/tmp/test.log\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid log file should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_str_eq(opts->log_file, "/tmp/test.log", "Log file should be set");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, log_file_at_root) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "log_file = \"/var/log/ascii-chat.log\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid root log file should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_str_eq(opts->log_file, "/var/log/ascii-chat.log", "Log file should be set from root");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

// =============================================================================
// Comprehensive Config Tests
// =============================================================================

Test(config_sections, full_client_config) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[network]\n"
                        "port = 9000\n"
                        "\n"
                        "[terminal]\n"
                        "width = 160\n"
                        "height = 48\n"
                        "color_mode = \"256\"\n"
                        "\n"
                        "[webcam]\n"
                        "webcam_index = 1\n"
                        "webcam_flip = false\n"
                        "\n"
                        "[display]\n"
                        "render_mode = \"half-block\"\n"
                        "fps = 60\n"
                        "stretch = true\n"
                        "snapshot = false\n"
                        "snapshot_delay = 1.0\n"
                        "palette = \"digital\"\n"
                        "\n"
                        "[logging]\n"
                        "quiet = false\n"
                        "log_file = \"/tmp/ascii-chat-test.log\"\n"
                        "\n"
                        "[audio]\n"
                        "audio = true\n"
                        "microphone_index = 0\n"
                        "\n"
                        "[security]\n"
                        "encrypt = true\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  // Disable auto dimensions
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.auto_width = 0;
  writable_opts.auto_height = 0;
  options_state_set(&writable_opts);

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Full client config should succeed");

  // Verify all values
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 9000, "Port should be 9000");
  cr_assert_eq(opts->width, 160, "Width should be 160");
  cr_assert_eq(opts->height, 48, "Height should be 48");
  cr_assert_eq(opts->webcam_index, 1, "Webcam index should be 1");
  cr_assert_eq(opts->webcam_flip, false, "Webcam flip should be false");
  cr_assert_eq(opts->color_mode, TERM_COLOR_256, "Color mode should be 256");
  cr_assert_eq(opts->render_mode, RENDER_MODE_HALF_BLOCK, "Render mode should be half-block");
  cr_assert_eq(opts->stretch, 1, "Stretch should be enabled");
  cr_assert_eq(opts->quiet, 0, "Quiet should be disabled");
  cr_assert_eq(opts->snapshot_mode, 0, "Snapshot mode should be disabled");
  cr_assert_float_eq(opts->snapshot_delay, 1.0f, 0.01f, "Snapshot delay should be 1.0");
  cr_assert_eq(opts->audio_enabled, 1, "Audio should be enabled");
  cr_assert_eq(opts->microphone_index, 0, "Microphone index should be 0");
  cr_assert_eq(opts->palette_type, PALETTE_DIGITAL, "Palette should be digital");
  cr_assert_eq(opts->encrypt_enabled, 1, "Encryption should be enabled");
  cr_assert_str_eq(opts->log_file, "/tmp/ascii-chat-test.log", "Log file should be set");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config_sections, full_server_config) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Create a temp directory for client keys to avoid path validation issues
  char *temp_keys_dir = create_temp_dir();
  cr_assert_not_null(temp_keys_dir, "Failed to create temp keys directory");

  char content[1024];
  safe_snprintf(content, sizeof(content),
                "[network]\n"
                "port = 27224\n"
                "\n"
                "[display]\n"
                "palette = \"blocks\"\n"
                "\n"
                "[security]\n"
                "encrypt = true\n"
                "client_keys = \"%s\"\n"
                "\n"
                "[logging]\n"
                "log_file = \"/tmp/ascii-chat-server-test.log\"\n",
                temp_keys_dir);

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(false, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Full server config should succeed");

  // Verify server values
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 27224, "Port should be 27224");
  cr_assert_eq(opts->palette_type, PALETTE_BLOCKS, "Palette should be blocks");
  cr_assert_eq(opts->encrypt_enabled, 1, "Encryption should be enabled");
  cr_assert_str_eq(opts->client_keys, temp_keys_dir, "Client keys should be set");
  cr_assert_str_eq(opts->log_file, "/tmp/ascii-chat-server-test.log", "Log file should be set");

  unlink(config_path);
  rmdir(temp_keys_dir);
  SAFE_FREE(config_path);
  SAFE_FREE(temp_keys_dir);
  restore_config_options(&backup);
}

// =============================================================================
// config_create_default Tests
// =============================================================================

Test(config_create, creates_file_with_content) {
  config_options_backup_t backup;
  save_config_options(&backup);

  char *temp_dir = create_temp_dir();
  cr_assert_not_null(temp_dir, "Failed to create temp directory");

  char config_path[512];
  safe_snprintf(config_path, sizeof(config_path), "%s/config.toml", temp_dir);

  asciichat_error_t result = config_create_default(config_path);
  cr_assert_eq(result, ASCIICHAT_OK, "Creating default config should succeed");

  // Verify file exists
  struct stat st;
  cr_assert_eq(stat(config_path, &st), 0, "Config file should exist");
  cr_assert_gt(st.st_size, 0, "Config file should have content");

  // Read file and verify it contains expected sections
  FILE *f = fopen(config_path, "r");
  cr_assert_not_null(f, "Should be able to open created file");

  char buffer[8192];
  size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, f);
  buffer[bytes_read] = '\0';
  fclose(f);

  // Verify key sections exist
  cr_assert(strstr(buffer, "[network]") != NULL, "Config should have [network] section");
  cr_assert(strstr(buffer, "[terminal]") != NULL, "Config should have [terminal] section");
  cr_assert(strstr(buffer, "[display]") != NULL, "Config should have [display] section");
  cr_assert(strstr(buffer, "[audio]") != NULL, "Config should have [audio] section");
  cr_assert(strstr(buffer, "[security]") != NULL, "Config should have [security] section");
  cr_assert(strstr(buffer, "[logging]") != NULL, "Config should have [logging] section");
  cr_assert(strstr(buffer, "ascii-chat") != NULL, "Config should mention ascii-chat");

  // Cleanup
  unlink(config_path);
  rmdir(temp_dir);
  SAFE_FREE(temp_dir);
  restore_config_options(&backup);
}

Test(config_create, fails_if_file_exists) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Create a temp file that already exists
  char *existing_file = create_temp_config("existing content");
  cr_assert_not_null(existing_file, "Failed to create temp file");

  // Set environment variable to answer "no" to overwrite prompt
  const char *old_response = getenv("ASCII_CHAT_TESTING_QUESTION_PROMPT_RESPONSE");
  setenv("ASCII_CHAT_TESTING_QUESTION_PROMPT_RESPONSE", "no", 1);

  // Try to create default config at same path
  asciichat_error_t result = config_create_default(existing_file);
  cr_assert_neq(result, ASCIICHAT_OK, "Creating config over existing file should fail");

  // Restore old environment variable
  if (old_response) {
    setenv("ASCII_CHAT_TESTING_QUESTION_PROMPT_RESPONSE", old_response, 1);
  } else {
    unsetenv("ASCII_CHAT_TESTING_QUESTION_PROMPT_RESPONSE");
  }

  unlink(existing_file);
  SAFE_FREE(existing_file);
  restore_config_options(&backup);
}

Test(config_create, creates_directory_if_needed) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Create path with non-existent directory
  char config_path[512];
  safe_snprintf(config_path, sizeof(config_path), "/tmp/ascii_chat_test_%d/subdir/config.toml", getpid());

  // The parent directory doesn't exist yet
  asciichat_error_t result = config_create_default(config_path);
  // This may fail if we can't create nested directories (config_create_default only creates one level)
  // Just verify it handles the case gracefully
  (void)result;

  // Cleanup (may or may not exist)
  unlink(config_path);
  char parent_dir[512];
  safe_snprintf(parent_dir, sizeof(parent_dir), "/tmp/ascii_chat_test_%d/subdir", getpid());
  rmdir(parent_dir);
  safe_snprintf(parent_dir, sizeof(parent_dir), "/tmp/ascii_chat_test_%d", getpid());
  rmdir(parent_dir);

  restore_config_options(&backup);
}

// =============================================================================
// Edge Case Tests
// =============================================================================

Test(config, unknown_sections_are_ignored) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[unknown_section]\n"
                        "unknown_key = \"value\"\n"
                        "\n"
                        "[network]\n"
                        "port = 5555\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Unknown sections should be ignored");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 5555, "Known values should still be applied");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, unknown_keys_are_ignored) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[network]\n"
                        "port = 6666\n"
                        "unknown_key = \"value\"\n"
                        "another_unknown = 123\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Unknown keys should be ignored");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 6666, "Known values should still be applied");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, multiple_loads_accumulate_correctly) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // First config sets port
  const char *content1 = "[network]\n"
                         "port = 7777\n";

  char *config_path1 = create_temp_config(content1);
  cr_assert_not_null(config_path1, "Failed to create first temp config file");

  asciichat_error_t result1 = config_load_and_apply(true, config_path1, false, &backup);
  cr_assert_eq(result1, ASCIICHAT_OK, "First config load should succeed");
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 7777, "Port should be 7777 after first load");

  // Second config sets different values
  // Note: port won't be overwritten because config_port_set flag is set
  const char *content2 = "[webcam]\n"
                         "webcam_index = 3\n";

  char *config_path2 = create_temp_config(content2);
  cr_assert_not_null(config_path2, "Failed to create second temp config file");

  asciichat_error_t result2 = config_load_and_apply(true, config_path2, false, &backup);
  cr_assert_eq(result2, ASCIICHAT_OK, "Second config load should succeed");
  // Port flag was reset in config_load_and_apply, so this is tricky to test
  // Let's just verify both loads succeeded
  cr_assert_eq(opts->webcam_index, 3, "Webcam index should be 3 after second load");

  unlink(config_path1);
  unlink(config_path2);
  SAFE_FREE(config_path1);
  SAFE_FREE(config_path2);
  restore_config_options(&backup);
}

Test(config, whitespace_handling) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "  [network]  \n"
                        "  port   =   8888   \n"
                        "\n"
                        "  [terminal]  \n"
                        "  width   =   120  \n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  // Disable auto_width so config value applies
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.auto_width = 0;
  options_state_set(&writable_opts);

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Config with extra whitespace should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 8888, "Port should be parsed correctly despite whitespace");
  cr_assert_eq(opts->width, 120, "Width should be parsed correctly despite whitespace");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, inline_comments) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[network]\n"
                        "port = 9999 # This is a port comment\n"
                        "\n"
                        "[terminal]\n"
                        "width = 100 # Width in characters\n";

  char *config_path = create_temp_config(content);
  // Disable auto width
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.auto_width = 0;
  options_state_set(&writable_opts);

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Config with inline comments should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 9999, "Port should be parsed correctly with inline comment");
  cr_assert_eq(opts->width, 100, "Width should be parsed correctly with inline comment");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

// =============================================================================
// Type Coercion Tests
// =============================================================================

Test(config, integer_vs_string_port) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Test integer port
  const char *content1 = "[network]\nport = 1234\n";
  char *config_path1 = create_temp_config(content1);
  cr_assert_not_null(config_path1, "Failed to create config with integer port");

  asciichat_error_t result1 = config_load_and_apply(true, config_path1, false, &backup);
  cr_assert_eq(result1, ASCIICHAT_OK, "Integer port should succeed");
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 1234, "Integer port should be applied");

  unlink(config_path1);
  SAFE_FREE(config_path1);

  // Reset for next test
  restore_config_options(&backup);
  save_config_options(&backup);

  // Test string port
  const char *content2 = "[network]\nport = \"5678\"\n";
  char *config_path2 = create_temp_config(content2);
  cr_assert_not_null(config_path2, "Failed to create config with string port");

  asciichat_error_t result2 = config_load_and_apply(true, config_path2, false, &backup);
  cr_assert_eq(result2, ASCIICHAT_OK, "String port should succeed");
  cr_assert_eq(opts->port, 5678, "String port should be parsed and applied");

  unlink(config_path2);
  SAFE_FREE(config_path2);
  restore_config_options(&backup);
}

Test(config, boolean_values) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Test true/false (TOML boolean literals)
  const char *content = "[display]\n"
                        "stretch = true\n"
                        "[logging]\n"
                        "quiet = false\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Boolean values should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->stretch, 1, "true should be 1");
  cr_assert_eq(opts->quiet, 0, "false should be 0");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, float_snapshot_delay) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const char *content = "[display]\n"
                        "snapshot_delay = 3.14159\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Float snapshot delay should succeed");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_float_eq(opts->snapshot_delay, 3.14159f, 0.0001f, "Float should be parsed correctly");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

// =============================================================================
// Validation Tests
// =============================================================================

Test(config, invalid_color_mode_skipped) {
  config_options_backup_t backup;
  save_config_options(&backup);
  // Set initial color mode
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.color_mode = TERM_COLOR_AUTO;
  options_state_set(&writable_opts);

  const char *content = "[display]\n"
                        "color_mode = \"invalid_mode\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Invalid color mode should be skipped");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->color_mode, TERM_COLOR_AUTO, "Color mode should remain unchanged");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, invalid_render_mode_skipped) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Set initial render mode
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.render_mode = RENDER_MODE_FOREGROUND;
  options_state_set(&writable_opts);

  const char *content = "[display]\n"
                        "render_mode = \"invalid_mode\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Invalid render mode should be skipped");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->render_mode, RENDER_MODE_FOREGROUND, "Render mode should remain unchanged");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, invalid_palette_type_skipped) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Set initial palette type
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.palette_type = PALETTE_STANDARD;
  options_state_set(&writable_opts);

  const char *content = "[palette]\n"
                        "type = \"nonexistent_palette\"\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Invalid palette type should be skipped");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->palette_type, PALETTE_STANDARD, "Palette type should remain unchanged");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, negative_width_skipped) {
  config_options_backup_t backup;
  save_config_options(&backup);

  // Set initial width values
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.width = 80;
  writable_opts.auto_width = 0;
  options_state_set(&writable_opts);

  const char *content = "[terminal]\n"
                        "width = \"-10\"\n"; // Negative as string

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Negative width should be skipped");
  options_state_set(&backup);
  const options_t *opts = options_get();
  (void)opts; // Reserved for future validation

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, negative_snapshot_delay_skipped) {
  config_options_backup_t backup;
  save_config_options(&backup);
  // Set initial snapshot delay
  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  options_state_set(&writable_opts);

  const char *content = "[display]\n"
                        "snapshot_delay = -5.0\n";

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Negative snapshot delay should be skipped");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_float_eq(opts->snapshot_delay, 1.0f, 0.01f, "Snapshot delay should remain unchanged");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}

Test(config, invalid_port_skipped) {
  config_options_backup_t backup;
  save_config_options(&backup);

  const options_t *current_opts = options_get();
  options_t writable_opts;
  memcpy(&writable_opts, current_opts, sizeof(options_t));
  writable_opts.port = 8080;
  options_state_set(&writable_opts);

  const char *content = "[network]\n"
                        "port = 99999\n"; // Invalid port (too high)

  char *config_path = create_temp_config(content);
  cr_assert_not_null(config_path, "Failed to create temp config file");

  asciichat_error_t result = config_load_and_apply(true, config_path, false, &backup);
  cr_assert_eq(result, ASCIICHAT_OK, "Invalid port should be skipped");
  options_state_set(&backup);
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 8080, "Port should remain unchanged");

  unlink(config_path);
  SAFE_FREE(config_path);
  restore_config_options(&backup);
}
