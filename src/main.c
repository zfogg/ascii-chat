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
#include <ascii-chat/log/filter.h>
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
    .entry_point = discovery_main,
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

  // VERY FIRST: Scan for --color BEFORE ANY logging initialization
  // This sets global flags that persist through cleanup, enabling --color to force colors
  extern bool g_color_flag_passed;
  extern bool g_color_flag_value;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--color") == 0 || strcmp(argv[i], "--color=true") == 0) {
      g_color_flag_passed = true;
      g_color_flag_value = true;
      break;
    } else if (strcmp(argv[i], "--color=false") == 0) {
      g_color_flag_passed = true;
      g_color_flag_value = false;
      break;
    } else if (strncmp(argv[i], "--color=", 8) == 0) {
      g_color_flag_passed = true;
      // Default to true for any other --color=X value
      g_color_flag_value = true;
      break;
    }
  }

  // SECOND: Scan for --grep BEFORE ANY logging initialization
  // This ensures ALL logs (including from shared_init) can be filtered
  // Supports multiple --grep patterns (ORed together)
  for (int i = 1; i < argc - 1; i++) {
    if (strcmp(argv[i], "--grep") == 0) {
      const char *pattern = argv[i + 1];
      asciichat_error_t filter_result = log_filter_init(pattern);
      if (filter_result != ASCIICHAT_OK) {
        fprintf(stderr,
                "ERROR: Invalid --grep pattern or invalid flags: \"%s\" - use /pattern/flags format (e.g., "
                "\"/query/ig\" or \"/literal/F\")\n",
                pattern);
        return 1;
      }
      i++; // Skip the pattern argument
    }
  }

  // Detect terminal capabilities early so colored help output works
  // Logging will be initialized by asciichat_shared_init() before options_init()
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

  // Load color scheme early (from config files and CLI) before logging initialization
  // This allows logging to use the correct colors from the start
  options_colorscheme_init_early(argc, argv);

  // Parse --color-scheme from argv early to set logging colors for help output
  const char *colorscheme_name = "pastel"; // default
  for (int i = 1; i < argc - 1; i++) {
    if (strcmp(argv[i], "--color-scheme") == 0) {
      colorscheme_name = argv[i + 1];
      break;
    }
  }

  // Load and apply colorscheme to logging BEFORE options_init() so help gets colors
  color_scheme_t scheme;
  if (colorscheme_load_builtin(colorscheme_name, &scheme) == ASCIICHAT_OK) {
    log_set_color_scheme(&scheme);
  } else {
    // Fall back to default pastel if scheme not found
    if (colorscheme_load_builtin("pastel", &scheme) == ASCIICHAT_OK) {
      log_set_color_scheme(&scheme);
    }
  }

  // Initialize logging colors so they're ready for help output
  log_init_colors();

  // EARLY PARSE: Determine mode from argv to know if this is client-like mode
  // The first positional argument (after options) is the mode: client, server, mirror, discovery, acds
  bool is_client_like_mode = false;
  if (argc > 1) {
    const char *first_arg = argv[1];
    if (strcmp(first_arg, "client") == 0 || strcmp(first_arg, "mirror") == 0 || strcmp(first_arg, "discovery") == 0) {
      is_client_like_mode = true;
    }
  }

  // EARLY PARSE: Extract log file from argv (--log-file or -L)
  const char *log_file = "ascii-chat.log"; // default
  for (int i = 1; i < argc - 1; i++) {
    if ((strcmp(argv[i], "--log-file") == 0 || strcmp(argv[i], "-L") == 0)) {
      log_file = argv[i + 1];
      break;
    }
  }

  // Initialize shared subsystems BEFORE options_init()
  // This ensures options parsing can use properly configured logging with colors
  asciichat_error_t init_result = asciichat_shared_init(log_file, is_client_like_mode);
  if (init_result != ASCIICHAT_OK) {
    return init_result;
  }

  // Register cleanup of shared subsystems to run on normal exit
  // Library code doesn't call atexit() - the application is responsible
  (void)atexit(asciichat_shared_shutdown);

  // NOW parse all options - can use logging with colors!
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

  // Get parsed options
  const options_t *opts = options_get();
  if (!opts) {
    fprintf(stderr, "Error: Options not initialized\n");
    return 1;
  }

  // Reconfigure logging with parsed log level
  log_init(log_file, GET_OPTION(log_level), false, false);

  // Apply quiet mode OR status screen mode - both disable terminal output
  // Status screen captures logs in its buffer and displays them in the UI
  if (GET_OPTION(quiet) || (opts->detected_mode == MODE_SERVER && GET_OPTION(status_screen))) {
    log_set_terminal_output(false);
  }

  // Initialize palette based on command line options
  const char *custom_chars = opts && opts->palette_custom_set ? opts->palette_custom : NULL;
  if (apply_palette_config(GET_OPTION(palette_type), custom_chars) != 0) {
    FATAL(ERROR_CONFIG, "Failed to apply palette configuration");
  }

  // Set quiet mode for memory debugging
#if defined(DEBUG_MEMORY) && !defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
  debug_memory_set_quiet_mode(GET_OPTION(quiet));
#endif

  // Truncate log if it's already too large
  log_truncate_if_large();

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

  // For server mode with status screen: disable terminal output IMMEDIATELY
  // This must happen BEFORE any log_*() calls to prevent logs from appearing on terminal
  // The status screen will capture and display logs in its buffer instead
  if (opts->detected_mode == MODE_SERVER && opts->status_screen) {
    log_set_terminal_output(false);
  }

  const char *final_log_file = (opts->log_file[0] != '\0') ? opts->log_file : "ascii-chat.log";
  log_dev("Logging initialized to %s", final_log_file);

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
