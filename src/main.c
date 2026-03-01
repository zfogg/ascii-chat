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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>

// Mode-specific entry points
#include "server/main.h"
#include "client/main.h"
#include "mirror/main.h"
#include "discovery-service/main.h"
#include "discovery/main.h"
#include <ascii-chat/discovery/session.h>

// Global exit API (exported in main.h)
#include "main.h"

// Application callbacks for library integration
#include <ascii-chat/app_callbacks.h>

// Utilities
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/string.h>

// Common headers for version info and initialization
#include <ascii-chat/atomic.h>
#include <ascii-chat/common.h>
#include <ascii-chat/version.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/options/common.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/actions.h>
#include <ascii-chat/options/colorscheme.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/json.h>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/options/colorscheme.h>
#include <ascii-chat/network/update_checker.h>
#include <ascii-chat/ui/splash.h>

#ifndef NDEBUG
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/debug/sync.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/atomic.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/debug/backtrace.h>
#endif

#ifndef _WIN32
#include <signal.h>
#endif

/* ============================================================================
 * Constants and Configuration
 * ============================================================================ */

#define APP_NAME "ascii-chat"
#define VERSION ASCII_CHAT_VERSION_FULL

/* ============================================================================
 * Default Log File Path Determination
 * ============================================================================ */

/**
 * @brief Get the mode-specific log filename
 *
 * @param mode The ascii-chat mode enum
 * @return The mode-specific log filename (e.g., "server.log", "client.log")
 */
static const char *get_mode_log_filename(asciichat_mode_t mode) {
  switch (mode) {
  case MODE_SERVER:
    return "server.log";
  case MODE_CLIENT:
    return "client.log";
  case MODE_MIRROR:
    return "mirror.log";
  case MODE_DISCOVERY_SERVICE:
    return "acds.log";
  case MODE_DISCOVERY:
    return "discovery.log";
  case MODE_INVALID:
  default:
    return "ascii-chat.log";
  }
}

/**
 * @brief Generate the default log file path based on build type and mode
 *
 * Debug mode: Current working directory with mode-specific names
 *   - client.log, server.log, mirror.log, acds.log, discovery.log
 *
 * Non-debug mode: System temp directory with mode-specific names
 *   - /tmp/ascii-chat/ on Linux/macOS
 *   - %TEMP%\ascii-chat\ on Windows
 */
static void generate_default_log_path(asciichat_mode_t mode, char *buf, size_t buf_size) {
  if (!buf || buf_size == 0) {
    return;
  }

  // Get mode-specific log filename
  const char *log_filename = get_mode_log_filename(mode);

#ifndef NDEBUG
  // Debug mode: use current working directory
  SAFE_STRNCPY(buf, log_filename, buf_size - 1);
#else
  // Non-debug mode: use system temp directory
#ifdef _WIN32
  const char *temp_dir = SAFE_GETENV("TEMP");
  if (!temp_dir) {
    temp_dir = SAFE_GETENV("TMP");
  }
  if (!temp_dir) {
    temp_dir = "C:\\Windows\\Temp";
  }
  safe_snprintf(buf, buf_size, "%s\\ascii-chat\\%s", temp_dir, log_filename);
#else
  safe_snprintf(buf, buf_size, "/tmp/ascii-chat/%s", log_filename);
#endif
  // Create directory if it doesn't exist (non-debug mode only)
  char dir_path[PATH_MAX];
  SAFE_STRNCPY(dir_path, buf, sizeof(dir_path) - 1);
  char *last_slash = strrchr(dir_path, '/');
#ifdef _WIN32
  char *last_backslash = strrchr(dir_path, '\\');
  if (last_backslash && (!last_slash || last_backslash > last_slash)) {
    last_slash = last_backslash;
  }
#endif
  if (last_slash) {
    *last_slash = '\0';
    platform_mkdir_recursive(dir_path, 0700);
  }
#endif
}

/* ============================================================================
 * Global Application Exit State (Centralized Signal Handling)
 * ============================================================================ */

/** Global flag indicating application should exit (used by all modes) */
atomic_t g_should_exit ATOMIC_INIT_AUTO(g_should_exit);

// Register with descriptive name for debug output
#ifndef NDEBUG
#define REGISTER_GLOBAL_ATOMICS() \
  do { \
    static bool _global_atomics_registered = false; \
    if (!_global_atomics_registered) { \
      NAMED_REGISTER_ATOMIC(&g_should_exit, "application_exit_flag"); \
      _global_atomics_registered = true; \
    } \
  } while(0)
