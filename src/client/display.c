/**
 * @file client/display.c
 * @ingroup client_display
 * @brief 💻 Client terminal display: TTY detection, frame rendering, and interactive/stdout output routing
 *
 * The display system supports dual output modes:
 * - **Interactive TTY Mode**: Direct terminal control with cursor positioning
 * - **Redirect Mode**: Plain text output for pipes and file redirection
 * - **Snapshot Mode**: Single frame capture with minimal terminal control
 *
 * ## TTY Detection and Management
 *
 * Implements robust TTY detection across platforms:
 * 1. **Environment Variables**: Check $TTY for explicit terminal path
 * 2. **Standard Streams**: Test stdin/stdout/stderr for TTY status
 * 3. **Controlling Terminal**: Fall back to /dev/tty (Unix) or CON (Windows)
 * 4. **Validation**: Verify TTY path accessibility and permissions
 *
 * ## Terminal Control Sequences
 *
 * Uses platform abstraction layer for terminal operations:
 * - **Initialization**: Set terminal to optimal display mode
 * - **Cursor Management**: Hide cursor and position for frame updates
 * - **Screen Control**: Clear screen and scrollback buffer
 * - **Reset Operations**: Restore terminal to original state
 *
 * ## Frame Rendering Pipeline
 *
 * Frame display follows a structured pipeline:
 * 1. **Mode Detection**: Determine output mode (TTY vs redirect)
 * 2. **Cursor Positioning**: Position cursor for frame update (TTY mode)
 * 3. **Data Writing**: Write frame data to appropriate file descriptor
 * 4. **Synchronization**: Ensure data reaches terminal (fsync for redirect)
 * 5. **State Updates**: Track frame dimensions and display state
 *
 * ## Snapshot Mode Support
 *
 * Special handling for single-frame capture:
 * - **Timing Control**: Coordinate with protocol for snapshot timing
 * - **Output Routing**: Final frame written to both TTY and stdout
 * - **Format Control**: Skip terminal control sequences in snapshot output
 * - **Cleanup**: Add newline terminator for proper file formatting
 *
 * ## Platform Compatibility
 *
 * Cross-platform terminal support:
 * - **Unix/Linux**: Uses /dev/tty and POSIX terminal I/O
 * - **macOS**: Enhanced TTY detection with $TTY environment variable
 * - **Windows**: CON device for console output with Windows Console API
 * - **Error Handling**: Graceful fallback for unsupported operations
 *
 * ## Integration Points
 *
 * - **main.c**: Display subsystem initialization and lifecycle management
 * - **protocol.c**: Frame data reception and rendering requests
 * - **server.c**: Terminal capability reporting and resize handling
 * - **options.c**: Display mode configuration from command line
 *
 * ## Error Handling
 *
 * Display errors handled with graceful degradation:
 * - **TTY Access Errors**: Fall back to stdout redirection mode
 * - **Write Errors**: Log errors but continue processing
 * - **Terminal Control Errors**: Skip control sequences, continue with data
 * - **Permission Errors**: Try alternative TTY paths before failing
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0
 */

#include "display.h"
#include "main.h"

#include "platform/abstraction.h"
#include "platform/internal.h"
#include "options.h"
#include "image2ascii/ascii.h"
#include "logging.h"

#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

/* ============================================================================
 * TTY Management
 * ============================================================================ */

/** Use tty_info_t from platform abstraction */

/**
 * @brief Global TTY information structure
 *
 * Maintains information about the terminal/TTY device for interactive output.
 * Contains file descriptor, device path, and validity status. Initialized
 * during display subsystem startup.
 *
 * @note Not static because it may be accessed from other modules
 * @ingroup client_display
 */
tty_info_t g_tty_info = {-1, NULL, false};

/**
 * @brief Flag indicating if we have a valid TTY for interactive output
 *
 * Set to true if the display has detected a valid terminal/TTY for rendering.
 * When false, output may be redirected or non-interactive (pipes, files, etc.).
 * Used to determine optimal rendering strategy.
 *
 * @ingroup client_display
 */
static bool g_has_tty = false;

/**
 * @brief Atomic flag indicating if this is the first frame of the current connection
 *
 * Set to true at the start of each new connection, cleared after the first
 * frame is rendered. Used to disable logging during the first frame render
 * to prevent console corruption, then enable logging for subsequent frames.
 *
 * @ingroup client_display
 */
static atomic_bool g_is_first_frame_of_connection = true;

/* ============================================================================
 * TTY Detection and Initialization
 * ============================================================================ */

