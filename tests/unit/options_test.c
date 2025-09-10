#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "options.h"
#include "common.h"
#include "terminal_detect.h"

void setup_quiet_test_logging(void);
void restore_test_logging(void);

// Global variables to store original stdout and stderr for restoration
static int original_stdout_fd = -1;
static int original_stderr_fd = -1;
static int dev_null_fd = -1;

TestSuite(options, .init = setup_quiet_test_logging, .fini = restore_test_logging);

// Separate test suite for error handling tests that need to see stderr output
TestSuite(options_errors, .init = NULL, .fini = NULL);

void setup_quiet_test_logging(void) {
  // Set log level to only show fatal errors during non-logging tests
  log_set_level(LOG_FATAL);

  // Redirect stdout and stderr to /dev/null to silence test output
  original_stdout_fd = dup(STDOUT_FILENO);
  original_stderr_fd = dup(STDERR_FILENO);
  dev_null_fd = open("/dev/null", O_WRONLY);
  dup2(dev_null_fd, STDOUT_FILENO);
  dup2(dev_null_fd, STDERR_FILENO);
}

void restore_test_logging(void) {
  // Restore normal log level after tests
  log_set_level(LOG_DEBUG);

  // Restore original stdout and stderr
  if (original_stdout_fd != -1) {
    dup2(original_stdout_fd, STDOUT_FILENO);
    close(original_stdout_fd);
    original_stdout_fd = -1;
  }

  if (original_stderr_fd != -1) {
    dup2(original_stderr_fd, STDERR_FILENO);
    close(original_stderr_fd);
    original_stderr_fd = -1;
  }

  // Close /dev/null file descriptor
  if (dev_null_fd != -1) {
    close(dev_null_fd);
    dev_null_fd = -1;
  }
}

// Helper function to save and restore global options
typedef struct {
  unsigned short int opt_width, opt_height, auto_width, auto_height;
  char opt_address[OPTIONS_BUFF_SIZE], opt_port[OPTIONS_BUFF_SIZE];
  unsigned short int opt_webcam_index;
  bool opt_webcam_flip;
  unsigned short int opt_color_output;
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
  backup->opt_color_output = opt_color_output;
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
  opt_color_output = backup->opt_color_output;
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
      argv_with_null = (char **)calloc((size_t)argc + 1, sizeof(char *));
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

    options_init(argc, argv_with_null, is_client);
    _exit(0); // Should not reach here if options_init calls exit()
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
  cr_assert_str_eq(opt_address, "0.0.0.0");
  cr_assert_str_eq(opt_port, "27224");
  cr_assert_eq(opt_webcam_index, 0);
  cr_assert_eq(opt_webcam_flip, false);
  cr_assert_eq(opt_color_output, 0);
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

Test(options, basic_client_options) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client", "-a", "192.168.1.1", "-p", "8080", "-x", "100", "-y", "50", NULL};
  int argc = 9;

  int result = test_options_init_with_fork(argv, argc, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, basic_server_options) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"server", "-a", "127.0.0.1", "-p", "3000", NULL};
  int argc = 5;

  int result = test_options_init_with_fork(argv, argc, false);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

/* ============================================================================
 * Address and Port Validation Tests
 * ============================================================================ */

Test(options, valid_ipv4_addresses) {
  options_backup_t backup;
  save_options(&backup);

  char *valid_ips[] = {"192.168.1.1", "127.0.0.1", "10.0.0.1", "255.255.255.255", "0.0.0.0"};

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"client", "-a", valid_ips[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 0, "Valid IP %s should not cause exit", valid_ips[i]);
  }

  restore_options(&backup);
}

