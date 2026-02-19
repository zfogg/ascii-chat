/**
 * @file client_like.c
 * @ingroup session
 * @brief Shared initialization and teardown for client-like modes
 */

#include <ascii-chat/session/client_like.h>
#include <ascii-chat/session/capture.h>
#include <ascii-chat/session/display.h>
#include <ascii-chat/session/render.h>
#include <ascii-chat/session/session_log_buffer.h>

#include <ascii-chat/media/source.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/ui/splash.h>

#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>

#include <string.h>
#include <stdatomic.h>

// Forward declarations (implemented below)
static bool capture_should_exit_adapter(void *user_data);
static bool display_should_exit_adapter(void *user_data);

// Module-level config for adapter callbacks
static const session_client_like_config_t *g_current_config = NULL;

// Module-level adapter functions for render loop access
static bool (*g_render_should_exit)(void *) = NULL;

// External exit function from src/main.c
extern bool should_exit(void);

/* ============================================================================
 * Public Accessor
 * ============================================================================ */

bool (*session_client_like_get_render_should_exit(void))(void *) {
  return g_render_should_exit;
}

/* ============================================================================
 * Exit Condition Adapters
 * ============================================================================ */

/**
 * Adapter for capture should_exit callback
 * Checks both global exit flag and mode-specific custom exit condition
 */
static bool capture_should_exit_adapter(void *user_data) {
  (void)user_data;
  if (should_exit()) {
    return true;
  }
  if (g_current_config && g_current_config->custom_should_exit) {
    return g_current_config->custom_should_exit(g_current_config->exit_user_data);
  }
  return false;
}

/**
 * Adapter for display should_exit callback
 * Checks both global exit flag and mode-specific custom exit condition
 */
static bool display_should_exit_adapter(void *user_data) {
  (void)user_data;
  if (should_exit()) {
    return true;
  }
  if (g_current_config && g_current_config->custom_should_exit) {
    return g_current_config->custom_should_exit(g_current_config->exit_user_data);
  }
  return false;
}

