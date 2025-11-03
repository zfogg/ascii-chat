/**
 * @file main.c
 * @ingroup main
 * @brief ascii-chat Unified Binary - Mode Dispatcher and Entry Point
 *
 * This file implements the main entry point for the unified ascii-chat binary,
 * which provides both server and client functionality in a single executable.
 * The dispatcher parses the first command line argument to determine which mode
 * to run and forwards execution to the appropriate mode-specific entry point.
 *
 * ## Unified Binary Architecture
 *
 * The ascii-chat application consolidates server and client into one binary:
 *
 * **Command Syntax:**
 * ```
 * ascii-chat <mode> [options...]
 * ascii-chat --help
 * ascii-chat --version
 * ```
 *
 * **Available Modes:**
 * - **server** - Multi-client video chat server with ASCII frame mixing
 * - **client** - Video chat client with webcam capture and terminal display
 *
 * **Design Benefits:**
 * - Single binary simplifies installation and deployment
 * - Shared library code reduces disk space usage
 * - Unified versioning eliminates client/server mismatch issues
 * - Easier testing and packaging (one artifact to build)
 *
 * ## Mode Dispatching Logic
 *
 * The dispatcher implements a clean separation of concerns:
 *
 * 1. **Top-Level Help** (`--help`, no args) - Shows mode selection help
 * 2. **Version Info** (`--version`) - Shows unified binary version
 * 3. **Mode-Specific Help** (`server --help`, `client --help`) - Shows mode options
 * 4. **Mode Dispatch** - Forwards argc/argv to server_main() or client_main()
 *
 * ## Implementation Strategy
 *
 * The dispatcher uses a function pointer table pattern for clean, extensible design:
 * - Mode entry points have identical signatures: `int mode_main(int argc, char *argv[])`
 * - Mode registration is centralized in a single table structure
 * - Adding new modes requires only: (1) implement mode_main(), (2) add table entry
 *
 * ## Error Handling
 *
 * The dispatcher validates mode selection before dispatch:
 * - **Invalid mode** → Print error and usage, exit with ERROR_USAGE
 * - **No mode specified** → Show help message, exit with 0
 * - **--help/--version** → Show info and exit with 0
 * - **Mode execution failure** → Return mode's error code unchanged
 *
 * ## Platform Compatibility
 *
 * The unified binary works identically across all supported platforms:
 * - **Linux/Unix** - Standard binary with mode argument
 * - **macOS** - Standard binary with mode argument
 * - **Windows** - .exe binary with mode argument (e.g., `ascii-chat.exe server`)
 *
 * Optional backwards compatibility via symlinks:
 * - `ascii-chat server` → `ascii-chat server`
 * - `ascii-chat client` → `ascii-chat client`
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 * @version 2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mode-specific entry point headers
#include "server/main.h"
#include "client/main.h"

// Common headers for version info
#include "common.h"
#include "version.h"

/* ============================================================================
 * Constants and Configuration
 * ============================================================================ */

/**
 * @def APP_NAME
 * @brief Application name for help and error messages
 *
 * Used consistently throughout the dispatcher for user-facing output.
 * Defined as a macro to allow easy customization if needed.
 */
#define APP_NAME "ascii-chat"

/**
 * @def VERSION
 * @brief Version string from CMake-generated header
 *
 * The version is set during build time by CMake and comes from
 * version.h. It includes major, minor, and patch version numbers.
 */
#define VERSION ASCII_CHAT_VERSION_FULL

/* ============================================================================
 * Mode Registration Table
 * ============================================================================ */

/**
 * @brief Mode entry point function pointer type
 *
 * All mode entry points must match this signature to be callable
 * by the dispatcher. The signature is identical to standard main().
 *
 * @param argc Argument count (at least 1, program name is always first)
 * @param argv Argument vector (argv[0] is program name, rest are options)
 * @return Exit code (0 for success, non-zero for error)
 *
 * @note The dispatcher strips the mode name from argv before calling
 *       mode entry points, so mode handlers don't need to skip it.
 * @note Mode entry points should use ERROR_USAGE for invalid arguments.
 *
 * @ingroup main
 */
typedef int (*mode_entry_point_t)(int argc, char *argv[]);

/**
 * @brief Mode descriptor structure
 *
 * Defines all metadata needed to dispatch to a mode:
 * - Name: Command line argument to trigger this mode (e.g., "server")
 * - Description: Brief summary for help text
 * - Entry point: Function pointer to mode_main() implementation
 *
 * @ingroup main
 */
typedef struct {
  const char *name;               ///< Mode name (e.g., "server", "client") - must be non-NULL for valid modes
  const char *description;        ///< One-line description for help text display
  mode_entry_point_t entry_point; ///< Function pointer to mode_main() implementation
} mode_descriptor_t;

