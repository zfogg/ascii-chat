/**
 * @file main.c
 * @ingroup main
 * @brief ascii-chat Unified Binary - Mode Dispatcher and Entry Point
 *
 * This file implements the main entry point for the unified ascii-chat binary,
 * which provides server, client, mirror, and discovery service functionality
 * in a single executable.
 *
 * The dispatcher now delegates ALL option parsing (including mode detection
 * and binary-level options) to options_init(), then simply dispatches to the
 * appropriate mode-specific entry point based on the detected mode.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 * @version 3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Mode-specific entry points
#include "server/main.h"
#include "client/main.h"
#include "mirror/main.h"
#include "discovery-service/main.h"
#include "discovery/main.h"

// Utilities
#include "util/utf8.h"
#include "util/time.h"
#include "util/string.h"

// Common headers for version info and initialization
#include "common.h"
#include "version.h"
#include "options/options.h"
#include "options/common.h"
#include "options/rcu.h"
#include "options/builder.h"
#include "log/logging.h"
#include "platform/terminal.h"
#include "util/path.h"

#ifndef NDEBUG
#include "asciichat_errno.h"
#include "debug/lock.h"
#endif

/* ============================================================================
 * Constants and Configuration
 * ============================================================================ */

#define APP_NAME "ascii-chat"
#define VERSION ASCII_CHAT_VERSION_FULL

/* ============================================================================
 * Mode Registration Table
 * ============================================================================ */

typedef int (*mode_entry_point_t)(void);

typedef struct {
  const char *name;
  const char *description;
  mode_entry_point_t entry_point;
} mode_descriptor_t;

static const mode_descriptor_t g_mode_table[] = {
    {
        .name = "server",
        .description = "Run as multi-client video chat server",
        .entry_point = server_main,
    },
    {
        .name = "client",
        .description = "Run as video chat client (connect to server)",
        .entry_point = client_main,
    },
    {
        .name = "mirror",
        .description = "View local webcam as ASCII art (no server)",
        .entry_point = mirror_main,
    },
    {
        .name = "discovery-service",
        .description = "Secure P2P session signalling",
        .entry_point = acds_main,
    },
    {.name = NULL, .description = NULL, .entry_point = NULL},
};

/* ============================================================================
 * Help and Usage Functions
 * ============================================================================ */

/**
 * @brief Helper to create colored option string for display
 *
 * Returns a string with ANSI color codes for proper coloring.
 * Caller must use the string immediately before calling again.
 */
static void print_usage(void) {
  // Print header
  printf("%s ascii-chat - %s %s\n", ASCII_CHAT_DESCRIPTION_EMOJI_L, ASCII_CHAT_DESCRIPTION_TEXT,
         ASCII_CHAT_DESCRIPTION_EMOJI_R);
  printf("\n");

  // Get binary options config with full metadata
  const options_config_t *config = options_preset_binary(NULL, NULL);
  if (!config) {
    fprintf(stderr, "Error: Failed to create options config\n");
    return;
  }

  // Print all sections programmatically (USAGE, MODES, MODE-OPTIONS, EXAMPLES, OPTIONS)
  options_config_print_usage(config, stdout);

  // Print project links
  print_project_links(stdout);

  // Cleanup
  options_config_destroy(config);
}

static void print_version(void) {
  printf("%s %s (%s, %s)\n", APP_NAME, VERSION, ASCII_CHAT_BUILD_TYPE, ASCII_CHAT_BUILD_DATE);
}

// Discovery mode is implicit (no keyword) so it has a separate descriptor
static const mode_descriptor_t g_discovery_mode = {
    .name = "discovery",
    .description = "P2P session with automatic host negotiation",
    .entry_point = discovery_main,
};

static const mode_descriptor_t *find_mode(asciichat_mode_t mode) {
  // Discovery mode is not in the table (it's implicit)
  if (mode == MODE_DISCOVERY) {
    return &g_discovery_mode;
  }

  for (const mode_descriptor_t *m = g_mode_table; m->name != NULL; m++) {
    switch (mode) {
    case MODE_SERVER:
      if (strcmp(m->name, "server") == 0)
        return m;
      break;
    case MODE_CLIENT:
      if (strcmp(m->name, "client") == 0)
        return m;
      break;
    case MODE_MIRROR:
      if (strcmp(m->name, "mirror") == 0)
        return m;
      break;
    case MODE_DISCOVERY_SERVER:
      if (strcmp(m->name, "discovery-service") == 0)
        return m;
      break;
    default:
      break;
    }
  }
  return NULL;
}