asciichat_error_t session_client_like_run(const session_client_like_config_t *config) {
  if (!config || !config->run_fn) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "config or run_fn is NULL");
  }

  // Store config globally for adapter callbacks
  g_current_config = config;

  // Store the render should_exit adapter globally so mode-specific run_fn can access it
  g_render_should_exit = display_should_exit_adapter;

  log_debug("session_client_like_run() starting");

  asciichat_error_t result = ASCIICHAT_OK;

  // Keep track of what's been initialized for cleanup
  session_display_ctx_t *temp_display = NULL;
  session_capture_ctx_t *capture = NULL;
  session_display_ctx_t *display = NULL;
  audio_context_t *audio_ctx = NULL;
  media_source_t *probe_source = NULL;
  bool audio_available = false;

  // ============================================================================
  // SETUP: Terminal and Logging
  // ============================================================================

  log_debug("session_client_like_run(): Setting up terminal and logging");

  // Force stderr when stdout is piped (prevents ASCII corruption in output stream)
  bool should_force_stderr = terminal_should_force_stderr();
  log_debug("terminal_should_force_stderr()=%d", should_force_stderr);

  if (should_force_stderr) {
    // Redirect logs to stderr to prevent corruption of stdout (for pipes)
    // But keep terminal output enabled so we can see initialization errors
    log_set_force_stderr(true);
  }

  // ============================================================================
  // SETUP: Keepawake System
  // ============================================================================

  log_debug("session_client_like_run(): Validating keepawake options");

  // Validate mutual exclusivity
  bool enable_ka = GET_OPTION(enable_keepawake);
  bool disable_ka = GET_OPTION(disable_keepawake);
  log_debug("enable_keepawake=%d, disable_keepawake=%d", enable_ka, disable_ka);

  if (enable_ka && disable_ka) {
    result = SET_ERRNO(ERROR_INVALID_PARAM, "--keepawake and --no-keepawake are mutually exclusive");
    goto cleanup;
  }

  log_debug("session_client_like_run(): Enabling keepawake if needed");

  // Enable keepawake unless explicitly disabled
  if (!disable_ka) {
    (void)platform_enable_keepawake();
  }

  log_debug("session_client_like_run(): Keepawake setup complete");

  // ============================================================================
  // SETUP: Splash Screen (before media initialization)
  // ============================================================================

  log_debug("session_client_like_run(): Creating temporary display for splash");

  temp_display = session_display_create(NULL);
  if (temp_display) {
    splash_intro_start(temp_display);

    // Detect if we're using media vs webcam (needed for splash timing)
    const char *media_url = GET_OPTION(media_url);
    const char *media_file = GET_OPTION(media_file);
    bool has_media = (media_url && strlen(media_url) > 0) || (media_file && strlen(media_file) > 0);

    // Show splash briefly for webcam, but skip sleep for media (which takes time anyway)
    if (!has_media && !GET_OPTION(snapshot_mode)) {
      platform_sleep_ms(250);
    }
  }

  // ============================================================================
  // SETUP: Terminal Logging in Snapshot Mode
  // ============================================================================

  if (!GET_OPTION(snapshot_mode)) {
    log_set_terminal_output(false);
  }

  // ============================================================================
  // SETUP: Media Source Selection and FPS Probing
  // ============================================================================

  session_capture_config_t capture_config = {0};
  capture_config.resize_for_network = false;
  capture_config.should_exit_callback = capture_should_exit_adapter;
  capture_config.callback_data = NULL;

  // Determine FPS explicitly set by user
  int user_fps = GET_OPTION(fps);
  bool fps_explicitly_set = user_fps > 0;

  // Select media source based on options (priority order)
  const char *media_url_val = GET_OPTION(media_url);
  const char *media_file_val = GET_OPTION(media_file);

  if (media_url_val && strlen(media_url_val) > 0) {
    // Network URL takes priority
    log_info("Using network URL: %s (webcam disabled)", media_url_val);
    capture_config.type = MEDIA_SOURCE_FILE;
    capture_config.path = media_url_val;
    capture_config.loop = false; // Network URLs cannot be looped

    if (fps_explicitly_set) {
      capture_config.target_fps = (uint32_t)user_fps;
      log_info("Using user-specified FPS: %u", capture_config.target_fps);
    } else {
      // Probe FPS for HTTP URLs
      probe_source = media_source_create(MEDIA_SOURCE_FILE, media_url_val);
      if (probe_source) {
        double url_fps = media_source_get_video_fps(probe_source);
        log_info("Detected HTTP stream video FPS: %.1f", url_fps);
        if (url_fps > 0.0) {
          capture_config.target_fps = (uint32_t)(url_fps + 0.5);
        } else {
          log_warn("FPS detection failed for HTTP stream, using default 60 FPS");
          capture_config.target_fps = 60;
        }
      } else {
        log_warn("Failed to create probe source for HTTP stream, using default 60 FPS");
        capture_config.target_fps = 60;
      }
    }
  } else if (media_file_val && strlen(media_file_val) > 0) {
    // Local file or stdin
    if (strcmp(media_file_val, "-") == 0) {
      log_info("Using stdin for media streaming (webcam disabled)");
      capture_config.type = MEDIA_SOURCE_STDIN;
      capture_config.path = NULL;
      capture_config.target_fps = fps_explicitly_set ? (uint32_t)user_fps : 60;
      capture_config.loop = false;
    } else {
      log_info("Using media file: %s (webcam disabled)", media_file_val);
      capture_config.type = MEDIA_SOURCE_FILE;
      capture_config.path = media_file_val;
      capture_config.loop = GET_OPTION(media_loop);

      if (fps_explicitly_set) {
        capture_config.target_fps = (uint32_t)user_fps;
        log_info("Using user-specified FPS: %u", capture_config.target_fps);
      } else {
        // Probe FPS for local files
        probe_source = media_source_create(MEDIA_SOURCE_FILE, media_file_val);
        if (probe_source) {
          double file_fps = media_source_get_video_fps(probe_source);
          log_info("Detected file video FPS: %.1f", file_fps);
          if (file_fps > 0.0) {
            capture_config.target_fps = (uint32_t)(file_fps + 0.5);
          } else {
            log_warn("FPS detection failed, using default 60 FPS");
            capture_config.target_fps = 60;
          }
        } else {
          log_warn("Failed to create probe source for FPS detection, using default 60 FPS");
          capture_config.target_fps = 60;
        }
      }
    }
  } else if (GET_OPTION(test_pattern)) {
    // Test pattern
    log_info("Using test pattern");
    capture_config.type = MEDIA_SOURCE_TEST;
    capture_config.path = NULL;
    capture_config.target_fps = fps_explicitly_set ? (uint32_t)user_fps : 60;
    capture_config.loop = false;
  } else {
    // Default: webcam
    log_info("Using local webcam");
    capture_config.type = MEDIA_SOURCE_WEBCAM;
    capture_config.path = NULL;
    capture_config.target_fps = fps_explicitly_set ? (uint32_t)user_fps : 60;
    capture_config.loop = false;
  }

  // Apply initial seek if specified
  capture_config.initial_seek_timestamp = GET_OPTION(media_seek_timestamp);

  // Clean up probe source (don't reuse for actual capture)
  if (probe_source) {
    media_source_destroy(probe_source);
    probe_source = NULL;
  }

  // ============================================================================
  // SETUP: Capture Context
  // ============================================================================

  capture = session_capture_create(&capture_config);
  if (!capture) {
    log_fatal("Failed to initialize capture source");
    result = ERROR_MEDIA_INIT;
    goto cleanup;
  }

  // ============================================================================
  // SETUP: Audio Context
  // ============================================================================

  // Skip audio for immediate snapshots
  bool should_init_audio = true;
  if (GET_OPTION(snapshot_mode) && GET_OPTION(snapshot_delay) == 0.0) {
    should_init_audio = false;
    log_debug("Skipping audio initialization for immediate snapshot");
  }

  // Probe for audio
  if (should_init_audio && capture_config.type == MEDIA_SOURCE_FILE && capture_config.path) {
    media_source_t *audio_probe_source = session_capture_get_media_source(capture);
    if (audio_probe_source && media_source_has_audio(audio_probe_source)) {
      audio_available = true;

      // Allocate and initialize audio context
      audio_ctx = SAFE_MALLOC(sizeof(audio_context_t), audio_context_t *);
      if (audio_ctx) {
        *audio_ctx = (audio_context_t){0};
        if (audio_init(audio_ctx) == ASCIICHAT_OK) {
          // Link audio to media source
          media_source_t *media_source = session_capture_get_media_source(capture);
          audio_ctx->media_source = media_source;

          if (media_source) {
            media_source_set_audio_context(media_source, audio_ctx);
          }

          // Determine if microphone should be enabled
          bool should_enable_mic = audio_should_enable_microphone(GET_OPTION(audio_source), audio_available);
          audio_ctx->playback_only = !should_enable_mic;

          // Disable jitter buffering for file playback
          if (audio_ctx->playback_buffer) {
            audio_ctx->playback_buffer->jitter_buffer_enabled = false;
            atomic_store(&audio_ctx->playback_buffer->jitter_buffer_filled, true);
          }

          // Store in capture for keyboard handler access
          session_capture_set_audio_context(capture, audio_ctx);

          log_debug("Audio context initialized");
        } else {
          log_warn("Failed to initialize audio context");
          audio_destroy(audio_ctx);
          SAFE_FREE(audio_ctx);
          audio_ctx = NULL;
          audio_available = false;
        }
      }
    }
  }

  // ============================================================================
  // SETUP: Display Context
  // ============================================================================

  session_display_config_t display_config = {0};
  display_config.snapshot_mode = GET_OPTION(snapshot_mode);
  display_config.palette_type = GET_OPTION(palette_type);
  display_config.custom_palette = GET_OPTION(palette_custom_set) ? GET_OPTION(palette_custom) : NULL;
  display_config.color_mode = TERM_COLOR_AUTO;
  display_config.enable_audio_playback = audio_available;
  display_config.audio_ctx = audio_ctx;
  display_config.should_exit_callback = display_should_exit_adapter;
  display_config.callback_data = NULL;

  display = session_display_create(&display_config);
  if (!display) {
    log_fatal("Failed to initialize display");
    result = ERROR_DISPLAY;
    goto cleanup;
  }

  // ============================================================================
  // SETUP: End Splash Screen
  // ============================================================================

  log_debug("About to call splash_intro_done()");
  splash_intro_done();
  log_debug("splash_intro_done() returned");

  if (temp_display) {
    session_display_destroy(temp_display);
    temp_display = NULL;
  }

  // ============================================================================
  // SETUP: Start Audio Playback
  // ============================================================================

  log_debug("About to check audio context for duplex");
  if (audio_ctx && audio_available) {
    if (audio_start_duplex(audio_ctx) == ASCIICHAT_OK) {
      log_info("Audio playback started");
    } else {
      log_warn("Failed to start audio duplex");
      audio_destroy(audio_ctx);
      SAFE_FREE(audio_ctx);
      audio_ctx = NULL;
      audio_available = false;
    }
  }

  // ============================================================================
  // RUN: Mode-Specific Main Loop
  // ============================================================================

  log_debug("About to call config->run_fn()");
  result = config->run_fn(capture, display, config->run_user_data);
  log_debug("config->run_fn() returned with result=%d", result);

  // ============================================================================
  // CLEANUP (always runs, even on error)
  // ============================================================================