#else
#define REGISTER_GLOBAL_ATOMICS() do {} while(0)
#endif

/** Mode-specific interrupt callback (called from signal handlers) */
static void (*g_interrupt_callback)(void) = NULL;

/* ============================================================================
 * Global Exit API Implementation (from main.h)
 * ============================================================================ */

bool should_exit(void) {
  return atomic_load_bool(&g_should_exit);
}

void signal_exit(void) {
  // Note: This function may be called from a signal handler context where
  // other threads may hold mutex locks. We avoid log_console() here to prevent
  // deadlock (render loop holds terminal lock, signal handler can't acquire it).
  // The shutdown will be logged by normal thread context later.
  atomic_store_bool(&g_should_exit, true);
  void (*cb)(void) = g_interrupt_callback;
  if (cb) {
    cb();
  }
}

void set_interrupt_callback(void (*cb)(void)) {
  g_interrupt_callback = cb;
}

/* ============================================================================
 * Signal Handlers
 * ============================================================================ */

#ifndef _WIN32
/**
 * SIGTERM handler for graceful shutdown
 * Called when process receives SIGTERM (e.g., from timeout(1) or systemd)
 */
static void handle_sigterm(int sig) {
  (void)sig;
  // log_console() is async-signal-safe - uses atomic ops and platform_write_all()
  log_console(LOG_INFO, "Signal received - shutting down");

#ifndef NDEBUG
  // Trigger debug sync state printing on shutdown (async-signal-safe)
  debug_sync_trigger_print();
#endif

  // Call signal_exit() to set flag AND interrupt blocking socket operations
  signal_exit();
}
#endif

/**
 * Console Ctrl+C handler (called from signal dispatcher on all platforms)
 * Counts consecutive Ctrl+C presses - double press forces immediate exit
 */
static bool console_ctrl_handler(console_ctrl_event_t event) {
  if (event != CONSOLE_CTRL_C && event != CONSOLE_CTRL_BREAK && event != CONSOLE_CLOSE) {
    return false;
  }

  // Double Ctrl+C forces immediate exit
  static atomic_t ctrl_c_count = {0};
  static bool ctrl_c_count_registered = false;
  if (!ctrl_c_count_registered) {
    NAMED_REGISTER_ATOMIC(&ctrl_c_count, "ctrl_c_interrupt_count");
    ctrl_c_count_registered = true;
  }
  if (atomic_fetch_add_int(&ctrl_c_count, 1) + 1 > 1) {
    platform_force_exit(1);
  }

#ifndef NDEBUG
  // Trigger debug sync state printing on shutdown
  debug_sync_trigger_print();
#endif

  // Note: Don't use log_console() here - on Unix, this is called from SIGINT context
  // where SAFE_MALLOC may be holding its mutex, causing deadlock. Windows runs this
  // in a separate thread so it would be safe there, but we keep both paths identical
  // for simplicity. The shutdown message will appear from normal thread context.
  signal_exit();
  return true;
}

/* ============================================================================
 * Debug Signal Handlers (all modes)
 * ============================================================================ */

#ifndef NDEBUG
#ifndef _WIN32
/**
 * @brief Common SIGUSR1 handler - triggers synchronization debugging output in all modes
 * @param sig The signal number (unused, required by signal handler signature)
 */
static void common_handle_sigusr1(int sig) {
  (void)sig;
  debug_sync_trigger_print();
}

/**
 * @brief Common SIGUSR2 handler - triggers memory report in all modes
 * @param sig The signal number (unused, required by signal handler signature)
 */
static void common_handle_sigusr2(int sig) {
  (void)sig;
  // Log to stderr directly since we're in signal context
  // (avoid logging system which uses mutexes)
  write(STDERR_FILENO, "[SIGUSR2 received]\n", 19);
  debug_memory_trigger_report();
}
#endif
#endif

/**
 * Set up global signal handlers
 * Called once at startup before mode dispatch
 */
void setup_signal_handlers(void) {
  platform_set_console_ctrl_handler(console_ctrl_handler);

#ifndef _WIN32
  platform_signal_handler_t handlers[] = {
      {SIGINT, handle_sigterm},
      {SIGTERM, handle_sigterm},
      {SIGPIPE, SIG_IGN},
  };
  platform_register_signal_handlers(handlers, 3);
  log_debug("Signal handlers registered");
#endif
}

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

