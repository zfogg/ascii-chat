/**
 * @file mirror/main.c
 * @ingroup mirror
 * @brief Local webcam mirror mode: view webcam as ASCII art without network
 *
 * Mirror mode provides a simple way to view your own webcam feed converted
 * to ASCII art directly in the terminal. No server connection is required.
 *
 * ## Features
 *
 * - Local webcam capture and ASCII conversion
 * - Terminal capability detection for optimal color output
 * - Frame rate limiting for smooth display
 * - Clean shutdown on Ctrl+C
 *
 * ## Usage
 *
 * Run as a standalone mode:
 * @code
 * ascii-chat mirror
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#include "main.h"
#include "video/webcam/webcam.h"
#include "video/ascii.h"
#include "video/image.h"
#include "video/ansi_fast.h"
#include "video/ansi.h"
#include "video/rle.h"

#include "platform/abstraction.h"
#include "platform/terminal.h"
#include "common.h"
#include "options/options.h"
#include "options/rcu.h" // For RCU-based options access
#include "video/palette.h"
#include "util/time.h"

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ============================================================================
 * Global State Variables
 * ============================================================================ */

/** Global flag indicating shutdown has been requested */
static atomic_bool g_mirror_should_exit = false;

/**
 * Check if shutdown has been requested
 *
 * @return true if shutdown requested, false otherwise
 */
static bool mirror_should_exit(void) {
  return atomic_load(&g_mirror_should_exit);
}

/**
 * Signal that shutdown should be requested
 */
static void mirror_signal_exit(void) {
  atomic_store(&g_mirror_should_exit, true);
}

/**
 * Console control handler for Ctrl+C and related events
 *
 * @param event The control event that occurred
 * @return true if the event was handled
 */
static bool mirror_console_ctrl_handler(console_ctrl_event_t event) {
  if (event != CONSOLE_CTRL_C && event != CONSOLE_CTRL_BREAK) {
    return false;
  }

  // Use atomic instead of volatile for signal handler
  static _Atomic int ctrl_c_count = 0;
  int count = atomic_fetch_add(&ctrl_c_count, 1) + 1;

  if (count > 1) {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 1);
#else
    _exit(1);
#endif
  }

  mirror_signal_exit();
  return true;
}

/* ============================================================================
 * Mirror Mode TTY Management
 * ============================================================================ */

/** TTY info for mirror mode */
static tty_info_t g_mirror_tty_info = {-1, NULL, false};

/** Flag indicating if we have a valid TTY */
static bool g_mirror_has_tty = false;

/**
 * Initialize mirror mode display
 *
 * @return 0 on success, negative on error
 */
static int mirror_display_init(void) {
  g_mirror_tty_info = get_current_tty();

  // Only use TTY output if stdout is also a TTY (respects shell redirection)
  // This ensures `cmd > file` works by detecting stdout redirection
  bool stdout_is_tty = platform_isatty(STDOUT_FILENO) != 0;
  if (g_mirror_tty_info.fd >= 0 && stdout_is_tty) {
    g_mirror_has_tty = platform_isatty(g_mirror_tty_info.fd) != 0;
  } else {
    g_mirror_has_tty = false;
  }

  // Initialize ASCII output
  ascii_write_init(g_mirror_tty_info.fd, !(GET_OPTION(snapshot_mode)));

  return 0;
}

/**
 * Cleanup mirror mode display
 */
static void mirror_display_cleanup(void) {
  ascii_write_destroy(g_mirror_tty_info.fd, true);

  if (g_mirror_tty_info.owns_fd && g_mirror_tty_info.fd >= 0) {
    platform_close(g_mirror_tty_info.fd);
    g_mirror_tty_info.fd = -1;
    g_mirror_tty_info.owns_fd = false;
  }

  g_mirror_has_tty = false;
}

/**
 * Write frame to terminal output
 *
 * @param frame_data ASCII frame data to display
 */
