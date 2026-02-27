/**
 * @file mirror/main.c
 * @ingroup mirror
 * @brief Local media mirror mode: view webcam or media files as ASCII art without network
 *
 * Mirror mode is a standalone, self-contained ASCII video viewer that requires no
 * server or network connection. It's ideal for:
 * - Testing webcam capture and ASCII conversion pipelines locally
 * - Viewing video files as ASCII art
 * - Previewing terminal rendering without networking complexity
 * - Debugging frame processing, color palettes, and keyboard input handling
 *
 * ## Architecture Overview
 *
 * Mirror mode uses the unified session library to manage initialization and rendering:
 *
 * ```
 * mirror_main()
 *     ↓
 * session_client_like_run()  [Shared initialization]
 *     ├─ Terminal setup (TTY detection, control handler)
 *     ├─ Media source creation (webcam/file/stdin/test pattern)
 *     ├─ FPS probing (automatically detect frame rate)
 *     ├─ Audio initialization (optional, from media or microphone)
 *     ├─ Display context creation (palette, color mode, terminal capabilities)
 *     ├─ Splash screen animation
 *     └─ Mirror mode main loop
 *         ↓
 *     mirror_run()
 *         ↓
 *     session_render_loop()  [Unified render loop]
 *         ├─ Frame capture via session_capture_read_frame()
 *         ├─ Frame rate limiting via session_capture_sleep_for_fps()
 *         ├─ ASCII conversion via session_display_convert_to_ascii()
 *         ├─ Terminal rendering via session_display_render_frame()
 *         ├─ Keyboard input polling (if handler provided)
 *         └─ Loop until user Ctrl+C or media EOF
 * ```
 *
 * ## Frame Processing Pipeline
 *
 * Each frame flows through a multi-stage pipeline:
 *
 * ### 1. Media Source & Capture
 *
 * The media source abstraction handles all input types transparently:
 * - **Webcam**: Continuous live capture at target FPS
 * - **Video Files**: Decoded frame-by-frame via FFmpeg
 * - **Streaming URLs**: HTTP/HTTPS/RTSP streams resolved via FFmpeg or yt-dlp
 * - **Stdin**: Custom frame input for testing
 * - **Test Pattern**: Synthetic frames for debugging
 *
 * Configuration:
 * ```c
 * session_capture_config_t {
 *   .type = MEDIA_SOURCE_WEBCAM,  // or FILE, STDIN, TEST
 *   .path = "0",                   // device index or file path
 *   .target_fps = 60,              // desired frame rate
 *   .loop = false,                 // repeat on EOF
 *   .resize_for_network = false    // local rendering, no resize
 * }
 * ```
 *
 * Captured frame: Raw RGB image from media source.
 *
 * ### 2. ASCII Conversion
 *
 * `session_display_convert_to_ascii()` transforms raw video to ASCII art:
 * - **Palette Selection**: Choose from standard, compact, or custom ASCII characters
 * - **Luminance Mapping**: Convert RGB to brightness, map to character palette
 * - **Color Rendering**: Terminal color codes (256-color or true color if supported)
 * - **Compression**: Optional RLE compression for efficient transmission
 *
 * Output: ASCII frame with embedded ANSI color codes.
 *
 * ### 3. Terminal Rendering
 *
 * `session_display_render_frame()` writes ASCII to terminal:
 * - **Terminal Detection**: Detect color capability (256-color, true color, monochrome)
 * - **Cursor Management**: Minimize flicker with intelligent cursor positioning
 * - **Atomic Writes**: Serialized output prevents visual artifacts
 *
 * Terminal output: Colored ASCII art displayed in real-time.
 *
 * ### 4. Frame Rate Control
 *
 * Adaptive sleep maintains target FPS:
 * - Track capture timestamps
 * - Calculate ideal sleep duration
 * - Yield CPU without blocking (platform-aware sleep)
 *
 * Result: Smooth, responsive playback at target frame rate.
 *
 * ## Features
 *
 * - Local webcam capture and ASCII conversion (no network)
 * - Media file playback (video/audio files, animated GIFs, URLs)
 * - Smart media resolution (FFmpeg + yt-dlp for 1000+ streaming sites)
 * - Loop playback for media files
 * - Terminal capability detection for optimal color output (256-color, true color)
 * - Adaptive frame rate limiting for smooth display
 * - Audio playback from media files or microphone
 * - Keyboard input support for interactive controls
 * - Graceful shutdown on Ctrl+C (proper cleanup, no orphaned processes)
 * - Snapshot mode for single-frame capture
 * - Custom palette support
 *
 * ## Usage Examples
 *
 * @code
 * // Local webcam (live view)
 * ascii-chat mirror
 *
 * // Video file (with loop)
 * ascii-chat mirror --file video.mp4 --loop
 *
 * // Streaming URL (YouTube, Twitch, etc. - requires yt-dlp)
 * ascii-chat mirror --url "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
 *
 * // Snapshot mode (single frame, then exit)
 * ascii-chat mirror --snapshot --snapshot-delay 0
 *
 * // Custom FPS and palette
 * ascii-chat mirror --file video.mp4 --fps 30 --palette compact
 *
 * // With audio from file
 * ascii-chat mirror --file video.mp4 --enable-audio
 * @endcode
 *
 * ## Integration with Session Library
 *
 * Mirror mode delegates initialization and lifecycle management to `session_client_like_run()`:
 *
 * - **Initialization**: Media source creation, FPS probing, display setup
 * - **Splash Screen**: Animated intro with embedded log capture
 * - **Render Loop**: Delegates to `session_render_loop()` for frame processing
 * - **Cleanup**: Proper teardown of audio, display, and media resources
 * - **Keyboard Handler**: Optional interactive controls via mirror_keyboard_handler()
 *
 * ## Keyboard Controls
 *
 * If keyboard support is enabled:
 * - `q` or `Ctrl+C`: Quit
 * - `h`: Toggle help screen
 * - `p`: Pause/resume playback (media files only)
 * - `n`: Next frame (when paused)
 * - `Space`: Pause/resume
 *
 * (Actual controls implemented in session_handle_keyboard_input())
 *
 * ## Performance Characteristics
 *
 * - **Webcam**: Minimal latency (single-frame buffer)
 * - **Video Files**: Streaming decode (no buffering overhead)
 * - **Large Files**: Constant memory usage (frame-by-frame processing)
 * - **Terminal Output**: Optimized for minimal flicker (differential rendering)
 * - **CPU Usage**: Scales with target FPS and color mode
 *
 * ## Terminal Requirements
 *
 * - TTY output (not suitable for piped output - detection and auto-fallback implemented)
 * - ANSI escape sequence support (16, 256, or true color)
 * - Minimum 60x20 terminal size (uses full available space)
 *
 * ## Example Workflow
 *
 * @code
 * // User runs:
 * $ ascii-chat mirror --file movie.mp4
 *
 * // Internal flow:
 * 1. mirror_main() called from binary main()
 * 2. session_client_like_run() initializes:
 *    - Opens FFmpeg decoder for movie.mp4
 *    - Probes FPS (e.g., 24 FPS)
 *    - Creates display context with 256-color palette
 *    - Shows splash screen with animation
 * 3. mirror_run() invokes session_render_loop()
 * 4. Each iteration:
 *    - Reads frame from FFmpeg decoder
 *    - Sleeps to maintain 24 FPS
 *    - Converts to ASCII (luminance mapping + color codes)
 *    - Renders to terminal with cursor optimization
 *    - Polls for keyboard input (q to quit)
 * 5. On Ctrl+C or EOF:
 *    - Clean exit from render loop
 *    - Destroy display, audio, media source
 *    - Print newline to separate from shell prompt
 *
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 *
 * @see session/client_like.h - Shared initialization
 * @see session/render.h - Unified render loop
 * @see session/capture.h - Media source abstraction
 * @see session/display.h - Terminal rendering
 * @see lib/media/source.h - Media source types (webcam, file, etc.)
 * @see lib/video/palette.h - ASCII palette types
 */