/**
 * @brief atexit() wrapper for terminal_cursor_show()
 *
 * atexit() expects void (*)(void) but terminal_cursor_show() returns
 * asciichat_error_t. This wrapper ignores the return value.
 */
static void on_exit_show_cursor(void) {
  (void)terminal_cursor_show();
}

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

  // Show cursor early in case a previous session crashed with it hidden
  (void)terminal_cursor_show();

  // Initialize the named registry for debugging (allows --debug-state to show registered synchronization primitives)
#ifndef NDEBUG
  named_init();
  // Initialize atomic operations debug tracking
  debug_atomic_init();
  // Register global atomics with descriptive names for debug sync state monitoring
  REGISTER_GLOBAL_ATOMICS();
  // Static atomics are now auto-registered via ATOMIC_INIT_AUTO macro at definition time
  // Register all packet types from the packet_type_t enum
  named_registry_register_packet_types();
#endif

  // Register the main thread IMMEDIATELY after named_init() to ensure it's available for all subsequent allocations
#ifndef NDEBUG
  NAMED_REGISTER_THREAD(asciichat_thread_self(), "main");
  // Also save main thread ID for memory reporting (must be very early)
  debug_sync_set_main_thread_id();
#endif

  // VERY FIRST: Scan for --color BEFORE ANY logging initialization
  // This sets global flags that persist through cleanup, enabling --color to force colors
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
      asciichat_error_t filter_result = grep_init(pattern);
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

  // EARLY PARSE: Find the mode position (first positional argument)
  // Binary-level options must appear BEFORE the mode
  int mode_position = -1;
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') {
      // Found a positional argument - this is the mode or session string
      mode_position = i;
      break;
    }

    // Skip option and its argument if needed
    // Check if this is an option that takes a required argument
    const char *arg = argv[i];
    const char *opt_name = arg;
    if (arg[0] == '-') {
      opt_name = arg + (arg[1] == '-' ? 2 : 1);
    }

    // Handle --option=value format
    if (strchr(opt_name, '=')) {
      continue; // Value is part of this arg, no need to skip next
    }

    // Options that take required arguments
    if (strcmp(arg, "--log-file") == 0 || strcmp(arg, "-L") == 0 || strcmp(arg, "--log-level") == 0 ||
        strcmp(arg, "--config") == 0 || strcmp(arg, "--color-scheme") == 0 || strcmp(arg, "--log-template") == 0) {
      if (i + 1 < argc) {
        i++; // Skip the argument value
      }
    }
  }

  // EARLY PARSE: Determine mode from argv to know if this is client-like mode
  // The first positional argument (after options) is the mode: client, server, mirror, discovery, acds
  bool is_client_like_mode = false;
  if (mode_position > 0) {
    const char *first_arg = argv[mode_position];
    if (strcmp(first_arg, "client") == 0 || strcmp(first_arg, "mirror") == 0 || strcmp(first_arg, "discovery") == 0) {
      is_client_like_mode = true;
    }
  }

  // EARLY PARSE: Extract log file from argv (--log-file or -L)
  // Must appear BEFORE the mode
  // Generate default log path based on build type and mode
  char default_log_path[PATH_MAX];
  asciichat_mode_t detected_mode = MODE_INVALID;
  if (mode_position > 0) {
    const char *mode_str = argv[mode_position];
    // Convert mode string to enum
    if (strcmp(mode_str, "server") == 0) {
      detected_mode = MODE_SERVER;
    } else if (strcmp(mode_str, "client") == 0) {
      detected_mode = MODE_CLIENT;
    } else if (strcmp(mode_str, "mirror") == 0) {
      detected_mode = MODE_MIRROR;
    } else if (strcmp(mode_str, "acds") == 0 || strcmp(mode_str, "discovery-service") == 0) {
      detected_mode = MODE_DISCOVERY_SERVICE;
    } else if (strcmp(mode_str, "discovery") == 0) {
      detected_mode = MODE_DISCOVERY;
    }
  }
  generate_default_log_path(detected_mode, default_log_path, sizeof(default_log_path));

  const char *log_file = default_log_path;
  int max_search = (mode_position > 0) ? mode_position : argc;
  for (int i = 1; i < max_search - 1; i++) {
    if ((strcmp(argv[i], "--log-file") == 0 || strcmp(argv[i], "-L") == 0)) {
      log_file = argv[i + 1];
      break;
    }
  }

  // EARLY PARSE: Check for --json (JSON logging format)
  // If JSON format is enabled, we'll use the JSON filename during early init
  // --json MUST appear BEFORE the mode
  bool early_json_format = false;
  if (mode_position > 0) {
    for (int i = 1; i < mode_position; i++) {
      if (strcmp(argv[i], "--json") == 0) {
        early_json_format = true;
        break;
      }
    }
  } else {
    // If no mode found, search entire argv
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--json") == 0) {
        early_json_format = true;
        break;
      }
    }
  }

  // EARLY PARSE: Extract log template from argv (--log-template)
  // Note: This is for format templates, different from --log-format which is the output format
  // Binary-level options must appear BEFORE the mode
  const char *early_log_template = NULL;
  bool early_log_template_console_only = false;
  for (int i = 1; i < max_search - 1; i++) {
    if (strcmp(argv[i], "--log-template") == 0) {
      early_log_template = argv[i + 1];
      break;
    }
  }
  for (int i = 1; i < max_search; i++) {
    if (strcmp(argv[i], "--log-format-console-only") == 0) {
      early_log_template_console_only = true;
      break;
    }
  }

  // Initialize shared subsystems BEFORE options_init()
  // This ensures options parsing can use properly configured logging with colors
  // If JSON format is requested, don't write text logs to file
  // All logs will be JSON formatted later once options_init() runs
  const char *early_log_file = early_json_format ? NULL : log_file;
  asciichat_error_t init_result = asciichat_shared_init(early_log_file, is_client_like_mode);
  if (init_result != ASCIICHAT_OK) {
    return init_result;
  }

  // Route logs to stderr if stdout is piped (MUST happen early, before options_init logs)
  // This keeps stdout clean for data output (e.g., --snapshot mode piped to file)
  if (terminal_should_force_stderr()) {
    log_set_force_stderr(true);
  }

  // Register cleanup of shared subsystems to run on normal exit
  // Library code doesn't call atexit() - the application is responsible
  // IMPORTANT: atexit handlers run in LIFO order - last registered runs first
  // So register order (top to bottom) = execution order (bottom to top):
  // 1. PCRE2 cleanup (runs last) - frees compiled regexes after all code stops using them
  // 2. Path cleanup (runs 3rd) - now a no-op but kept for API compatibility
  // 3. Shared destroy (runs 2nd) - frees all shared resources
  // 4. Cursor show (runs 1st) - shows cursor before exit
  (void)atexit(asciichat_pcre2_cleanup_all);
  (void)atexit(path_cleanup_thread_locals);
  (void)atexit(asciichat_shared_destroy);
  (void)atexit(on_exit_show_cursor);

  // SECRET: Check for --backtrace (debug builds only) BEFORE options_init()
  // Prints a backtrace and exits immediately - useful for debugging hangs
