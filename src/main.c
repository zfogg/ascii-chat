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

// Mode-specific entry point
#include "server/main.h"
#include "client/main.h"
#include "mirror/main.h"
#include "discovery-server/main.h"

// Common headers for version info and initialization
#include "common.h"
#include "version.h"
#include "options/options.h"
#include "options/rcu.h"
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
        .name = "discovery-server",
        .description = "Run discovery service for session management",
        .entry_point = acds_main,
    },
    {.name = NULL, .description = NULL, .entry_point = NULL},
};

/* ============================================================================
 * Help and Usage Functions
 * ============================================================================ */

static void print_usage(void) {
#ifdef _WIN32
  const char *binary_name = "ascii-chat.exe";
#else
  const char *binary_name = "ascii-chat";
#endif

  printf("%s ascii-chat - %s %s\n", ASCII_CHAT_DESCRIPTION_EMOJI_L, ASCII_CHAT_DESCRIPTION_TEXT,
         ASCII_CHAT_DESCRIPTION_EMOJI_R);
  printf("\n");
  printf("USAGE:\n");
  printf("  %s [options] <mode> [mode-options...]\n", binary_name);
  printf("\n");
  printf("EXAMPLES:\n");
  printf("  %s server                    Start server on default port 27224\n", binary_name);
  printf("  %s client                    Connect to localhost:27224\n", binary_name);
  printf("  %s client example.com        Connect to example.com:27224\n", binary_name);
  printf("\n");
  printf("OPTIONS:\n");
  printf("  --help                       Show this help\n");
  printf("  --version                    Show version information\n");
  printf("  --config FILE                Load configuration from FILE\n");
  printf("  --config-create [FILE]       Create default config and exit\n");
  printf("  -L --log-file FILE           Redirect logs to FILE\n");
  printf("  --log-level LEVEL            Set log level: dev, debug, info, warn, error, fatal\n");
  printf("  -V --verbose                 Increase log verbosity (stackable: -VV, -VVV)\n");
  printf("  -q --quiet                   Disable console logging (log to file only)\n");
  printf("\n");
  printf("MODES:\n");
  for (const mode_descriptor_t *mode = g_mode_table; mode->name != NULL; mode++) {
    printf("  %-18s  %s\n", mode->name, mode->description);
  }
  printf("\n");
  printf("MODE-OPTIONS:\n");
  printf("  %s <mode> --help             Show options for a mode\n", binary_name);
  printf("\n");
  printf("🔗 https://ascii-chat.com\n");
  printf("🔗 https://github.com/zfogg/ascii-chat\n");
}

static void print_version(void) {
  printf("%s %s (%s, %s)\n", APP_NAME, VERSION, ASCII_CHAT_BUILD_TYPE, ASCII_CHAT_BUILD_DATE);
}

static const mode_descriptor_t *find_mode(asciichat_mode_t mode) {
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
      if (strcmp(m->name, "discovery-server") == 0)
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

static void update_log_file_updater(options_t *opts, void *context) {
  const char *validated_path = (const char *)context;
  SAFE_STRNCPY(opts->log_file, validated_path, sizeof(opts->log_file));
}

static void clear_log_file_updater(options_t *opts, void *context) {
  (void)context;
  opts->log_file[0] = '\0';
}

static void update_color_mode_to_none_updater(options_t *opts, void *context) {
  (void)context;
  opts->color_mode = COLOR_MODE_NONE;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(int argc, char *argv[]) {
  // Validate basic argument structure
  if (argc < 1 || argv == NULL || argv[0] == NULL) {
    fprintf(stderr, "Error: Invalid argument vector\n");
    return 1;
  }

  // Warn if Release build was built from dirty working tree
#if ASCII_CHAT_GIT_IS_DIRTY
  if (strcmp(ASCII_CHAT_BUILD_TYPE, "Release") == 0) {
    fprintf(stderr, "⚠️  WARNING: This Release build was compiled from a dirty git working tree!\n");
    fprintf(stderr, "    Git commit: %s (dirty)\n", ASCII_CHAT_GIT_COMMIT_HASH);
    fprintf(stderr, "    Build date: %s\n", ASCII_CHAT_BUILD_DATE);
    fprintf(stderr, "    For reproducible builds, commit or stash changes before building.\n\n");
  }
#endif

  // Case: No arguments - show usage
  if (argc == 1) {
    print_usage();
    return 0;
  }

  // UNIFIED OPTION INITIALIZATION
  // This single call handles:
  // - Mode detection from command-line arguments (including session string auto-detection)
  // - Binary-level option parsing (--help, --version, --log-file, etc.)
  // - Mode-specific option parsing
  // - Configuration file loading
  // - Post-processing and validation
  //
  // Session String Detection (Phase 1 ACDS):
  // When a positional argument matches word-word-word pattern (e.g., "swift-river-mountain"),
  // it's automatically detected as a session string by options_init(), which:
  // 1. Sets detected_mode to MODE_CLIENT
  // 2. Stores the session string in options.session_string
  // 3. Triggers automatic discovery on LAN (mDNS) and/or internet (ACDS)
  // This enables: `ascii-chat swift-river-mountain` (no explicit "client" mode needed)
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

  // Handle --help and --version (these are detected and flagged by options_init)
  if (opts->help) {
    print_usage();
    return 0;
  }

  if (opts->version) {
    print_version();
    return 0;
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
      options_update(clear_log_file_updater, NULL);
      opts = options_get(); // Refresh pointer after update
      SAFE_FREE(validated_log_file);
    } else {
      // Replace log_file with validated path
      options_update(update_log_file_updater, validated_log_file);
      opts = options_get(); // Refresh pointer after update
      SAFE_FREE(validated_log_file);
    }
  }

  // Determine if this mode uses client-like initialization (client and mirror modes)
  bool is_client_or_mirror_mode = (opts->detected_mode == MODE_CLIENT || opts->detected_mode == MODE_MIRROR);

  // Handle client-specific --show-capabilities flag (exit after showing capabilities)
  if (is_client_or_mirror_mode && opts->show_capabilities) {
    terminal_capabilities_t caps = detect_terminal_capabilities();
    caps = apply_color_mode_override(caps);
    print_terminal_capabilities(&caps);
    return 0;
  }

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
    options_update(update_color_mode_to_none_updater, NULL);
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