#include "main.h"
#include "session/client_like.h"
#include "session/capture.h"
#include "session/display.h"
#include "session/render.h"
#include "session/keyboard_handler.h"
#include <ascii-chat/log/logging.h>
#include <unistd.h>

/* ============================================================================
 * Mode-Specific Keyboard Handler
 * ============================================================================ */

/**
 * Mirror mode keyboard handler callback
 *
 * @param capture Capture context for media source control
 * @param key Keyboard key code
 * @param user_data Display context for help screen toggle (borrowed reference)
 */
static void mirror_keyboard_handler(session_capture_ctx_t *capture, int key, void *user_data) {
  session_display_ctx_t *display = (session_display_ctx_t *)user_data;
  session_handle_keyboard_input(capture, display, (keyboard_key_t)key);
}

/* ============================================================================
 * Mode-Specific Main Loop
 * ============================================================================ */

/**
 * Mirror mode run callback
 *
 * Called after all shared initialization is complete (capture opened, audio
 * started, display ready, splash done). Runs the unified render loop.
 *
 * @param capture   Initialized capture context
 * @param display   Initialized display context
 * @param user_data Unused (NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t mirror_run(session_capture_ctx_t *capture, session_display_ctx_t *display, void *user_data) {
  (void)user_data; // Unused

  log_info("mirror_run: CALLED - capture=%p display=%p", (void *)capture, (void *)display);

  // Get the render loop's should_exit callback from session_client_like
  bool (*render_should_exit)(void *) = session_client_like_get_render_should_exit();
  if (!render_should_exit) {
    log_error("mirror_run: render_should_exit callback not initialized");
    return SET_ERRNO(ERROR_INVALID_STATE, "Render should_exit callback not initialized");
  }

  log_info("mirror_run: Calling session_render_loop");
  // Run the unified render loop with keyboard support
  asciichat_error_t result = session_render_loop(capture, display,
                                                 render_should_exit,      // Exit check (checks global + custom)
                                                 NULL,                    // No custom capture callback
                                                 NULL,                    // No custom sleep callback
                                                 mirror_keyboard_handler, // Keyboard handler
                                                 display);                // user_data for keyboard handler
  log_info("mirror_run: session_render_loop returned with result=%d", result);
  return result;
}

/* ============================================================================
 * Deadlock Test Code
 * ============================================================================ */

