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
#include "session/capture.h"
#include "session/display.h"
#include "session/render.h"

#include "media/source.h"
#include "media/youtube.h"
#include "audio/audio.h"
#include "common.h"
#include "options/options.h"

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
 * Unix signal handler for graceful shutdown on SIGTERM
 *
 * @param sig The signal number (unused, but required by signal handler signature)
 */
static void mirror_handle_sigterm(int sig) {
  (void)sig; // Unused
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
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 1);
#else
    _exit(1);
#endif
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
  log_info("Starting mirror mode");

  // Install console control-c handler
  platform_set_console_ctrl_handler(mirror_console_ctrl_handler);

#ifndef _WIN32
  platform_signal(SIGPIPE, SIG_IGN);
  // Handle SIGTERM gracefully for timeout(1) support
  platform_signal(SIGTERM, mirror_handle_sigterm);
#endif

  // Configure media source based on options
  const char *media_url = GET_OPTION(media_url);
  const char *media_file = GET_OPTION(media_file);
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
    log_info("Using network URL: %s", media_url);
    capture_config.type = MEDIA_SOURCE_FILE;
    capture_config.path = media_url;

    // Create probe source once and reuse for FPS and audio detection
    probe_source = media_source_create(MEDIA_SOURCE_FILE, media_url);
    if (probe_source) {
      double url_fps = media_source_get_video_fps(probe_source);
      if (url_fps > 0.0) {
        capture_config.target_fps = (uint32_t)(url_fps + 0.5);
        log_debug("URL FPS: %.1f (using %u)", url_fps, capture_config.target_fps);
      }
    }
    capture_config.loop = false; // Network URLs cannot be looped
  } else if (media_file && strlen(media_file) > 0) {
    // User specified a media file or stdin
    if (strcmp(media_file, "-") == 0) {
      capture_config.type = MEDIA_SOURCE_STDIN;
      capture_config.path = NULL;
    } else {
      capture_config.type = MEDIA_SOURCE_FILE;
      capture_config.path = media_file;

      // Create probe source once and reuse for FPS and audio detection
      probe_source = media_source_create(MEDIA_SOURCE_FILE, media_file);
      if (probe_source) {
        double file_fps = media_source_get_video_fps(probe_source);
        if (file_fps > 0.0) {
          capture_config.target_fps = (uint32_t)(file_fps + 0.5);
          log_debug("File FPS: %.1f (using %u)", file_fps, capture_config.target_fps);
        }
      }
    }
    capture_config.loop = GET_OPTION(media_loop);
  } else {
    // Default to webcam
    capture_config.type = MEDIA_SOURCE_WEBCAM;
    capture_config.path = NULL;
    capture_config.loop = false;
  }

  // Add seek timestamp if specified
  capture_config.initial_seek_timestamp = GET_OPTION(media_seek_timestamp);

  // For local files: pass probe_source to reuse (avoids file reopens)
  // For YouTube URLs: don't pass probe_source because FPS detection may have altered decoder state
  // The yt-dlp cache (30 seconds) prevents redundant network requests anyway
  bool is_youtube_url = media_url && strlen(media_url) > 0 && youtube_is_youtube_url(media_url);
  if (!is_youtube_url && probe_source) {
    capture_config.media_source = probe_source;
  }
  // Note: For YouTube, let session_capture create its own source

  session_capture_ctx_t *capture = session_capture_create(&capture_config);
  if (!capture) {
    log_fatal("Failed to initialize capture source");
    return ERROR_MEDIA_INIT;
  }

  // Initialize audio for playback if media file has audio
  audio_context_t *audio_ctx = NULL;
  bool audio_available = false;

  // Check if file/URL has audio stream
  // For YouTube: use capture's media source (probe_source not reused due to FPS detection altering state)
  // For files: use probe_source if available
  media_source_t *audio_probe_source =
      (is_youtube_url && capture) ? session_capture_get_media_source(capture) : probe_source;
  if (capture_config.type == MEDIA_SOURCE_FILE && capture_config.path && audio_probe_source) {
    if (media_source_has_audio(audio_probe_source)) {
      audio_available = true;
      audio_ctx = SAFE_MALLOC(sizeof(audio_context_t), audio_context_t *);
      if (audio_ctx) {
        *audio_ctx = (audio_context_t){0};
        if (audio_init(audio_ctx) == ASCIICHAT_OK) {
          // Store the media source in audio context for direct callback reading
          audio_ctx->media_source = session_capture_get_media_source(capture);

          // Enable playback-only mode (no microphone input in mirror mode)
          audio_ctx->playback_only = true;

          // Disable jitter buffering for local file playback
          if (audio_ctx->playback_buffer) {
            audio_ctx->playback_buffer->jitter_buffer_enabled = false;
            atomic_store(&audio_ctx->playback_buffer->jitter_buffer_filled, true);
          }

          if (audio_start_duplex(audio_ctx) == ASCIICHAT_OK) {
            log_info("Audio playback initialized for media file");
          } else {
            log_warn("Failed to start audio duplex");
            audio_destroy(audio_ctx);
            SAFE_FREE(audio_ctx);
            audio_ctx = NULL;
            audio_available = false;
          }
        } else {
          log_warn("Failed to initialize audio context");
          SAFE_FREE(audio_ctx);
          audio_ctx = NULL;
          audio_available = false;
        }
      }
    }
  }
  // Note: probe_source is now managed by session_capture (not owned by us)

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
    session_capture_destroy(capture);
    return ERROR_DISPLAY;
  }

  log_info("Mirror mode running - press Ctrl+C to exit");
  log_set_terminal_output(false);

  // Run the unified render loop - handles frame capture, ASCII conversion, and rendering
  // Synchronous mode: pass capture context, NULL for callbacks
  asciichat_error_t result = session_render_loop(capture, display, mirror_render_should_exit,
                                                 NULL,  // No custom capture callback
                                                 NULL,  // No custom sleep callback
                                                 NULL); // No user_data needed

  if (result != ASCIICHAT_OK) {
    log_error("Render loop failed with error code: %d", result);
  }

  // Cleanup
  log_set_terminal_output(true);
  log_info("Mirror mode shutting down");

  // Stop audio FIRST to prevent callback from accessing media_source
  if (audio_ctx) {
    audio_stop_duplex(audio_ctx);
    audio_destroy(audio_ctx);
    SAFE_FREE(audio_ctx);
  }

  session_display_destroy(display);
  session_capture_destroy(capture);

  // Free probe_source (we created it, so we must free it)
  // This is separate from capture's media source because capture doesn't own it
  if (probe_source) {
    media_source_destroy(probe_source);
    probe_source = NULL;
  }

  return 0;
}