static void mirror_write_frame(const char *frame_data) {
  if (!frame_data) {
    return;
  }

  size_t frame_len = strnlen(frame_data, 1024 * 1024);
  if (frame_len == 0) {
    return;
  }

  if (g_mirror_has_tty && g_mirror_tty_info.fd >= 0) {
    cursor_reset(g_mirror_tty_info.fd);
    platform_write(g_mirror_tty_info.fd, frame_data, frame_len);
    terminal_flush(g_mirror_tty_info.fd);
  } else {
    // Expand RLE for pipe/file output where terminals can't interpret REP sequences
    char *expanded = ansi_expand_rle(frame_data, frame_len);
    const char *output_data = expanded ? expanded : frame_data;
    size_t output_len = expanded ? strlen(expanded) : frame_len;

    // Strip all ANSI escape sequences if --strip-ansi is set
    char *stripped = NULL;
    if (GET_OPTION(strip_ansi)) {
      stripped = ansi_strip_escapes(output_data, output_len);
      if (stripped) {
        output_data = stripped;
        output_len = strlen(stripped);
      }
    }

    if (!GET_OPTION(snapshot_mode)) {
      cursor_reset(STDOUT_FILENO);
    }
    platform_write(STDOUT_FILENO, output_data, output_len);
    platform_write(STDOUT_FILENO, "\n", 1); // Trailing newline after frame
    (void)fflush(stdout);
    SAFE_FREE(stripped);
    SAFE_FREE(expanded);
  }
}

/* ============================================================================
 * Mirror Mode Main Loop
 * ============================================================================ */

/**
 * @brief Run mirror mode main loop
 *
 * Initializes webcam and terminal, then continuously captures frames,
 * converts them to ASCII art, and displays them locally.
 *
 * @return 0 on success, non-zero error code on failure
 */