static mutex_t g_test_mutex_a, g_test_mutex_b;

static void *test_thread_a(void *arg) {
  (void)arg;
  log_info("TEST DEADLOCK: Thread A acquiring mutex_a");
  mutex_lock(&g_test_mutex_a);
  log_info("TEST DEADLOCK: Thread A got mutex_a, sleeping 1s");
  sleep(1);
  log_info("TEST DEADLOCK: Thread A trying to acquire mutex_b (will deadlock)");
  mutex_lock(&g_test_mutex_b); // Will block forever
  log_info("TEST DEADLOCK: Thread A got both (should never reach)");
  mutex_unlock(&g_test_mutex_b);
  mutex_unlock(&g_test_mutex_a);
  return NULL;
}

static void *test_thread_b(void *arg) {
  (void)arg;
  usleep(500000); // Let thread A go first (500ms)
  log_info("TEST DEADLOCK: Thread B acquiring mutex_b");
  mutex_lock(&g_test_mutex_b);
  log_info("TEST DEADLOCK: Thread B got mutex_b, sleeping 500ms");
  usleep(500000);
  log_info("TEST DEADLOCK: Thread B trying to acquire mutex_a (will deadlock)");
  mutex_lock(&g_test_mutex_a); // Will block forever
  log_info("TEST DEADLOCK: Thread B got both (should never reach)");
  mutex_unlock(&g_test_mutex_a);
  mutex_unlock(&g_test_mutex_b);
  return NULL;
}

/* ============================================================================
 * Mirror Mode Entry Point
 * ============================================================================ */

/**
 * @brief Run mirror mode main loop
 *
 * Initializes webcam and terminal, then continuously captures frames,
 * converts them to ASCII art, and displays them locally.
 *
 * Uses the session library for unified capture and display management.
 *
 * @return 0 on success, non-zero error code on failure
 */
int mirror_main(void) {
  // Initialize test mutexes for deadlock demonstration
  mutex_init(&g_test_mutex_a, "test_mutex_a");
  mutex_init(&g_test_mutex_b, "test_mutex_b");

  // Create deadlock threads
  asciichat_thread_t tid_a, tid_b;
  log_info("Creating deadlock test threads...");
  asciichat_thread_create(&tid_a, "test_deadlock_a", test_thread_a, NULL);
  asciichat_thread_create(&tid_b, "test_deadlock_b", test_thread_b, NULL);

  log_info("===== DEADLOCK WILL FORM IN ~2 SECONDS =====");
  log_info("In another terminal, run: kill -SIGUSR1 %d", getpid());
  log_info("======================================================");

  // Configure mode-specific session settings
  session_client_like_config_t config = {
      .run_fn = mirror_run,
      .run_user_data = NULL,
      .keyboard_handler = mirror_keyboard_handler,
      .print_newline_on_tty_exit = true, // Mirror prints newline to separate frame from prompt
  };

  log_info("mirror_main: Starting session_client_like_run");
  // Run shared initialization/teardown with mode-specific loop
  asciichat_error_t result = session_client_like_run(&config);
  log_info("mirror_main: session_client_like_run returned with result=%d", result);

  if (result != ASCIICHAT_OK) {
    log_error("Mirror mode failed with error code: %d", result);
  }

  return (int)result;
}