cleanup:
  // Re-enable terminal output for shutdown logs
  log_set_terminal_output(true);

  // CRITICAL: Terminate PortAudio device resources FIRST
  log_debug("Terminating PortAudio device resources");
  audio_terminate_portaudio_final();

  // Stop and destroy audio (after PortAudio is terminated)
  if (audio_ctx) {
    audio_stop_duplex(audio_ctx);
    audio_destroy(audio_ctx);
    SAFE_FREE(audio_ctx);
    audio_ctx = NULL;
  }

  // Destroy display
  if (display) {
    session_display_destroy(display);
    display = NULL;
  }

  // Destroy capture
  if (capture) {
    session_capture_destroy(capture);
    capture = NULL;
  }

  // Clean up probe source if still allocated (shouldn't happen, but be safe)
  if (probe_source) {
    media_source_destroy(probe_source);
    probe_source = NULL;
  }

  // Free cached webcam images and test patterns
  webcam_destroy();

  // Cleanup session log buffer (used by splash screen)
  session_log_buffer_destroy();

  // Disable keepawake (re-allow OS to sleep)
  log_debug("Disabling keepawake");
  platform_disable_keepawake();

  // Write newline to separate final frame from prompt (if configured)
  if (config->print_newline_on_tty_exit && terminal_is_stdout_tty()) {
    const char newline = '\n';
    (void)platform_write_all(STDOUT_FILENO, &newline, 1);
  }

  log_set_terminal_output(false);

  return result;
}