/* ============================================================================
 * Helper Functions for Post-Options Processing
 * ============================================================================ */

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(int argc, char *argv[]) {
  // Validate basic argument structure
  if (argc < 1 || argv == NULL || argv[0] == NULL) {
    fprintf(stderr, "Error: Invalid argument vector\n");
    return 1;
  }

  // Initialize terminal capabilities for proper color detection in help output
  log_redetect_terminal_capabilities();

  // Warn if Release build was built from dirty working tree
#if ASCII_CHAT_GIT_IS_DIRTY
  if (strcmp(ASCII_CHAT_BUILD_TYPE, "Release") == 0) {
    fprintf(stderr, "⚠️  WARNING: This Release build was compiled from a dirty git working tree!\n");
    fprintf(stderr, "    Git commit: %s (dirty)\n", ASCII_CHAT_GIT_COMMIT_HASH);
    fprintf(stderr, "    Build date: %s\n", ASCII_CHAT_BUILD_DATE);
    fprintf(stderr, "    For reproducible builds, commit or stash changes before building.\n\n");
  }
#endif

  asciichat_error_t options_result = options_init(argc, argv);
  if (options_result != ASCIICHAT_OK) {
    asciichat_error_context_t error_ctx;
    if (HAS_ERRNO(&error_ctx)) {
      fprintf(stderr, "Error: %s\n", error_ctx.context_message);
    } else {
      fprintf(stderr, "Error: Failed to initialize options\n");
    }
    return options_result;
  }

  // Get parsed options including detected mode
  const options_t *opts = options_get();
  if (!opts) {
    fprintf(stderr, "Error: Options not initialized\n");
    return 1;
  }

  // Determine default log filename based on detected mode
  const char *default_log_filename;
  switch (opts->detected_mode) {
  case MODE_SERVER:
    default_log_filename = "server.log";
    break;
  case MODE_CLIENT:
    default_log_filename = "client.log";
    break;
  case MODE_MIRROR:
    default_log_filename = "mirror.log";
    break;
  case MODE_DISCOVERY_SERVER:
    default_log_filename = "acds.log";
    break;
  case MODE_DISCOVERY:
    default_log_filename = "discovery.log";
    break;
  default:
    default_log_filename = "ascii-chat.log";
    break;
  }

  // Validate log file path if specified (security: prevent path traversal)
  const char *log_file_from_opts = (opts->log_file[0] != '\0') ? opts->log_file : NULL;
  if (log_file_from_opts && strlen(log_file_from_opts) > 0) {
    char *validated_log_file = NULL;
    asciichat_error_t log_path_result =
        path_validate_user_path(log_file_from_opts, PATH_ROLE_LOG_FILE, &validated_log_file);
    if (log_path_result != ASCIICHAT_OK || !validated_log_file || strlen(validated_log_file) == 0) {
      // Invalid log file path - warn and fall back to default
      fprintf(stderr, "WARNING: Invalid log file path '%s', using default '%s'\n", log_file_from_opts,
              default_log_filename);
      options_set_string("log_file", "");
      opts = options_get(); // Refresh pointer after update
      SAFE_FREE(validated_log_file);
    } else {
      // Replace log_file with validated path
      options_set_string("log_file", validated_log_file);
      opts = options_get(); // Refresh pointer after update
      SAFE_FREE(validated_log_file);
    }
  }

  // Determine if this mode uses client-like initialization (client and mirror modes)
  // Discovery mode is client-like (uses terminal display, webcam, etc.)
  bool is_client_or_mirror_mode = (opts->detected_mode == MODE_CLIENT || opts->detected_mode == MODE_MIRROR ||
                                   opts->detected_mode == MODE_DISCOVERY);

  // Detect terminal capabilities and apply color mode override for all output modes
  // This must happen before --help/--version to ensure colored help text
  terminal_capabilities_t caps = detect_terminal_capabilities();
  caps = apply_color_mode_override(caps);

  // Handle --help and --version (these are detected and flagged by options_init)
  // Terminal capabilities already initialized in main() at startup
  if (opts->help) {
    print_usage();
    return 0;
  }

  if (opts->version) {
    print_version();
    return 0;
  }

  // Handle client-specific --show-capabilities flag (exit after showing capabilities)
  if (is_client_or_mirror_mode && opts->show_capabilities) {
    // Print title with color
    (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_INFO, "Detected Terminal Capabilities:"));

    // Column alignment: calculate max label width
    const char *label_color_level = "Color Level:";
    const char *label_max_colors = "Max Colors:";
    const char *label_utf8 = "UTF-8 Support:";
    const char *label_render_mode = "Render Mode:";
    const char *label_term = "TERM:";
    const char *label_colorterm = "COLORTERM:";
    const char *label_detection = "Detection Reliable:";
    const char *label_bitmask = "Capabilities Bitmask:";

    size_t max_label_width = 0;
    max_label_width = MAX(max_label_width, strlen(label_color_level));
    max_label_width = MAX(max_label_width, strlen(label_max_colors));
    max_label_width = MAX(max_label_width, strlen(label_utf8));
    max_label_width = MAX(max_label_width, strlen(label_render_mode));
    max_label_width = MAX(max_label_width, strlen(label_term));
    max_label_width = MAX(max_label_width, strlen(label_colorterm));
    max_label_width = MAX(max_label_width, strlen(label_detection));
    max_label_width = MAX(max_label_width, strlen(label_bitmask));

#define PRINT_CAP_LINE(label, value_str, value_color)                                                                  \
  do {                                                                                                                 \
    (void)fprintf(stdout, "  %s", colored_string(LOG_COLOR_GREY, label));                                              \
    for (size_t i = strlen(label); i < max_label_width; i++) {                                                         \
      (void)fprintf(stdout, " ");                                                                                      \
    }                                                                                                                  \
    (void)fprintf(stdout, " %s\n", colored_string(value_color, value_str));                                            \
  } while (0)

    // Print Color Level
    const char *color_level_name = terminal_color_level_name(caps.color_level);
    PRINT_CAP_LINE(label_color_level, color_level_name, LOG_COLOR_DEV);

    // Print Max Colors
    char max_colors_str[32];
    (void)snprintf(max_colors_str, sizeof(max_colors_str), "%u", caps.color_count);
    PRINT_CAP_LINE(label_max_colors, max_colors_str, LOG_COLOR_DEV);

    // Print UTF-8 Support
    PRINT_CAP_LINE(label_utf8, caps.utf8_support ? "Yes" : "No", caps.utf8_support ? LOG_COLOR_INFO : LOG_COLOR_ERROR);

    // Print Render Mode
    const char *render_mode_str;
    if (caps.render_mode == RENDER_MODE_HALF_BLOCK) {
      render_mode_str = "half-block";
    } else if (caps.render_mode == RENDER_MODE_BACKGROUND) {
      render_mode_str = "background";
    } else {
      render_mode_str = "foreground";
    }
    PRINT_CAP_LINE(label_render_mode, render_mode_str, LOG_COLOR_DEV);

    // Print TERM
    PRINT_CAP_LINE(label_term, caps.term_type[0] ? caps.term_type : "Unknown", LOG_COLOR_DEV);

    // Print COLORTERM
    PRINT_CAP_LINE(label_colorterm, caps.colorterm[0] ? caps.colorterm : "(not set)", LOG_COLOR_DEV);

    // Print Detection Reliable
    PRINT_CAP_LINE(label_detection, caps.detection_reliable ? "Yes" : "No",
                   caps.detection_reliable ? LOG_COLOR_INFO : LOG_COLOR_ERROR);

    // Print Capabilities Bitmask
    char bitmask_str[32];
    (void)snprintf(bitmask_str, sizeof(bitmask_str), "0x%08x", caps.capabilities);
    PRINT_CAP_LINE(label_bitmask, bitmask_str, LOG_COLOR_GREY);

#undef PRINT_CAP_LINE

    (void)fflush(stdout);
    return 0;
  }

  // Initialize timer system BEFORE any subsystem that might use timing functions
  // This must be done before asciichat_shared_init which initializes palette, buffer pool, etc.
  if (!timer_system_init()) {
    fprintf(stderr, "FATAL: Failed to initialize timer system\n");
    return ERROR_PLATFORM_INIT;
  }
  (void)atexit(timer_system_cleanup);

  // Initialize shared subsystems (platform, logging, palette, buffer pool, cleanup)
  // For client/mirror modes, this also sets log_force_stderr(true) to route all logs to stderr
  asciichat_error_t init_result = asciichat_shared_init(default_log_filename, is_client_or_mirror_mode);
  if (init_result != ASCIICHAT_OK) {
    return init_result;
  }
  const char *final_log_file = (opts->log_file[0] != '\0') ? opts->log_file : default_log_filename;
  log_warn("Logging initialized to %s", final_log_file);

  // Client-specific: auto-detect piping and default to no color mode
  // This keeps stdout clean for piping: `ascii-chat client --snapshot | tee file.ascii_art`
  terminal_color_mode_t color_mode = opts->color_mode;
  if (is_client_or_mirror_mode && !platform_isatty(STDOUT_FILENO) && color_mode == COLOR_MODE_AUTO) {
    options_set_int("color_mode", COLOR_MODE_NONE);
    opts = options_get(); // Refresh pointer after update
    log_info("stdout is piped/redirected - defaulting to none (override with --color-mode)");
  }

