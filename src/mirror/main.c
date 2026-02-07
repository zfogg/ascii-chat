/**
 * @file mirror/main.c
 * @ingroup mirror
 * @brief Local media mirror mode: view webcam or media files as ASCII art without network
 *
 * Mirror mode provides a simple way to view webcam feed or media files converted
 * to ASCII art directly in the terminal. No server connection is required.
 *
 * ## Features
 *
 * - Local webcam capture and ASCII conversion
 * - Media file playback (video/audio files, animated GIFs)
 * - Loop playback for media files
 * - Terminal capability detection for optimal color output
 * - Frame rate limiting for smooth display
 * - Clean shutdown on Ctrl+C
 *
 * ## Usage
 *
 * Run as a standalone mode:
 * @code
 * ascii-chat mirror                        # Use webcam
 * ascii-chat mirror --file video.mp4       # Play video file
 * ascii-chat mirror --file video.mp4 --loop # Loop video file
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#include "main.h"
#include <ascii-chat/session/capture.h>
#include <ascii-chat/session/display.h>
#include <ascii-chat/session/render.h>
#include <ascii-chat/session/keyboard_handler.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/ui/splash.h>

#include <ascii-chat/media/source.h>
#include <ascii-chat/media/youtube.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#include <string.h>
#include <ascii-chat/platform/abstraction.h>

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
 * Unix signal handler for graceful shutdown on SIGTERM
 *
 * @param sig The signal number (unused, but required by signal handler signature)
 */
static void mirror_handle_sigterm(int sig) {
  (void)sig; // Unused
  log_info_nofile("SIGTERM received - shutting down mirror...");
  mirror_signal_exit();
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
    platform_force_exit(1);
  }

  mirror_signal_exit();
  return true;
}

/**
 * Exit condition callback for render loop
 *
 * @param user_data Unused (NULL)
 * @return true if render loop should exit
 */
static bool mirror_render_should_exit(void *user_data) {
  (void)user_data; // Unused parameter
  return mirror_should_exit();
}

/**
 * Mirror mode keyboard handler callback
 *
 * @param capture Capture context for media source control
 * @param key Keyboard key code
 * @param user_data Display context for help screen toggle (borrowed reference)
 */
static void mirror_keyboard_handler(session_capture_ctx_t *capture, int key, void *user_data) {
  // user_data is the display context (session_display_ctx_t *)
  session_display_ctx_t *display = (session_display_ctx_t *)user_data;
  session_handle_keyboard_input(capture, display, (keyboard_key_t)key);
}

/**
 * Adapter function for session capture exit callback
 * Converts from void*->bool signature to match session_capture_should_exit_fn
 *
 * @param user_data Unused (NULL)
 * @return true if capture should exit
 */
static bool mirror_capture_should_exit_adapter(void *user_data) {
  (void)user_data; // Unused parameter
  return mirror_should_exit();
}

/**
 * Adapter function for session display exit callback
 * Converts from void*->bool signature to match session_display_should_exit_fn
 *
 * @param user_data Unused (NULL)
 * @return true if display should exit
 */
static bool mirror_display_should_exit_adapter(void *user_data) {
  (void)user_data; // Unused parameter
  return mirror_should_exit();
}

/* ============================================================================
 * Mirror Mode Display (using session library)
 * ============================================================================ */

/* Display is now managed via session_display_ctx_t from lib/session/display.h */

