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
#include <ascii-chat/discovery/session.h>

// Utilities
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/string.h>

// Common headers for version info and initialization
#include <ascii-chat/common.h>
#include <ascii-chat/version.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/common.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/colorscheme.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/options/colorscheme.h>

#ifndef NDEBUG
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/debug/lock.h>
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
    .entry_point = acds_main,
};

/**
 * @brief Mode lookup table for O(1) mode dispatch (direct indexing)
 *
 * Maps asciichat_mode_t enum values directly to mode descriptors for fast lookup.
 * This eliminates the O(n) loop+switch pattern in the previous implementation.
 *
 * Must match the order of asciichat_mode_t enum in options.h:
 * - MODE_SERVER = 0
 * - MODE_CLIENT = 1
 * - MODE_MIRROR = 2
 * - MODE_DISCOVERY_SERVICE = 3
 * - MODE_DISCOVERY = 4
 * - MODE_INVALID = 5
 *
 * @note MODE_INVALID is reserved and has no descriptor
 */
static const mode_descriptor_t *g_mode_lookup[] = {
    [MODE_SERVER] = &g_mode_table[0],            // server_main
    [MODE_CLIENT] = &g_mode_table[1],            // client_main
    [MODE_MIRROR] = &g_mode_table[2],            // mirror_main
    [MODE_DISCOVERY_SERVICE] = &g_mode_table[3], // acds_main
    [MODE_DISCOVERY] = &g_discovery_mode,        // discovery_main (implicit mode)
    [MODE_INVALID] = NULL,                       // Invalid: no descriptor
};

/**
 * @brief Find mode descriptor by asciichat_mode_t enum (O(1) direct lookup)
 *
 * Returns the mode descriptor for the given mode by direct indexing into
 * the lookup table. This is much faster than the previous loop+switch approach.
 *
 * @param mode The mode enum value
 * @return Pointer to mode descriptor, or NULL if mode is invalid
 */
static const mode_descriptor_t *find_mode(asciichat_mode_t mode) {
  // Bounds check: ensure mode is within valid range (0 to MODE_INVALID)
  if (mode < MODE_SERVER || mode > MODE_INVALID) {
    return NULL;
  }
  return g_mode_lookup[mode];
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

  // Detect early if stdout is piped so we can route logs to stderr from the start
  // This must be done BEFORE any logging, including terminal detection
  // This prevents log output from corrupting piped frame data during initialization
  bool stdout_is_piped = !platform_isatty(STDOUT_FILENO);

  // Initialize logging early so options parsing and terminal detection can log errors to stderr
  // Use generic filename for now; will be replaced with mode-specific filename once mode is detected
  // This will be reconfigured first in options_init() with mode-specific name,
  // then again in asciichat_shared_init() with final settings from parsed options
  // When piped, suppress DEBUG logs entirely to avoid corrupting frame data with debug output
  log_level_t init_log_level = stdout_is_piped ? LOG_INFO : LOG_DEBUG;
  log_init("ascii-chat.log", init_log_level, stdout_is_piped, false);

  // Detect terminal capabilities early so colored help output works
  // With force_stderr already set, these logs go to stderr, not stdout
  log_redetect_terminal_capabilities();
  terminal_capabilities_t caps = detect_terminal_capabilities();
  caps = apply_color_mode_override(caps);

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
      if (colorscheme_init() != ASCIICHAT_OK) {
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
      asciichat_error_t export_result = colorscheme_export_scheme(scheme_name, output_file);
      colorscheme_shutdown();

      if (export_result != ASCIICHAT_OK) {
        return export_result;
      }
      return ASCIICHAT_OK;
    }
  }

  // Load color scheme early (from config files and CLI) before logging initialization
  // This allows logging to use the correct colors from the start
  options_colorscheme_init_early(argc, argv);

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
    // Log to file only, not terminal - avoid polluting piped stdout when stderr is redirected to stdout (2>&1)
    log_debug("stdout is piped/redirected - defaulting to none (override with --color-mode)");
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