#ifndef NDEBUG
  // Initialize lock debugging system after logging is fully set up
  log_debug("Initializing lock debug system...");
  int lock_debug_result = lock_debug_init();
  if (lock_debug_result != 0) {
    LOG_ERRNO_IF_SET("Lock debug system initialization failed");
    FATAL(ERROR_PLATFORM_INIT, "Lock debug system initialization failed");
  }
  log_debug("Lock debug system initialized successfully");
#endif

  // Set global FPS from command-line option if provided
  extern int g_max_fps;
  if (opts->fps > 0) {
    if (opts->fps < 1 || opts->fps > 144) {
      log_warn("FPS value %d out of range (1-144), using default", opts->fps);
    } else {
      g_max_fps = opts->fps;
      log_debug("Set FPS from command line: %d", g_max_fps);
    }
  }

  // Find and dispatch to mode entry point
  const mode_descriptor_t *mode = find_mode(opts->detected_mode);
  if (!mode) {
    fprintf(stderr, "Error: Mode not found for detected_mode=%d\n", opts->detected_mode);
    return 1;
  }

  // Call the mode-specific entry point
  // Mode entry points use options_get() to access parsed options
  int exit_code = mode->entry_point();

  if (exit_code == ERROR_USAGE) {
    _exit(ERROR_USAGE);
  }

  return exit_code;
}