Test(options, invalid_ipv4_addresses) {
  char *invalid_ips[] = {
      "256.1.1.1",       // Octet > 255
      "192.168.1",       // Too few octets
      "192.168.1.1.1",   // Too many octets
      "192.168.1.abc",   // Non-numeric
      "192.168.1.",      // Trailing dot
      ".192.168.1.1",    // Leading dot
      "192..168.1.1",    // Double dot
      "192.168.-1.1",    // Negative octet
      "192.168.1.1.1.1", // Way too many octets
      "notanip",         // Not an IP at all
      ""                 // Empty string
  };

  for (int i = 0; i < 11; i++) {
    char *argv[] = {"client", "-a", invalid_ips[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 1, "Invalid IP %s should cause exit with code 1", invalid_ips[i]);
  }
}

Test(options, valid_ports) {
  options_backup_t backup;
  save_options(&backup);

  char *valid_ports[] = {"1", "80", "443", "8080", "27224", "65535"};

  for (int i = 0; i < 6; i++) {
    char *argv[] = {"client", "-p", valid_ports[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 0, "Valid port %s should not cause exit", valid_ports[i]);
  }

  restore_options(&backup);
}

Test(options, invalid_ports) {
  char *invalid_ports[] = {
      "0",     // Too low
      "65536", // Too high
      "abc",   // Non-numeric
      "-1",    // Negative
      "80.5",  // Decimal
      ""       // Empty
  };

  for (int i = 0; i < 6; i++) {
    char *argv[] = {"client", "-p", invalid_ports[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 1, "Invalid port %s should cause exit with code 1", invalid_ports[i]);
  }
}

/* ============================================================================
 * Dimension Tests
 * ============================================================================ */

Test(options, valid_dimensions) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client", "-x", "100", "-y", "50", NULL};
  int result = test_options_init_with_fork(argv, 5, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, invalid_dimensions) {
  char *invalid_dims[] = {
      "0",     // Zero width/height
      "-1",    // Negative
      "abc",   // Non-numeric
      "100.5", // Decimal
      ""       // Empty
  };

  for (int i = 0; i < 5; i++) {
    char *argv[] = {"client", "-x", invalid_dims[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 1, "Invalid dimension %s should cause exit with code 1", invalid_dims[i]);
  }
}

/* ============================================================================
 * Webcam Options Tests
 * ============================================================================ */

Test(options, valid_webcam_index) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client", "-c", "2", NULL};
  int result = test_options_init_with_fork(argv, 3, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, invalid_webcam_index) {
  char *invalid_indices[] = {
      "-1",  // Negative
      "abc", // Non-numeric
      "2.5", // Decimal
      ""     // Empty
  };

  for (int i = 0; i < 4; i++) {
    char *argv[] = {"client", "-c", invalid_indices[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 1, "Invalid webcam index %s should cause exit with code 1", invalid_indices[i]);
  }
}

Test(options, valid_webcam_flip) {
  options_backup_t backup;
  save_options(&backup);

  // Test that -f flag works (should not cause exit)
  char *argv[] = {"client", "-f", NULL};
  int result = test_options_init_with_fork(argv, 2, true);
  cr_assert_eq(result, 0, "Webcam flip flag should not cause exit");

  restore_options(&backup);
}


/* ============================================================================
 * Color Mode Tests
 * ============================================================================ */

Test(options, valid_color_modes) {
  options_backup_t backup;
  save_options(&backup);

  char *valid_modes[] = {"auto", "mono", "monochrome", "16", "16color", "256", "256color", "truecolor", "24bit"};

  for (int i = 0; i < 9; i++) {
    char *argv[] = {"client", "--color-mode", valid_modes[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 0, "Valid color mode %s should not cause exit", valid_modes[i]);
  }

  restore_options(&backup);
}

Test(options, invalid_color_modes) {
  char *invalid_modes[] = {"invalid", "32", "512", "fullcolor", "rgb", ""};

  for (int i = 0; i < 6; i++) {
    char *argv[] = {"client", "--color-mode", invalid_modes[i], NULL};
    int result = test_options_init_with_fork(argv, 3, true);
    cr_assert_eq(result, 1, "Invalid color mode %s should cause exit with code 1", invalid_modes[i]);
  }
}

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

Test(options, valid_log_file) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client", "--log-file", "/tmp/test.log", NULL};
  int result = test_options_init_with_fork(argv, 3, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, invalid_log_file) {
  char *argv[] = {"client", "--log-file", "", NULL};
  int result = test_options_init_with_fork(argv, 3, true);
  cr_assert_eq(result, 1);
}

Test(options, valid_encryption_key) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client", "--key", "mysecretkey", NULL};
  int result = test_options_init_with_fork(argv, 3, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, invalid_encryption_key) {
  char *argv[] = {"client", "--key", "", NULL};
  int result = test_options_init_with_fork(argv, 3, true);
  cr_assert_eq(result, 1);
}

Test(options, valid_keyfile) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client", "--keyfile", "/tmp/keyfile.txt", NULL};
  int result = test_options_init_with_fork(argv, 3, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

Test(options, invalid_keyfile) {
  char *argv[] = {"client", "--keyfile", "", NULL};
  int result = test_options_init_with_fork(argv, 3, true);
  cr_assert_eq(result, 1);
}

/* ============================================================================
 * Flag Options Tests
 * ============================================================================ */

Test(options, flag_options) {
  options_backup_t backup;
  save_options(&backup);

  char *argv[] = {"client",  "--show-capabilities", "--utf8",   "--audio", "--stretch",
                  "--quiet", "--snapshot",          "--encrypt", NULL};
  int result = test_options_init_with_fork(argv, 8, true);
  cr_assert_eq(result, 0);

  restore_options(&backup);
}

/* ============================================================================
 * Help Tests
 * ============================================================================ */

Test(options, help_client) {
  char *argv[] = {"client", "--help", NULL};
  int result = test_options_init_with_fork(argv, 2, true);
  cr_assert_eq(result, 0);
}

Test(options, help_server) {
  char *argv[] = {"server", "--help", NULL};
  int result = test_options_init_with_fork(argv, 2, false);
  cr_assert_eq(result, 0);
}

Test(options, help_short) {
  char *argv[] = {"client", "-h", NULL};
  int result = test_options_init_with_fork(argv, 2, true);
  cr_assert_eq(result, 0);
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

Test(options_errors, unknown_option) {
  char *argv[] = {"client", "--unknown-option", NULL};
  int result = test_options_init_with_fork(argv, 2, true);
  cr_assert_eq(result, 1);
}

Test(options_errors, missing_argument_address) {
  char *argv[] = {"client", "--address", NULL};
  log_set_level(LOG_DEBUG);
  int result = test_options_init_with_fork(argv, 2, true);
  cr_assert_eq(result, 1);
}

Test(options_errors, missing_argument_short) {
  char *argv[] = {"client", "-a", NULL};
  int result = test_options_init_with_fork(argv, 2, true);
  cr_assert_eq(result, 1);
}

Test(options_errors, missing_argument_port) {
  char *argv[] = {"client", "--port", NULL};
  log_set_level(LOG_DEBUG);
  int result = test_options_init_with_fork(argv, 2, true);
  cr_assert_eq(result, 1);
}

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

  char *argv[] = {"server",       "--address=0.0.0.0",
                  "--port=27224", "--palette=digital",
                  "--audio",      "--log-file=/var/log/ascii-chat.log",
                  "--encrypt",    "--keyfile=/etc/ascii-chat/key",
                  NULL};
  int argc = 8;

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

  char *argv[] = {"client",           "--address=0.0.0.0",   "--port=1", "--width=1", "--height=1",
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