#ifndef NDEBUG
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--backtrace") == 0) {
      log_info("=== Backtrace at startup ===");
      platform_print_backtrace(0);
      log_info("=== End Backtrace ===");
      asciichat_shared_destroy();
      return 0;
    }
  }
#endif

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
    options_state_destroy();
    options_cleanup_schema();

    exit(options_result);
  }

  // Get parsed options
  const options_t *opts = options_get();
  if (!opts) {
    fprintf(stderr, "Error: Options not initialized\n");
    return 1;
  }

  // Determine final log file path (use mode-specific default from options if available)
  // Determine output format early to decide on filename and logging strategy
  bool use_json_logging = GET_OPTION(json);

  // Determine the log filename
  // Check if user explicitly passed --log-file (not just the mode-specific default from options_init)
  bool user_specified_log_file = false;
  for (int i = 1; i < argc - 1; i++) {
    if (strcmp(argv[i], "--log-file") == 0 || strcmp(argv[i], "-L") == 0) {
      user_specified_log_file = true;
      break;
    }
  }

  const char *final_log_file = opts->log_file;
  char json_filename_buf[256];

  if (use_json_logging) {
    // When JSON logging is enabled, determine JSON output filename
    if (user_specified_log_file) {
      // User explicitly specified a log file - use it exactly for JSON output
      SAFE_STRNCPY(json_filename_buf, final_log_file, sizeof(json_filename_buf) - 1);
    } else {
      // Using default: replace .log with .json in the mode-specific default
      // e.g., server.log -> server.json, mirror.log -> mirror.json, etc.
      size_t len = strlen(final_log_file);
      if (len > 4 && strcmp(&final_log_file[len - 4], ".log") == 0) {
        // File ends with .log - replace with .json
        SAFE_STRNCPY(json_filename_buf, final_log_file, sizeof(json_filename_buf) - 1);
        // Replace .log with .json
        strcpy(&json_filename_buf[len - 4], ".json");
      } else {
        // File doesn't end with .log - just append .json
        SAFE_STRNCPY(json_filename_buf, final_log_file, sizeof(json_filename_buf) - 1);
        strncat(json_filename_buf, ".json", sizeof(json_filename_buf) - strlen(json_filename_buf) - 1);
      }
    }
    final_log_file = json_filename_buf;
    // For JSON mode: Initialize logging without file (text will be disabled)
    // We'll set up JSON output separately below
    // Note: log_init() was already called in asciichat_shared_init()
  } else {
    // Text logging mode: use the file from options (which is mode-specific default or user-specified)
    // Note: log_init() was already called in asciichat_shared_init()
  }

  // Apply custom log template if specified (use early parsed value if available, otherwise use options)
  const char *final_format = early_log_template ? early_log_template : GET_OPTION(log_template);
  bool final_format_console_only =
      early_log_template ? early_log_template_console_only : GET_OPTION(log_format_console_only);
  if (final_format && final_format[0] != '\0') {
    asciichat_error_t fmt_result = log_set_format(final_format, final_format_console_only);
    if (fmt_result != ASCIICHAT_OK) {
      log_error("Failed to apply custom log format");
    }
  }

  // Configure JSON output if requested
  if (use_json_logging) {
    // Open the JSON file for output
    int json_fd = platform_open("main_log_file", final_log_file, O_CREAT | O_RDWR | O_TRUNC, FILE_PERM_PRIVATE);
    if (json_fd >= 0) {
      log_set_json_output(json_fd);
    } else {
      // Failed to open JSON file - report error and exit
      SET_ERRNO(ERROR_CONFIG, "Failed to open JSON output file: %s", final_log_file);
      return ERROR_CONFIG;
    }
  }

  // Initialize colors now that logging is fully initialized
  // This must happen after log_init() since log_init_colors() checks if g_log.initialized
  log_init_colors();

  // Apply quiet mode - disables terminal output
  // Status screen mode only disables terminal output if terminal is interactive
  // In non-interactive mode (piped output), logs go to stdout/stderr normally
  if (GET_OPTION(quiet) ||
      (opts->detected_mode == MODE_SERVER && GET_OPTION(status_screen) && terminal_is_interactive())) {
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
    fflush(NULL);
    _Exit(0);
  }

  if (opts->version) {
    log_set_terminal_output(true);
    action_show_version();
    // action_show_version() calls _Exit(), so we don't reach here
  }