/**
 * @brief Mode registration table
 *
 * Central registry of all available modes. To add a new mode:
 * 1. Implement mode_main() in new source file
 * 2. Create mode header (e.g., mode_name_mode.h)
 * 3. Add entry to this table
 *
 * The table is NULL-terminated for iteration convenience. The last entry
 * must have all fields set to NULL to mark the end of the table.
 *
 * @note Table is static const for compile-time initialization
 * @note Mode names must be unique (case-sensitive matching)
 * @ingroup main
 */
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
    // NULL terminator for table iteration
    {.name = NULL, .description = NULL, .entry_point = NULL},
};

/* ============================================================================
 * Help and Usage Functions
 * ============================================================================ */

/**
 * @brief Print top-level usage information
 *
 * Displays the main help screen showing available modes and global options.
 * This is shown when:
 * - User runs `ascii-chat` with no arguments
 * - User runs `ascii-chat --help` or `ascii-chat -h`
 * - User specifies an invalid mode
 *
 * The output includes:
 * - Application name and description
 * - Usage syntax
 * - List of available modes from the registration table
 * - Instructions for mode-specific help
 *
 * @param program_name Argv[0] from main (usually "ascii-chat")
 *                    Currently unused - binary name is determined at compile time
 *
 * @note Platform-specific binary name (e.g., "ascii-chat.exe" on Windows)
 * @ingroup main
 */
static void print_usage(const char *program_name) {
  (void)program_name; // Unused - we use "ascii-chat" directly

  // Use platform-specific binary name
#ifdef _WIN32
  const char *binary_name = "ascii-chat.exe";
#else
  const char *binary_name = "ascii-chat";
#endif

  printf("%s ascii-chat - %s %s\n", ASCII_CHAT_DESCRIPTION_EMOJI_L, ASCII_CHAT_DESCRIPTION_TEXT,
         ASCII_CHAT_DESCRIPTION_EMOJI_R);
  printf("\n");
  printf("USAGE:\n");
  printf("  %s <mode> [options...]\n", binary_name);
  printf("  %s --help\n", binary_name);
  printf("  %s --version\n", binary_name);
  printf("\n");
  printf("MODES:\n");
  for (const mode_descriptor_t *mode = g_mode_table; mode->name != NULL; mode++) {
    printf("  %-6s  %s\n", mode->name, mode->description);
  }
  printf("\n");
  printf("MODE-SPECIFIC HELP:\n");
  printf("  %s <mode> --help     Show mode-specific options\n", binary_name);
  printf("\n");
  printf("️🔗 https://github.com/zfogg/ascii-chat\n");
}

/**
 * @brief Print version information
 *
 * Displays the unified binary version and build information.
 * Version is defined by CMake during build process.
 *
 * The output includes:
 * - Application name and version string
 * - Compiler information (Clang, GCC, MSVC, etc.)
 * - C library information (musl, glibc, MSVCRT, libSystem, etc.)
 * - Platform information (OS name)
 * - Project URL
 *
 * @note All information is determined at compile time
 * @ingroup main
 */