/**
 * @brief Detect and configure current TTY
 *
 * Implements multi-method TTY detection with fallback strategy:
 * 1. Check $TTY environment variable (most specific on macOS)
 * 2. Test standard file descriptors for TTY status
 * 3. Fall back to controlling terminal device
 * 4. Return invalid structure if no TTY available
 *
 * TTY Detection Priority:
 * - Environment TTY path has highest priority (explicit user preference)
 * - Standard streams checked in order: stdin, stdout, stderr
 * - Controlling terminal (/dev/tty or CON) as final fallback
 * - Windows uses CON device for console access
 *
 * @return TTY information structure with file descriptor and path
 *
 * @ingroup client_display
 */
static tty_info_t display_get_current_tty(void) {
  // Use the platform abstraction layer function
  return get_current_tty();
}

/**
 * @brief Perform complete terminal reset
 *
 * Executes comprehensive terminal reset sequence for clean display state.
 * Skips terminal control operations in snapshot mode to avoid interfering
 * with clean ASCII output capture.
 *
 * Reset Operations:
 * 1. Terminal attribute reset to default state
 * 2. Screen clearing with console_clear() wrapper
 * 3. Scrollback buffer clearing for clean history
 * 4. Cursor hiding for uninterrupted frame display
 * 5. Terminal buffer flushing to ensure immediate effect
 *
 * @param fd File descriptor for terminal operations
 *
 * @ingroup client_display
 */
static void full_terminal_reset(int fd) {
  // Skip terminal control sequences in snapshot mode - just print raw ASCII
  if (!opt_snapshot_mode) {
    terminal_reset(fd);             // Reset using the proper TTY fd
    console_clear(fd);              // This calls terminal_clear_screen() + terminal_cursor_home(fd)
    terminal_clear_scrollback(fd);  // Clear scrollback using the proper TTY fd
    terminal_hide_cursor(fd, true); // Hide cursor on the specific TTY
    terminal_flush(fd);             // Flush the specific TTY fd
  }
}

/* ============================================================================
 * Frame Rendering Functions
 * ============================================================================ */

/**
 * @brief Write frame data to appropriate output destination
 *
 * Routes frame output based on TTY availability and snapshot mode requirements.
 * Handles cursor positioning and output synchronization for different modes.
 *
 * Output Routing Logic:
 * - **TTY Mode**: Direct terminal writing with cursor control
 * - **Redirect Mode**: stdout writing with optional cursor control
 * - **Snapshot Mode**: Raw ASCII output without cursor positioning
 * - **Error Handling**: Graceful fallback if TTY operations fail
 *
 * @param frame_data ASCII frame data to display
 * @param use_direct_tty Whether to use direct TTY output or stdout
 *
 * @ingroup client_display
 */
static void write_frame_to_output(const char *frame_data, bool use_direct_tty) {
  // Safety check for NULL or empty data
  if (!frame_data) {
    log_error("write_frame_to_output: NULL frame_data");
    return;
  }

  // Calculate length safely to avoid potential segfault in strlen
  size_t frame_len = strnlen(frame_data, 1024 * 1024); // Max 1MB frame
  if (frame_len == 0) {
    log_debug("write_frame_to_output: Empty frame data");
    return;
  }

  if (use_direct_tty) {
    // Direct TTY for interactive use
    if (g_tty_info.fd >= 0) {
      // Always position cursor for TTY output (even in snapshot mode)
      cursor_reset(g_tty_info.fd);
      platform_write(g_tty_info.fd, frame_data, frame_len);
      // Flush terminal buffer to ensure immediate visibility
      terminal_flush(g_tty_info.fd);
    } else {
      log_error("Failed to open TTY: %s", g_tty_info.path ? g_tty_info.path : "unknown");
    }
  } else {
    // stdout for pipes/redirection/testing
    // Skip cursor reset in snapshot mode - just print raw ASCII
    if (!opt_snapshot_mode) {
      cursor_reset(STDOUT_FILENO);
    }
    platform_write(STDOUT_FILENO, frame_data, frame_len);
    // Flush stdout to ensure immediate visibility
    (void)fflush(stdout);
    // Only fsync if we have a valid file descriptor and not on Windows console
    if (!platform_isatty(STDOUT_FILENO)) {
      platform_fsync(STDOUT_FILENO);
    }
  }
}

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

/**
 * @brief Initialize what is necessary to display ascii frames
 *
 * Performs TTY detection, terminal configuration, and ASCII rendering
 * initialization. Must be called once during client startup.
 *
 * Initialization Steps:
 * 1. TTY detection and configuration
 * 2. Interactive mode determination
 * 3. ASCII rendering subsystem initialization
 * 4. Terminal dimension and capability setup
 * 5. Take ownership of terminal from logging system
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_display
 */