/* ============================================================================
 * Mirror Mode Main Loop
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
  // Disable terminal logging before any logging happens when stdout is piped.
  // This prevents buffered log output from corrupting the ASCII frame stream.
  if (!platform_isatty(STDOUT_FILENO)) {
    log_set_force_stderr(true);     // Force ALL logs to stderr, not stdout
    log_set_terminal_output(false); // Then disable terminal output entirely
  }

  // Install console control-c handler
  platform_set_console_ctrl_handler(mirror_console_ctrl_handler);

  // Register signal handlers for graceful shutdown and error handling
  platform_signal_handler_t signal_handlers[] = {
      {SIGPIPE, SIG_IGN},               // Ignore broken pipe errors
      {SIGTERM, mirror_handle_sigterm}, // Handle SIGTERM for timeout(1) support
  };
  platform_register_signal_handlers(signal_handlers, 2);

  // Handle keepawake: check for mutual exclusivity and apply mode default
  // Mirror default: keepawake ENABLED (use --no-keepawake to disable)
  if (GET_OPTION(enable_keepawake) && GET_OPTION(disable_keepawake)) {
    FATAL(ERROR_INVALID_PARAM, "--keepawake and --no-keepawake are mutually exclusive");
  }
  if (!GET_OPTION(disable_keepawake)) {
    (void)platform_enable_keepawake();
  }

  log_debug("mirror_main: audio_enabled=%d", GET_OPTION(audio_enabled));

  // Configure media source based on options (read early for splash logic)
  const char *media_url = GET_OPTION(media_url);
  const char *media_file = GET_OPTION(media_file);

  // Detect if we're using media (vs webcam) - needed to decide on splash sleep duration
  bool has_media = (media_url && strlen(media_url) > 0) || (media_file && strlen(media_file) > 0);

  // ============================================================================
  // START SPLASH SCREEN IMMEDIATELY (before any media initialization)
  // ============================================================================
  // Create a minimal display context just for the splash screen
  // This needs to exist before splash_intro_start, but real initialization happens later
  // Pass NULL to auto-detect all settings from command-line options
  session_display_ctx_t *temp_display = session_display_create(NULL);
  if (temp_display) {
    // Display splash immediately so it shows DURING network activity (yt-dlp extraction, etc.)
    splash_intro_start(temp_display);
    // Only sleep for webcam mode - media initialization takes time anyway
    if (!has_media) {
      platform_sleep_ms(1000);
    }
  }

  // Disable terminal logging AFTER splash starts so splash can display cleanly
  // This prevents logs from media initialization from interfering with splash animation
  log_set_terminal_output(false);

  session_capture_config_t capture_config = {0};
  capture_config.target_fps = 60; // Default for webcam
  capture_config.resize_for_network = false;
  capture_config.should_exit_callback = mirror_capture_should_exit_adapter;
  capture_config.callback_data = NULL;

  // Temporary media source for probing (FPS detection, audio detection)
  // Reuse same source to avoid multiple YouTube yt-dlp extractions
  media_source_t *probe_source = NULL;

  if (media_url && strlen(media_url) > 0) {
    // User specified a network URL (takes priority over --file)
    // Don't open webcam when streaming from URL
    log_info("Using network URL: %s (webcam disabled)", media_url);
    capture_config.type = MEDIA_SOURCE_FILE;
    capture_config.path = media_url;

    // Detect FPS for HTTP URLs using probe source (safe now with lazy initialization)
    probe_source = media_source_create(MEDIA_SOURCE_FILE, media_url);
    if (probe_source) {
      double url_fps = media_source_get_video_fps(probe_source);
      log_info("Detected HTTP stream video FPS: %.1f", url_fps);
      if (url_fps > 0.0) {
        capture_config.target_fps = (uint32_t)(url_fps + 0.5);
        log_info("Using target FPS: %u", capture_config.target_fps);
      } else {
        log_warn("FPS detection failed for HTTP stream, using default 30 FPS");
        capture_config.target_fps = 30;
      }
    } else {
      log_warn("Failed to create probe source for HTTP stream, using default 30 FPS");
      capture_config.target_fps = 30;
    }

    capture_config.loop = false; // Network URLs cannot be looped
  } else if (media_file && strlen(media_file) > 0) {
    // User specified a media file or stdin - don't open webcam
    if (strcmp(media_file, "-") == 0) {
      log_info("Using stdin for media streaming (webcam disabled)");
      capture_config.type = MEDIA_SOURCE_STDIN;
      capture_config.path = NULL;
    } else {
      log_info("Using media file: %s (webcam disabled)", media_file);
      capture_config.type = MEDIA_SOURCE_FILE;
      capture_config.path = media_file;

      // Create probe source once and reuse for FPS and audio detection
      probe_source = media_source_create(MEDIA_SOURCE_FILE, media_file);
      if (probe_source) {
        double file_fps = media_source_get_video_fps(probe_source);
        log_info("Detected file video FPS: %.1f", file_fps);
        if (file_fps > 0.0) {
          capture_config.target_fps = (uint32_t)(file_fps + 0.5);
          log_info("Using target FPS: %u", capture_config.target_fps);
        } else {
          log_warn("FPS detection failed, using default 60 FPS");
          capture_config.target_fps = 60;
        }
      } else {
        log_warn("Failed to create probe source for FPS detection");
      }
    }
    capture_config.loop = GET_OPTION(media_loop);
  } else if (GET_OPTION(test_pattern)) {
    // Test pattern mode
    log_info("Using test pattern");
    capture_config.type = MEDIA_SOURCE_TEST;
    capture_config.path = NULL;
    capture_config.loop = false;
  } else {
    // Default to webcam (no --file, --url, or --test-pattern specified)
    log_info("Using local webcam");
    capture_config.type = MEDIA_SOURCE_WEBCAM;
    capture_config.path = NULL;
    capture_config.loop = false;
  }

  // Add seek timestamp if specified
  capture_config.initial_seek_timestamp = GET_OPTION(media_seek_timestamp);

  // Don't reuse probe_source - FPS detection corrupts decoder state
  // Let session_capture create its own fresh source for reliable playback
  if (probe_source) {
    media_source_destroy(probe_source);
    probe_source = NULL;
  }

  session_capture_ctx_t *capture = session_capture_create(&capture_config);
  if (!capture) {
    log_fatal("Failed to initialize capture source");
    if (probe_source) {
      media_source_destroy(probe_source);
      probe_source = NULL;
    }
    // Clean up webcam resources and cached images on failure
    webcam_destroy();
    return ERROR_MEDIA_INIT;
  }

  // ============================================================================
  // PREPARE AUDIO (initialize context but don't start playback yet)
  // ============================================================================
  // Initialize audio context structure now so display can reference it,
  // but defer starting duplex playback until AFTER splash_intro_done()
  // This prevents audio from playing during the splash screen animation
  audio_context_t *audio_ctx = NULL;
  bool audio_available = false;
  bool should_init_audio = true;

  // Skip audio for immediate snapshots (snapshot_delay == 0) - optimization
  if (GET_OPTION(snapshot_mode) && GET_OPTION(snapshot_delay) == 0.0) {
    should_init_audio = false;
    log_debug("Skipping audio initialization for immediate snapshot (snapshot_delay=0)");
  }

  // Check if media source has audio and initialize context (but don't start playback)
  media_source_t *audio_probe_source = capture ? session_capture_get_media_source(capture) : NULL;
  if (should_init_audio && capture_config.type == MEDIA_SOURCE_FILE && capture_config.path && audio_probe_source) {
    if (media_source_has_audio(audio_probe_source)) {
      audio_available = true;
      audio_ctx = SAFE_MALLOC(sizeof(audio_context_t), audio_context_t *);
      if (audio_ctx) {
        *audio_ctx = (audio_context_t){0};
        if (audio_init(audio_ctx) == ASCIICHAT_OK) {
          // Store the media source in audio context for direct callback reading
          media_source_t *media_source = session_capture_get_media_source(capture);
          audio_ctx->media_source = media_source;

          // Store audio context in media source for seek buffer flushing
          if (media_source) {
            media_source_set_audio_context(media_source, audio_ctx);
          }

          // Determine if microphone should be enabled based on --audio-source setting
          bool should_enable_mic = audio_should_enable_microphone(GET_OPTION(audio_source), audio_available);
          audio_ctx->playback_only = !should_enable_mic;

          // Disable jitter buffering for local file playback
          if (audio_ctx->playback_buffer) {
            audio_ctx->playback_buffer->jitter_buffer_enabled = false;
            atomic_store(&audio_ctx->playback_buffer->jitter_buffer_filled, true);
          }

          // Store audio context in capture for keyboard handler access
          session_capture_set_audio_context(capture, audio_ctx);

          log_debug("Audio context initialized (playback will start after splash)");
        } else {
          log_warn("Failed to initialize audio context (audio_init returned error)");
          audio_destroy(audio_ctx);
          SAFE_FREE(audio_ctx);
          audio_ctx = NULL;
          audio_available = false;
        }
      }
    }
  }

  session_display_config_t display_config = {0};
  display_config.snapshot_mode = GET_OPTION(snapshot_mode);
  display_config.palette_type = GET_OPTION(palette_type);
  display_config.custom_palette = GET_OPTION(palette_custom_set) ? GET_OPTION(palette_custom) : NULL;
  display_config.color_mode = TERM_COLOR_AUTO;
  display_config.enable_audio_playback = audio_available;
  display_config.audio_ctx = audio_ctx;
  display_config.should_exit_callback = mirror_display_should_exit_adapter;
  display_config.callback_data = NULL;

  session_display_ctx_t *display = session_display_create(&display_config);
  if (!display) {
    log_fatal("Failed to initialize display");
    if (audio_ctx) {
      audio_stop_duplex(audio_ctx);
      audio_destroy(audio_ctx);
      SAFE_FREE(audio_ctx);
    }
    session_capture_destroy(capture);
    if (probe_source) {
      media_source_destroy(probe_source);
      probe_source = NULL;
    }
    // Clean up webcam resources and cached images on failure
    webcam_destroy();
    return ERROR_DISPLAY;
  }

  // Signal splash to stop - content is ready to render
  splash_intro_done();

  // Clean up temporary display context used for splash
  if (temp_display) {
    session_display_destroy(temp_display);
    temp_display = NULL;
  }

  // ============================================================================
  // START AUDIO PLAYBACK (after splash ends)
  // ============================================================================
  // Now that splash is done, start audio duplex playback
  // Audio context was already initialized before display creation, we just
  // need to start the actual PortAudio streams now
  if (audio_ctx && audio_available) {
    if (audio_start_duplex(audio_ctx) == ASCIICHAT_OK) {
      log_info("Audio playback started after splash screen");
    } else {
      log_warn("Failed to start audio duplex");
      audio_destroy(audio_ctx);
      SAFE_FREE(audio_ctx);
      audio_ctx = NULL;
      audio_available = false;

      // Update display to reflect audio is not available
      if (display) {
        // Display was created with audio_ctx, but now it's NULL
        // The display will handle this gracefully
      }
    }
  }

  // Run the unified render loop - handles frame capture, ASCII conversion, and rendering
  // Synchronous mode: pass capture context, NULL for callbacks
  // Keyboard support: pass handler and display context for help screen toggle
  asciichat_error_t result =
      session_render_loop(capture, display, mirror_render_should_exit,
                          NULL,                    // No custom capture callback
                          NULL,                    // No custom sleep callback
                          mirror_keyboard_handler, // Keyboard handler for interactive controls
                          display);                // user_data (display context for help screen and frame rendering)

  // Re-enable terminal logging after rendering
  // Always re-enable terminal output for shutdown logs
  log_set_terminal_output(true);

  log_debug("mirror_main: session_render_loop returned with result=%d", result);
  if (result != ASCIICHAT_OK) {
    log_error("Render loop failed with error code: %d", result);
  }

  log_debug("mirror_main: render loop complete, starting shutdown sequence");

  // Terminate PortAudio FIRST, BEFORE closing streams and destroying audio context
  // This ensures device resources are freed in the correct order
  log_debug("mirror_main: terminating PortAudio device resources");
  audio_terminate_portaudio_final();

  // Cleanup - must happen AFTER PortAudio is terminated
  // Stop audio FIRST to prevent callback from accessing media_source
  log_debug("mirror_main: starting cleanup, audio_ctx=%p", (void *)audio_ctx);
  if (audio_ctx) {
    log_debug("mirror_main: stopping audio duplex");
    audio_stop_duplex(audio_ctx);
    log_debug("mirror_main: destroying audio");
    audio_destroy(audio_ctx);
    log_debug("mirror_main: freeing audio_ctx");
    SAFE_FREE(audio_ctx);
    log_debug("mirror_main: audio cleanup complete");
  }

  session_display_destroy(display);
  session_capture_destroy(capture);

  // Free probe_source (we created it, so we must free it)
  // This is separate from capture's media source because capture doesn't own it
  if (probe_source) {
    media_source_destroy(probe_source);
    probe_source = NULL;
  }

  // Clean up webcam resources and cached images (test pattern, etc.)
  webcam_destroy();

  // Cleanup session log buffer (used by splash screen)
  session_log_buffer_destroy();

  // Re-enable terminal output for shutdown logging
  log_set_terminal_output(true);

  // Disable keepawake mode (re-allow OS to sleep)
  log_debug("mirror_main: disabling keepawake");
  platform_disable_keepawake();

  // Always show shutdown message unless --quiet is set
  // In snapshot mode, we suppress logs DURING rendering, but show them AFTER rendering completes
  if (!GET_OPTION(quiet)) {
    log_info("Mirror mode shutting down");
  }

  // Disable logging before final cleanup
  log_set_terminal_output(false);

  // Print newline to terminal to separate final frame from shutdown message (only on user Ctrl-C)
  if (mirror_should_exit() && platform_isatty(1)) { // 1 = stdout
    const char newline = '\n';
    (void)platform_write_all(STDOUT_FILENO, &newline, 1);
  }

  return 0;
}