#ifndef NDEBUG
  // Handle --debug-state (debug builds only)
  // Sleep for specified time AFTER mode initialization so locks are created
  if (IS_OPTION_EXPLICIT(debug_sync_state_time, opts) && opts->debug_sync_state_time > 0.0) {
    log_info("Will print sync state after %f seconds", opts->debug_sync_state_time);
  }
#endif

  // For server mode with status screen: disable terminal output only if interactive
  // In non-interactive mode (piped output), logs go to stdout/stderr normally
  // The status screen (when shown) will capture and display logs in its buffer instead
  if (opts->detected_mode == MODE_SERVER && opts->status_screen && terminal_is_interactive()) {
    log_set_terminal_output(false);
  }

  log_dev("Logging initialized to %s", final_log_file);

  // Note: We do NOT auto-disable colors when stdout appears to be piped, because:
  // 1. Tools like ripgrep can display ANSI colors when piped
  // 2. Sandboxed/containerized environments may report false positives for isatty()
  // 3. Users can explicitly disable colors with --color=false if needed
  // Color behavior is now fully controlled by --color and --color-mode options.

#ifndef NDEBUG
  // Initialize lock debugging system after logging is fully set up
  log_debug("Initializing lock debug system...");
  int debug_sync_result = debug_sync_init();
  if (debug_sync_result != 0) {
    LOG_ERRNO_IF_SET("Debug sync system initialization failed");
    FATAL(ERROR_PLATFORM_INIT, "Debug sync system initialization failed");
  }
  log_debug("Debug sync system initialized successfully");

  // Initialize memory debug system
  // TEMPORARILY DISABLED: these calls hang in non-interactive mode
  // log_debug("Initializing memory debug system...");
  // int debug_memory_result = debug_memory_thread_init();
  // if (debug_memory_result != 0) {
  //   LOG_ERRNO_IF_SET("Memory debug system initialization failed");
  //   FATAL(ERROR_PLATFORM_INIT, "Memory debug system initialization failed");
  // }
  // log_debug("Memory debug system initialized successfully");

  // Start memory debug (now just prints report directly)
  // if (debug_memory_thread_start() != 0) {
  //   LOG_ERRNO_IF_SET("Memory debug startup failed");
  //   FATAL(ERROR_THREAD, "Memory debug startup failed");
  // }

