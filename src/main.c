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
#include "options/colors.h"
#include "log/logging.h"
#include "platform/terminal.h"
#include "util/path.h"
#include "ui/colors.h"

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

static void print_usage(asciichat_mode_t mode) {
  // Use the new usage() function from common.c which handles mode-specific help properly
  usage(stdout, mode);
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
    case MODE_DISCOVERY_SERVICE:
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
  // Set global argc/argv for early argv inspection (e.g., in terminal.c)
  g_argc = argc;
  g_argv = argv;
  // Validate basic argument structure
  if (argc < 1 || argv == NULL || argv[0] == NULL) {
    fprintf(stderr, "Error: Invalid argument vector\n");
    return 1;
  }

  // Detect terminal capabilities early so colored help output works
  log_redetect_terminal_capabilities();
  terminal_capabilities_t caps = detect_terminal_capabilities();
  caps = apply_color_mode_override(caps);

  // Initialize logging early so options parsing can log errors
  // Use generic filename for now; will be replaced with mode-specific filename once mode is detected
  // This will be reconfigured first in options_init() with mode-specific name,
  // then again in asciichat_shared_init() with final settings from parsed options
  log_init("ascii-chat.log", LOG_INFO, false, false);

  // Warn if Release build was built from dirty working tree
#if ASCII_CHAT_GIT_IS_DIRTY
  if (strcmp(ASCII_CHAT_BUILD_TYPE, "Release") == 0) {
    fprintf(stderr, "⚠️  WARNING: This Release build was compiled from a dirty git working tree!\n");
    fprintf(stderr, "    Git commit: %s (dirty)\n", ASCII_CHAT_GIT_COMMIT_HASH);
    fprintf(stderr, "    Build date: %s\n", ASCII_CHAT_BUILD_DATE);
    fprintf(stderr, "    For reproducible builds, commit or stash changes before building.\n\n");
  }
#endif

  // Handle --color-scheme-create early (before options_init) to avoid options parsing issues
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--color-scheme-create") == 0) {
      // Initialize colors system
      if (colors_init() != ASCIICHAT_OK) {
        fprintf(stderr, "Error: Failed to initialize color system\n");
        return ERROR_INIT;
      }

      // Parse scheme name and output file (both optional)
      const char *scheme_name = "pastel"; // default
      const char *output_file = NULL;

      // Check if next argument is a scheme name (no '-' prefix)
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        scheme_name = argv[i + 1];
        i++;

        // Check if argument after that is an output file
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          output_file = argv[i + 1];
          i++;
        }
      }

      // Export the scheme
      asciichat_error_t export_result = colors_export_scheme(scheme_name, output_file);
      colors_shutdown();

      if (export_result != ASCIICHAT_OK) {
        return export_result;
      }
      return ASCIICHAT_OK;
    }
  }

  // Load color scheme early (from config files and CLI) before logging initialization
  // This allows logging to use the correct colors from the start
  options_colors_init_early(argc, argv);

  asciichat_error_t options_result = options_init(argc, argv);
  if (options_result != ASCIICHAT_OK) {
    asciichat_error_context_t error_ctx;
    if (HAS_ERRNO(&error_ctx)) {
      fprintf(stderr, "Error: %s\n", error_ctx.context_message);
    } else {
      fprintf(stderr, "Error: Failed to initialize options\n");
    }
    (void)fflush(stderr);

    // Clean up options state before exiting
    options_state_shutdown();
    options_cleanup_schema();

    exit(options_result);
  }

  // Get parsed options including detected mode
  const options_t *opts = options_get();
  if (!opts) {
    fprintf(stderr, "Error: Options not initialized\n");
    return 1;
  }

  // Determine if this mode uses client-like initialization (client and mirror modes)
  // Discovery mode is client-like (uses terminal display, webcam, etc.)
  bool is_client_like_mode = (opts->detected_mode == MODE_CLIENT || opts->detected_mode == MODE_MIRROR ||
                              opts->detected_mode == MODE_DISCOVERY);

  // Handle --help and --version (these are detected and flagged by options_init)
  // Terminal capabilities already initialized before options_init() at startup
  if (opts->help) {
    print_usage(opts->detected_mode);
    return 0;
  }

  if (opts->version) {
    print_version();
    exit(0);
  }

  // Initialize timer system BEFORE any subsystem that might use timing functions
  // This must be done before asciichat_shared_init which initializes palette, buffer pool, etc.
  if (!timer_system_init()) {
    fprintf(stderr, "FATAL: Failed to initialize timer system\n");
    return ERROR_PLATFORM_INIT;
  }
  // Note: timer_system_cleanup will be registered by asciichat_shared_init

  // Initialize shared subsystems (platform, logging, palette, buffer pool, cleanup)
  // For client/mirror modes, this also sets log_force_stderr(true) to route all logs to stderr
  asciichat_error_t init_result = asciichat_shared_init(is_client_like_mode);
  if (init_result != ASCIICHAT_OK) {
    return init_result;
  }
  const char *final_log_file = (opts->log_file[0] != '\0') ? opts->log_file : "ascii-chat.log";
  log_warn("Logging initialized to %s", final_log_file);

  // Client-specific: auto-detect piping and route logs to stderr
  // This keeps stdout clean for piping: `ascii-chat client --snapshot | tee file.ascii_art`
  // Logs go to stderr when piped, regardless of color settings
  // Color settings are decided separately below
  terminal_color_mode_t color_mode = opts->color_mode;
  if (is_client_like_mode && !platform_isatty(STDOUT_FILENO)) {
    log_set_force_stderr(true); // Always route logs to stderr when stdout is piped
  }

  // Separately: auto-disable colors when piped (unless explicitly enabled with --color=true)
  // This respects user's explicit --color=true request while auto-disabling in other cases
  if (is_client_like_mode && !platform_isatty(STDOUT_FILENO) && color_mode == COLOR_MODE_AUTO &&
      opts->color != COLOR_SETTING_TRUE) {
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

  if (opts->fps > 0) {
    if (opts->fps < 1 || opts->fps > 144) {
      log_warn("FPS value %d out of range (1-144), using default", opts->fps);
    } else {
      log_debug("FPS set from command line: %d", opts->fps);
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
    exit(ERROR_USAGE);
  }

  return exit_code;
}