int display_init() {
  // Get TTY info for direct terminal access (if needed for interactive mode)
  g_tty_info = display_get_current_tty();

  // Determine if we should use interactive TTY output
  // For output routing: only consider stdout status (not controlling terminal)
  // Additional TTY validation: if stdout is redirected but we have a controlling TTY,
  // we can still show interactive output on the terminal
  if (g_tty_info.fd >= 0) {
    g_has_tty = platform_isatty(g_tty_info.fd) != 0; // We have a valid controlling terminal
  }

  // Initialize ASCII output for this connection
  ascii_write_init(g_tty_info.fd, false);

  // Take ownership of terminal from logging system.
  // While display owns the terminal, log_*() calls will block.
  // This prevents log messages from interleaving with video frames.
  log_terminal_take_ownership();

  return 0;
}

/**
 * @brief Check if display has TTY capability
 *
 * @return true if TTY is available for interactive output, false otherwise
 *
 * @ingroup client_display
 */
bool display_has_tty() {
  return g_has_tty;
}

/**
 * @brief Perform full display reset
 *
 * Executes complete terminal reset sequence for clean display state.
 * Safe to call multiple times and handles mode-specific behavior.
 *
 * @ingroup client_display
 */
void display_full_reset() {
  if (g_tty_info.fd >= 0 && !should_exit()) {
    full_terminal_reset(g_tty_info.fd);
  }
}

/**
 * @brief Reset display state for new connection
 *
 * Resets the first frame tracking flag to prepare for a new connection.
 * Call this when starting a new connection to reset first frame tracking.
 *
 * @ingroup client_display
 */
void display_reset_for_new_connection() {
  atomic_store(&g_is_first_frame_of_connection, true);
}

/**
 * @brief Disable terminal logging for first frame
 *
 * Disables terminal logging before clearing the display for the first frame
 * to prevent log output from interfering with ASCII display.
 *
 * @ingroup client_display
 */
void display_disable_logging_for_first_frame() {
  // Disable terminal logging before clearing display and rendering first frame
  if (atomic_load(&g_is_first_frame_of_connection)) {
    log_set_terminal_output(false);
    atomic_store(&g_is_first_frame_of_connection, false);
  }
}

/**
 * Render ASCII frame to display
 *
 * Handles frame rendering with appropriate output routing based on display
 * mode and snapshot requirements. Manages TTY vs redirect output and
 * snapshot timing coordination.
 *
 * Rendering Logic:
 * - **Interactive Mode**: Render every frame to TTY
 * - **Redirect Mode**: Only render final snapshot frame
 * - **Snapshot Mode**: Render to both TTY and stdout for final frame
 * - **Format Control**: Add newline terminator for snapshot files
 *
 * @param frame_data ASCII frame data to render
 * @param is_snapshot_frame Whether this is the final snapshot frame
 *
 * @ingroup client_display
 */
void display_render_frame(const char *frame_data, bool is_snapshot_frame) {
  if (!frame_data) {
    log_warn("Attempted to render NULL frame data");
    return;
  }

  // For terminal: print every frame until final snapshot
  // For non-terminal: only print the final snapshot frame
  if (g_has_tty || (!g_has_tty && opt_snapshot_mode && is_snapshot_frame)) {
    if (is_snapshot_frame) {
      // Write the final frame to the terminal as well, not just to stdout
      write_frame_to_output(frame_data, true);
    }

    // The real ASCII data frame write call
    write_frame_to_output(frame_data, g_has_tty && !is_snapshot_frame);

    if (opt_snapshot_mode && is_snapshot_frame) {
      // A newline at the end of the snapshot of ASCII art to end the file
      printf("\n");
    }
  }
}

/**
 * @brief Cleanup display subsystem
 *
 * Performs graceful cleanup of display resources and terminal state.
 * Restores terminal to original state and closes owned file descriptors.
 * Releases terminal ownership back to logging system so pending logs proceed.
 *
 * @ingroup client_display
 */
void display_cleanup() {
  // Re-enable terminal logging before releasing ownership.
  // This causes any pending log_*() calls to start blocking on the mutex
  // (previously they were returning early because terminal output was disabled).
  // Then when we release ownership, those blocked calls will proceed.
  log_set_terminal_output(true);

  // Release terminal ownership back to logging system.
  // Any blocked log_*() calls will now proceed.
  log_terminal_release_ownership();

  // Cleanup ASCII rendering
  ascii_write_destroy(g_tty_info.fd, true);

  // Close the controlling terminal if we opened it
  if (g_tty_info.owns_fd && g_tty_info.fd >= 0) {
    platform_close(g_tty_info.fd);
    g_tty_info.fd = -1;
    g_tty_info.owns_fd = false;
  }

  g_has_tty = false;
}