int mirror_main(void) {
  log_info("Starting mirror mode");

  // Install console control-c handler
  platform_set_console_ctrl_handler(mirror_console_ctrl_handler);

#ifndef _WIN32
  platform_signal(SIGPIPE, SIG_IGN);
#endif

  // Initialize webcam
  int webcam_result = webcam_init(GET_OPTION(webcam_index));
  if (webcam_result != 0) {
    log_fatal("Failed to initialize webcam: %s", asciichat_error_string(webcam_result));
    webcam_print_init_error_help(webcam_result);
    return webcam_result;
  }

  // Initialize display
  if (mirror_display_init() != 0) {
    log_fatal("Failed to initialize display");
    webcam_cleanup();
    return ERROR_DISPLAY;
  }

  // Detect terminal capabilities
  terminal_capabilities_t caps = detect_terminal_capabilities();
  caps = apply_color_mode_override(caps);

  // Initialize ANSI color lookup tables based on terminal capabilities
  if (caps.color_level == TERM_COLOR_TRUECOLOR) {
    ansi_fast_init();
  } else if (caps.color_level == TERM_COLOR_256) {
    ansi_fast_init_256color();
  } else if (caps.color_level == TERM_COLOR_16) {
    ansi_fast_init_16color();
  }

  // Initialize palette
  char palette_chars[256] = {0};
  size_t palette_len = 0;
  char luminance_palette[256] = {0};

  const char *custom_chars = GET_OPTION(palette_custom_set) ? GET_OPTION(palette_custom) : NULL;
  palette_type_t palette_type = GET_OPTION(palette_type);
  if (initialize_client_palette(palette_type, custom_chars, palette_chars, &palette_len, luminance_palette) != 0) {
    log_fatal("Failed to initialize palette");
    mirror_display_cleanup();
    webcam_cleanup();
    return ERROR_INVALID_STATE;
  }

  // Adaptive sleep for frame rate limiting at 60 FPS
  adaptive_sleep_state_t sleep_state = {0};
  adaptive_sleep_config_t config = {
      .baseline_sleep_ns = 16666667, // 60 FPS (16.67ms)
      .min_speed_multiplier = 1.0,   // Constant rate (no slowdown)
      .max_speed_multiplier = 1.0,   // Constant rate (no speedup)
      .speedup_rate = 0.0,           // No adaptive behavior (constant FPS)
      .slowdown_rate = 0.0           // No adaptive behavior (constant FPS)
  };
  adaptive_sleep_init(&sleep_state, &config);

  // Snapshot mode timing
  struct timespec snapshot_start_time = {0, 0};
  bool snapshot_done = false;
  if (GET_OPTION(snapshot_mode)) {
    (void)clock_gettime(CLOCK_MONOTONIC, &snapshot_start_time);
  }

  // FPS tracking
  uint64_t frame_count = 0;
  struct timespec fps_report_time;
  (void)clock_gettime(CLOCK_MONOTONIC, &fps_report_time);

  log_info("Mirror mode running - press Ctrl+C to exit");
  log_set_terminal_output(false);

  while (!mirror_should_exit()) {
    // Frame rate limiting using adaptive sleep system
    // Use queue_depth=0 and target_depth=0 for constant 60 FPS display
    adaptive_sleep_do(&sleep_state, 0, 0);

    // Capture timestamp for snapshot mode timing
    struct timespec current_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &current_time);

    // Snapshot mode: check if delay has elapsed (delay 0 = capture first frame immediately)
    if (GET_OPTION(snapshot_mode) && !snapshot_done) {
      double elapsed_sec = (double)(current_time.tv_sec - snapshot_start_time.tv_sec) +
                           (double)(current_time.tv_nsec - snapshot_start_time.tv_nsec) / 1e9;

      float snapshot_delay = GET_OPTION(snapshot_delay);
      if (elapsed_sec >= snapshot_delay) {
        snapshot_done = true;
      }
    }

    // Read frame from webcam
    image_t *image = webcam_read();
    if (!image) {
      platform_sleep_usec(10000); // 10ms delay before retry
      continue;
    }

    // Convert image to ASCII
    // When stretch is 0 (disabled), we preserve aspect ratio (true)
    // When stretch is 1 (enabled), we allow stretching without aspect ratio preservation (false)
    bool stretch = GET_OPTION(stretch);
    unsigned short int width = GET_OPTION(width);
    unsigned short int height = GET_OPTION(height);
    bool preserve_aspect_ratio = !stretch;
    char *ascii_frame = ascii_convert_with_capabilities(image, width, height, &caps, preserve_aspect_ratio, stretch,
                                                        palette_chars, luminance_palette);

    if (ascii_frame) {
      // When piping/redirecting in snapshot mode, only output the final frame
      // When outputting to TTY, show live preview frames
      bool snapshot_mode = GET_OPTION(snapshot_mode);
      bool should_write = !snapshot_mode || g_mirror_has_tty || snapshot_done;
      if (should_write) {
        mirror_write_frame(ascii_frame);
      }

      // Snapshot mode: exit after capturing the final frame
      if (snapshot_mode && snapshot_done) {
        SAFE_FREE(ascii_frame);
        image_destroy(image);
        break;
      }

      SAFE_FREE(ascii_frame);
      frame_count++;
    }

    image_destroy(image);

    // FPS reporting every 5 seconds
    uint64_t fps_elapsed_us = ((uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000) -
                              ((uint64_t)fps_report_time.tv_sec * 1000000 + (uint64_t)fps_report_time.tv_nsec / 1000);

    if (fps_elapsed_us >= 5000000) {
      double fps = (double)frame_count / ((double)fps_elapsed_us / 1000000.0);
      log_debug("Mirror FPS: %.1f", fps);
      frame_count = 0;
      fps_report_time = current_time;
    }
  }

  // Cleanup
  log_set_terminal_output(true);
  log_info("Mirror mode shutting down");

  mirror_display_cleanup();
  webcam_cleanup();

  return 0;
}