#ifndef _WIN32
  // Unblock SIGUSR1 and SIGUSR2 at process level to ensure delivery
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  sigaddset(&set, SIGUSR2);
  sigprocmask(SIG_UNBLOCK, &set, NULL);

  // Register SIGUSR1 to trigger synchronization debugging output in all modes
  signal_handler_t old_usr1 = platform_signal(SIGUSR1, common_handle_sigusr1);
  if (old_usr1 == SIG_ERR) {
    log_warn("Failed to register SIGUSR1 handler");
  } else {
    log_debug("SIGUSR1 handler registered successfully");
  }

  // Register SIGUSR2 to trigger memory report in all modes
  signal_handler_t old_usr2 = platform_signal(SIGUSR2, common_handle_sigusr2);
  if (old_usr2 == SIG_ERR) {
    log_warn("Failed to register SIGUSR2 handler");
  } else {
    log_debug("SIGUSR2 handler registered successfully");
  }
#endif
#endif

  if (opts->fps > 0) {
    if (opts->fps < 1 || opts->fps > 144) {
      log_warn("FPS value %d out of range (1-144), using default", opts->fps);
    } else {
      log_debug("FPS set from command line: %d", opts->fps);
    }
  }

  // Automatic update check at startup (once per week maximum)
  if (!GET_OPTION(no_check_update)) {
    update_check_result_t update_result;
    asciichat_error_t update_err = update_check_startup(&update_result);
    if (update_err == ASCIICHAT_OK && update_result.update_available) {
      char notification[1024];
      update_check_format_notification(&update_result, notification, sizeof(notification));
      log_info("%s", notification);

      // Set update notification for splash/status screens
      splash_set_update_notification(notification);
    }
  }

  // Set up global signal handlers BEFORE mode dispatch
  // All modes use the same centralized exit mechanism
  setup_signal_handlers();

  // Register application callbacks so lib code can check exit flags
  // This connects the render loop's should_exit check to signal_exit()
  static const app_callbacks_t app_callbacks = {
      .should_exit = should_exit,
      .signal_exit = signal_exit,
  };
  app_callbacks_register(&app_callbacks);

  // Register shutdown callback so splash thread and other code can check for exit
  shutdown_register_callback((shutdown_check_fn)should_exit);

#ifndef NDEBUG
  // Start debug threads now, after initialization but before mode entry
  // This avoids lock contention during critical initialization phase
  // Note: debug_sync_start_thread() and debug_memory_thread_start() now use
  // direct pthread calls instead of mutex_init/cond_init to avoid named registry
  // deadlock during thread startup
  if (debug_sync_start_thread() != 0) {
    LOG_ERRNO_IF_SET("Debug sync thread startup failed");
    FATAL(ERROR_THREAD, "Debug sync thread startup failed");
  }
  log_debug("Debug sync thread started");

#endif

  // Find and dispatch to mode entry point
  const mode_descriptor_t *mode = find_mode(opts->detected_mode);
  if (!mode) {
    fprintf(stderr, "Error: Mode not found for detected_mode=%d\n", opts->detected_mode);
    return 1;
  }

  // Call the mode-specific entry point
  // Mode entry points use options_get() to access parsed options
  int exit_code = mode->entry_point();

#ifndef NDEBUG
  // Clean up named registry (debug builds only)
  named_destroy();
#endif

  if (exit_code == ERROR_USAGE) {
    exit(ERROR_USAGE);
  }

  return exit_code;
}