static void print_version() {
  printf("%s %s\n", APP_NAME, VERSION);
  printf("\n");

  printf("Built with:\n");

#ifdef __clang__
  printf("  Compiler: Clang %s\n", __clang_version__);
#elif defined(__GNUC__)
  printf("  Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  printf("  Compiler: MSVC %d\n", _MSC_VER);
#else
  printf("  Compiler: Unknown\n");
#endif

#ifdef USE_MUSL
  printf("  C Library: musl\n");
#elif defined(__GLIBC__)
  printf("  C Library: glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#elif defined(_WIN32)
  printf("  C Library: MSVCRT\n");
#elif defined(__APPLE__)
  printf("  C Library: libSystem\n");
#else
  printf("  C Library: Unknown\n");
#endif

  printf("  Platform: %s\n", ASCII_CHAT_OS);

  printf("\n");
  printf("For more information: https://github.com/zfogg/ascii-chat\n");
}

/* ============================================================================
 * Mode Dispatch Logic
 * ============================================================================ */

/**
 * @brief Find mode descriptor by name
 *
 * Searches the mode table for a matching mode name. This implements
 * a simple linear search, which is acceptable given the small number
 * of modes (2-5 typically).
 *
 * @param mode_name Mode name to search for (e.g., "server")
 *                 Must be non-NULL and null-terminated
 * @return Pointer to mode descriptor if found, NULL if not found or invalid input
 *
 * @note Mode name matching is case-sensitive
 * @note Searches until NULL terminator is found in table
 * @ingroup main
 */
static const mode_descriptor_t *find_mode(const char *mode_name) {
  for (const mode_descriptor_t *mode = g_mode_table; mode->name != NULL; mode++) {
    if (strcmp(mode->name, mode_name) == 0) {
      return mode;
    }
  }
  return NULL;
}

/**
 * @brief Main entry point for unified ascii-chat binary
 *
 * This function implements the mode dispatcher pattern, routing execution
 * to the appropriate mode-specific entry point based on command line arguments.
 *
 * **Dispatcher Logic Flow:**
 * 1. Parse first argument to determine mode or global option
 * 2. Handle global options (--help, --version)
 * 3. Validate mode name and find mode descriptor
 * 4. Prepare argc/argv with mode name removed
 * 5. Call mode-specific entry point
 * 6. Return mode's exit code to operating system
 *
 * **Argument Forwarding:**
 * The mode name is consumed by the dispatcher and NOT passed to mode entry points.
 * Example transformation:
 * ```
 * Command:  ascii-chat server --port 8080
 * Dispatch: server_main(argc=2, argv=["ascii-chat", "--port", "8080"])
 * ```
 *
 * This design allows mode entry points to parse their own options without
 * special handling for the mode name argument.
 *
 * @param argc Original argument count from OS
 * @param argv Original argument vector from OS
 * @return Exit code (0 = success, non-zero = error)
 */
int main(int argc, char *argv[]) {
  /**
   * @name Argument Validation
   * @{
   */

  // Validate basic argument structure
  if (argc < 1 || argv == NULL || argv[0] == NULL) {
    fprintf(stderr, "Error: Invalid argument vector\n");
    return 1;
  }

  // Store program name for error messages
  const char *program_name = argv[0];

  /** @} */

  /**
   * @name Case 1: No Arguments - Show Usage
   * @{
   */

  // Case 1: No arguments provided - show usage
  if (argc == 1) {
    print_usage(program_name);
    return 0;
  }

  /** @} */

  /**
   * @name Case 2: Find Mode (First Non-Option Argument)
   * @{
   */

  // Case 2: Find the mode (first non-option argument)
  int mode_index = -1;
  const mode_descriptor_t *mode = NULL;

  for (int i = 1; i < argc; i++) {
    // Skip options (arguments starting with -)
    if (argv[i][0] == '-') {
      continue;
    }

    // This is a non-option argument - check if it's a valid mode
    mode = find_mode(argv[i]);
    if (mode != NULL) {
      mode_index = i;
      break;
    }
  }

  /** @} */

  /**
   * @name Case 3: Handle Global Options (Before Mode)
   * @{
   */

  // Case 3: Check for --help or --version BEFORE the mode (global options)
  // If they appear after the mode, they'll be passed to the mode handler
  // Any other option before the mode is an error
  for (int i = 1; i < argc && (mode_index == -1 || i < mode_index); i++) {
    if (argv[i][0] == '-') {
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
        print_usage(program_name);
        return 0;
      }
      if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
        print_version();
        return 0;
      }
      // Any other option before the mode is not supported
      fprintf(stderr, "Error: Option '%s' must come after the mode\n\n", argv[i]);
      print_usage(program_name);
      return ERROR_USAGE;
    }
  }

  /** @} */

  /**
   * @name Case 4: Validate Mode Found
   * @{
   */

  // Case 4: Validate mode was found
  if (mode == NULL) {
    // Check if there's a non-option non-mode argument (invalid mode)
    for (int i = 1; i < argc; i++) {
      if (argv[i][0] != '-') {
        // Found a non-option that's not a valid mode
        fprintf(stderr, "Error: Unknown mode '%s'\n\n", argv[i]);
        print_usage(program_name);
        return ERROR_USAGE;
      }
    }

    // Only options provided, no mode
    fprintf(stderr, "Error: No mode specified\n\n");
    print_usage(program_name);
    return ERROR_USAGE;
  }

  /** @} */

  /**
   * @name Case 5: Build argv for Mode Entry Point
   * @{
   */

  // Case 5: Build argv for mode entry point
  // Include only arguments AFTER the mode
  // Example: ["ascii-chat", "--color", "server", "--port", "8080"]
  //       → ["ascii-chat", "--port", "8080"]
  //       mode_argc = 1 (program_name) + 2 (--port, 8080) = 3
  int args_after_mode = argc - mode_index - 1; // Number of args after mode
  int mode_argc = 1 + args_after_mode;         // program_name + args after mode
  char **mode_argv = SAFE_MALLOC((size_t)(mode_argc + 1) * sizeof(char *), char **);

  // Build new argv: [program_name, options_after_mode]
  mode_argv[0] = argv[0]; // Keep program name
  int out_idx = 1;

  // Copy only arguments that come AFTER the mode
  for (int i = mode_index + 1; i < argc; i++) {
    mode_argv[out_idx++] = argv[i];
  }
  mode_argv[mode_argc] = NULL; // NULL-terminate for safety

  /** @} */

  /**
   * @name Case 6: Dispatch to Mode Entry Point
   * @{
   */

  // Dispatch to mode entry point
  int exit_code = mode->entry_point(mode_argc, mode_argv);
  if (exit_code == ERROR_USAGE) {
    _exit(ERROR_USAGE);
  }

  /** @} */

  /**
   * @name Cleanup and Return
   * @{
   */

  // Cleanup and return
  SAFE_FREE(mode_argv);
  return exit_code;

  /** @} */
}
