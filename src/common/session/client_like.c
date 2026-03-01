/**
 * @file client_like.c
 * @ingroup session
 * @brief Shared initialization and teardown for client-like modes
 */

#include "session/client_like.h"
#include "session/capture.h"
#include "session/display.h"
#include "session/render.h"
#include "session/session_log_buffer.h"
#include "session/stdin_reader.h"
#include "../../client/audio.h"

#include <ascii-chat/media/source.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/debug/sync.h>

#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/network/tcp/client.h>
#include <ascii-chat/network/websocket/client.h>
#include <ascii-chat/util/url.h>
#include <ascii-chat/app_callbacks.h>

#include <string.h>
#include <stdatomic.h>

// Forward declarations (implemented below)
static bool capture_should_exit_adapter(void *user_data);
static bool display_should_exit_adapter(void *user_data);

// Module-level config for adapter callbacks
static const session_client_like_config_t *g_current_config = NULL;

// Module-level adapter functions for render loop access
static bool (*g_render_should_exit)(void *) = NULL;

// Module-level network clients (created by framework, accessed by run_fn)
static tcp_client_t *g_tcp_client = NULL;
static websocket_client_t *g_websocket_client = NULL;

// Module-level stdin reader for ASCII-to-video rendering (stdin render mode only)
static stdin_frame_reader_t *g_stdin_reader = NULL;

/* ============================================================================
 * Public Accessors
 * ============================================================================ */

bool (*session_client_like_get_render_should_exit(void))(void *) {
  return g_render_should_exit;
}

tcp_client_t *session_client_like_get_tcp_client(void) {
  return g_tcp_client;
}

websocket_client_t *session_client_like_get_websocket_client(void) {
  return g_websocket_client;
}

void session_client_like_set_websocket_client(websocket_client_t *client) {
  g_websocket_client = client;
}

stdin_frame_reader_t *session_client_like_get_stdin_reader(void) {
  return g_stdin_reader;
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
  if (APP_CALLBACK_BOOL(should_exit)) {
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
  if (APP_CALLBACK_BOOL(should_exit)) {
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

  // Create temporary display for splash with render-file disabled
  session_display_config_t splash_config = {
      .snapshot_mode = GET_OPTION(snapshot_mode),
      .palette_type = GET_OPTION(palette_type),
      .custom_palette = GET_OPTION(palette_custom_set) ? GET_OPTION(palette_custom) : NULL,
      .color_mode = TERM_COLOR_AUTO,
      .skip_render_file = true, // Skip render-file for temporary splash display
  };
  temp_display = session_display_create(&splash_config);
  if (temp_display) {
    splash_intro_start(temp_display);
    log_debug("session_client_like_run(): splash_intro_start() returned");

    // Detect if we're using media vs webcam (needed for splash timing)
    const char *media_url = GET_OPTION(media_url);
    const char *media_file = GET_OPTION(media_file);
    bool has_media = (media_url && strlen(media_url) > 0) || (media_file && strlen(media_file) > 0);

    // Show splash briefly for webcam, but skip sleep for media (which takes time anyway)
    if (!has_media && !GET_OPTION(snapshot_mode)) {
      platform_sleep_ms(250);
    }
    log_debug("session_client_like_run(): After splash sleep");

    // Restore stderr now that splash animation and post-splash logging are done
    // This allows logs to appear on screen again after this point
    splash_restore_stderr();
  }

  // ============================================================================
  // SETUP: Terminal Logging in Snapshot Mode
  // ============================================================================

  // ============================================================================
  // SETUP: Terminal Logging in Snapshot Mode
  // ============================================================================

  log_debug("session_client_like_run(): About to disable terminal logging (snapshot=%d)", GET_OPTION(snapshot_mode));
  if (!GET_OPTION(snapshot_mode)) {
    log_debug("session_client_like_run(): Calling log_set_terminal_output(false)...");
    log_set_terminal_output(false);
    log_debug("session_client_like_run(): RETURNED from log_set_terminal_output(false)");
  }
  log_debug("session_client_like_run(): Terminal logging disabled, starting media source setup");

  // ============================================================================
  // SETUP: Media Source Selection and FPS Probing
  // ============================================================================

  log_debug("session_client_like_run(): Initializing capture config");
  session_capture_config_t capture_config = {0};
  capture_config.resize_for_network = false;
  capture_config.should_exit_callback = capture_should_exit_adapter;
  capture_config.callback_data = NULL;

  // Determine FPS explicitly set by user
  log_debug("session_client_like_run(): Getting FPS option");
  int user_fps = GET_OPTION(fps);
  bool fps_explicitly_set = user_fps > 0;
  log_debug("session_client_like_run(): FPS=%d (explicitly_set=%d)", user_fps, fps_explicitly_set);

  // Select media source based on options (priority order)
  log_debug("session_client_like_run(): Getting media_url option");
  const char *media_url_val = GET_OPTION(media_url);
  log_debug("session_client_like_run(): Getting media_file option");
  const char *media_file_val = GET_OPTION(media_file);
  log_debug("session_client_like_run(): media_url=%s, media_file=%s", media_url_val ? media_url_val : "(null)",
            media_file_val ? media_file_val : "(null)");

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
    log_debug("session_client_like_run(): Using test pattern");
    log_info("Using test pattern");
    capture_config.type = MEDIA_SOURCE_TEST;
    capture_config.path = NULL;
    capture_config.target_fps = fps_explicitly_set ? (uint32_t)user_fps : 60;
    capture_config.loop = false;
  } else {
    // Default: webcam
    log_debug("session_client_like_run(): Using local webcam (about to log)");
    log_info("Using local webcam");
    log_debug("session_client_like_run(): Setting webcam capture config");
    capture_config.type = MEDIA_SOURCE_WEBCAM;
    capture_config.path = NULL;
    capture_config.target_fps = fps_explicitly_set ? (uint32_t)user_fps : 60;
    capture_config.loop = false;
    log_debug("session_client_like_run(): Webcam config set (type=%d, fps=%u)", capture_config.type,
              capture_config.target_fps);
  }

  // Apply initial seek if specified
  log_debug("session_client_like_run(): Getting media_seek_timestamp option");
  capture_config.initial_seek_timestamp = GET_OPTION(media_seek_timestamp);
  log_debug("session_client_like_run(): Media seek timestamp set");

  // Clean up probe source (don't reuse for actual capture)
  if (probe_source) {
    log_debug("session_client_like_run(): Destroying probe source");
    media_source_destroy(probe_source);
    probe_source = NULL;
    log_debug("session_client_like_run(): Probe source destroyed");
  } else {
    log_debug("session_client_like_run(): No probe source to destroy");
  }

  // ============================================================================
  // SETUP: Network Transports (TCP/WebSocket)
  // ============================================================================

  log_debug("session_client_like_run(): Setting up network transports");

  // Use network clients from config if provided (client mode), or skip for mirror mode
  // Mirror mode passes NULL for both tcp_client and websocket_client
  g_tcp_client = config->tcp_client;
  g_websocket_client = config->websocket_client;

  // Determine the networking mode based on available indicators:
  // - If config has a discovery object, it's discovery mode (will create network clients later)
  // - If config already has tcp_client or websocket_client, it's network mode (client mode)
  // - Otherwise, it's mirror mode (local-only capture)
  // Mirror mode has a run_fn (mirror_run) but no network components
  bool discovery_mode = (config->discovery != NULL);
  bool network_mode = (g_tcp_client != NULL || g_websocket_client != NULL);
  bool mirror_mode = (!discovery_mode && !network_mode);

  if (mirror_mode) {
    log_debug("Mirror mode detected - will use local capture with media source");
  } else if (discovery_mode) {
    log_debug("Discovery mode detected - discovery session will manage networking");
  } else if (network_mode) {
    log_debug("Client/Network mode detected - will use network capture without local media source");
  }

  // ============================================================================
  // SETUP: Capture Context
  // ============================================================================

  // Check for stdin render mode: read ASCII frames from stdin, render to video
  const char *render_file_opt = GET_OPTION(render_file);
  bool stdin_render_mode = (render_file_opt && strcmp(render_file_opt, "-") == 0 && !terminal_is_stdin_tty());

  if (stdin_render_mode) {
    // Stdin render mode: read ASCII frames from stdin, output video to stdout
    log_info("Stdin render mode enabled: reading ASCII frames from stdin, output to stdout");

    // Require explicit --height when reading from stdin
    // (height determines frame boundaries; width can be detected from line lengths)
    int frame_height = GET_OPTION(height);
    int frame_width = GET_OPTION(width);

    // Validate stdin render mode constraints
    bool height_was_not_set = (frame_height == OPT_HEIGHT_DEFAULT);
    bool width_was_explicitly_set = (frame_width != OPT_WIDTH_DEFAULT);

    if (height_was_not_set) {
      result = SET_ERRNO(ERROR_USAGE, "Stdin render mode requires explicit frame height.\n"
                                      "Please specify: --height <rows>");
      goto cleanup;
    }

    if (width_was_explicitly_set) {
      result = SET_ERRNO(ERROR_USAGE, "Stdin render mode does not accept --width (auto-detected from frames).\n"
                                      "Only specify: --height <rows>");
      goto cleanup;
    }

    asciichat_error_t stdin_err = stdin_frame_reader_create(frame_height, &g_stdin_reader);
    if (stdin_err != ASCIICHAT_OK) {
      log_fatal("Failed to initialize stdin frame reader: %s", asciichat_error_string(stdin_err));
      result = ERROR_MEDIA_INIT;
      goto cleanup;
    }

    // For stdin render mode, create a minimal capture context for compatibility
    int fps = GET_OPTION(fps);
    capture = session_network_capture_create((uint32_t)(fps > 0 ? fps : 60));
    if (!capture) {
      log_fatal("Failed to initialize capture context for stdin rendering");
      stdin_frame_reader_destroy(g_stdin_reader);
      g_stdin_reader = NULL;
      result = ERROR_MEDIA_INIT;
      goto cleanup;
    }

    log_info("stdin render mode: reading %d-line frames from stdin, auto-detecting width", frame_height);
  }

  // Choose capture type based on mode determined earlier:
  // - Mirror mode: needs to capture local media (webcam, file, test pattern)
  // - Network modes (client/discovery): receive frames from network, no local capture
  bool is_network_mode = network_mode || discovery_mode;

  if (!stdin_render_mode && is_network_mode) {
    // Network mode: create minimal capture context without media source
    log_debug("Network mode detected - using network capture (no local media source)");
    int fps = GET_OPTION(fps);
    uint64_t net_cap_start = time_get_ns();
    capture = session_network_capture_create((uint32_t)(fps > 0 ? fps : 60));
    double net_cap_ms = time_ns_to_ms(time_elapsed_ns(net_cap_start, time_get_ns()));
    log_info("★ INIT_CHECKPOINT: Network capture created in %.1f ms", net_cap_ms);
    if (!capture) {
      log_fatal("Failed to initialize network capture context");
      result = ERROR_MEDIA_INIT;
      goto cleanup;
    }
    if (fps > 0) {
      log_debug("Network capture FPS set to %d from options", fps);
    }
  } else if (!stdin_render_mode) {
    // Mirror mode: create capture context with local media source
    log_debug("Mirror mode detected - using mirror capture with local media source");
    uint64_t mirror_cap_start = time_get_ns();
    capture = session_mirror_capture_create(&capture_config);
    double mirror_cap_ms = time_ns_to_ms(time_elapsed_ns(mirror_cap_start, time_get_ns()));
    log_info("★ INIT_CHECKPOINT: Mirror capture created in %.1f ms", mirror_cap_ms);
    if (!capture) {
      log_fatal("Failed to initialize mirror capture source");
      result = ERROR_MEDIA_INIT;
      goto cleanup;
    }
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

  log_debug("[SETUP_DISPLAY] Creating display context");
  display = session_display_create(&display_config);
  if (!display) {
    log_debug("[SETUP_DISPLAY] session_display_create() returned NULL - checking error");
    asciichat_error_context_t ctx;
    if (HAS_ERRNO(&ctx)) {
      log_debug("[SETUP_DISPLAY] Error context: %s", ctx.context_message);
    } else {
      log_debug("[SETUP_DISPLAY] No error context available");
    }
    log_fatal("Failed to initialize display");

    result = ERROR_DISPLAY;
    goto cleanup;
  }
  log_debug("[SETUP_DISPLAY] Display context created");

  // Pass stdin_reader to display if in stdin render mode
  if (stdin_render_mode && g_stdin_reader) {
    session_display_set_stdin_reader(display, g_stdin_reader);
    log_debug("stdin_reader passed to display context");
  }

  // ============================================================================
  // SETUP: End Splash Screen
  // ============================================================================
  log_debug("[SETUP_SPLASH] About to end splash screen");

  // The splash screen will display during initialization and exit when done.
  // The initialization process (media probing, display setup, etc.) naturally
  // takes time, keeping the splash visible. The splash is kept alive during
  // connection attempts (TCP and WebSocket) to prevent a blank screen during
  // network operations. splash_intro_done() is called only after a successful
  // connection is established.

  log_debug("[SETUP_SPLASH] Splash will remain visible during connection attempts");

  // Exit early if shutdown was requested (e.g., user pressed Ctrl-C)
  if (APP_CALLBACK_BOOL(should_exit)) {
    log_debug("[SETUP] Shutdown requested, exiting early");
    log_debug("[SETUP_SPLASH] Ending splash due to early shutdown request");
    splash_intro_done();
    splash_wait_for_animation();
    if (temp_display) {
      session_display_destroy(temp_display);
      temp_display = NULL;
    }
    result = ASCIICHAT_OK;
    goto cleanup;
  }

  if (temp_display) {
    session_display_destroy(temp_display);
    temp_display = NULL;
  }

  // ============================================================================
  // SETUP: Start Audio Playback
  // ============================================================================

  log_debug("[SETUP_AUDIO] About to check audio context for duplex");
  if (audio_ctx && audio_available) {
    log_debug("[SETUP_AUDIO] Starting audio duplex");
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
  log_debug("[SETUP_COMPLETE] All setup complete, about to start connection loop");

  // ============================================================================
  // RUN: Mode-Specific Main Loop with Reconnection
  // ============================================================================

  // Connection/attempt loop - wraps run_fn with reconnection logic
  int attempt = 0;
  int max_attempts = config->max_reconnect_attempts;

  log_debug("[CLIENT_LIKE_LOOP] Starting connection loop, max_attempts=%d", max_attempts);

  while (true) {
    attempt++;

    log_debug("[CLIENT_LIKE_LOOP] About to call config->run_fn() (attempt %d)", attempt);
    result = config->run_fn(capture, display, config->run_user_data);
    log_debug("[CLIENT_LIKE_LOOP] config->run_fn() returned with result=%d", result);

    // Exit immediately if run_fn succeeded
    if (result == ASCIICHAT_OK) {
      // Connection succeeded - splash screen cleanup now happens when first frame renders
      // This ensures the splash stays visible during TCP/WebSocket/datachannel connection attempts
      log_debug("[CLIENT_LIKE_LOOP] Connection established, splash cleanup will occur on first frame");

      break;
    }

    // Check if we should attempt reconnection
    bool should_retry = false;

    if (max_attempts != 0) { // max_attempts == 0 means no retries
      // Check custom reconnect logic if provided
      if (config->should_reconnect_callback) {
        should_retry = config->should_reconnect_callback(result, attempt, config->reconnect_user_data);
      } else {
        should_retry = true; // Default: always retry
      }

      // Check retry limits if still planning to retry
      if (should_retry && max_attempts > 0 && attempt >= max_attempts) {
        log_debug("Reached maximum reconnection attempts (%d), giving up", max_attempts);
        should_retry = false;
      }
    }

    // If not retrying or shutdown requested, exit loop
    if (!should_retry || APP_CALLBACK_BOOL(should_exit)) {
      break;
    }

    // Reconnection will happen - show splash screen during reconnection attempt
    // Reset first_frame flag so splash cleanup runs on next successful connection
    log_debug("[CLIENT_LIKE_LOOP] Reconnection will be attempted, showing splash screen");
    session_display_reset_first_frame(display);

    // splash_intro_start() handles all the logic:
    // - If splash should display: shows splash and disables console logging
    // - If splash shouldn't display: restores stderr and returns without changing logging state
    splash_intro_start(display);

    // Log reconnection message
    if (max_attempts == -1) {
      log_info("Connection failed, retrying...");
    } else if (max_attempts > 0) {
      log_info("Connection failed, retrying (attempt %d/%d)...", attempt + 1, max_attempts);
    }

    // Apply reconnection delay if configured
    // Check APP_CALLBACK_BOOL(should_exit) frequently during sleep so SIGTERM can interrupt reconnection attempts
    if (config->reconnect_delay_ms > 0) {
      unsigned int remaining_ms = config->reconnect_delay_ms;
      const unsigned int check_interval_ms = 100; // Check exit flag every 100ms

      while (remaining_ms > 0 && !APP_CALLBACK_BOOL(should_exit)) {
        unsigned int sleep_ms = (remaining_ms < check_interval_ms) ? remaining_ms : check_interval_ms;
        platform_sleep_ms(sleep_ms);
        remaining_ms -= sleep_ms;
      }
    }

    // Continue loop to retry
  }

  // ============================================================================
  // CLEANUP (always runs, even on error)
  // ============================================================================

cleanup:
  log_debug("[CLIENT_LIKE_CLEANUP] Reached cleanup label");
  // Re-enable terminal output for shutdown logs
  log_set_terminal_output(true);

  // Cleanup network transports (TCP/WebSocket clients)
  if (g_websocket_client) {
    log_debug("Destroying WebSocket client");
    websocket_client_destroy(&g_websocket_client);
  }
  if (g_tcp_client) {
    log_debug("Destroying TCP client");
    tcp_client_destroy(&g_tcp_client);
  }

  // CRITICAL: Terminate PortAudio device resources FIRST
  log_debug("Terminating PortAudio device resources");
  audio_terminate_portaudio_final();

  // Stop audio thread before destroying audio context to prevent use-after-free
  // The audio worker thread may still be logging when we destroy the buffer
  APP_CALLBACK_VOID(audio_stop_thread);

  // Stop and destroy audio (after PortAudio is terminated and audio thread is stopped)
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

  // Destroy stdin reader if active (stdin render mode)
  if (g_stdin_reader) {
    stdin_frame_reader_destroy(g_stdin_reader);
    g_stdin_reader = NULL;
  }

  // Clean up probe source if still allocated (shouldn't happen, but be safe)
  if (probe_source) {
    media_source_destroy(probe_source);
    probe_source = NULL;
  }

  // Free cached webcam images and test patterns
  log_debug("[CLEANUP] About to call webcam_destroy()");
  webcam_destroy();
  log_debug("[CLEANUP] webcam_destroy() returned");

  // Stop splash animation and enforce minimum display time (even on error path)
  // But skip completely if shutting down - don't interact with splash at all during shutdown
  // The animation thread will exit on its own when it detects shutdown_is_requested()
  if (!APP_CALLBACK_BOOL(should_exit)) {
    log_debug("[CLEANUP] About to call splash_intro_done()");
    splash_intro_done();
    log_debug("[CLEANUP] splash_intro_done() returned");

    // Wait for animation thread to exit before cleanup
    log_debug("[CLEANUP] About to call splash_wait_for_animation()");
    splash_wait_for_animation();
    log_debug("[CLEANUP] splash_wait_for_animation() returned");
  } else {
    log_debug("[CLEANUP] Skipping all splash operations (shutdown in progress)");
    // During shutdown, don't interact with splash - let animation thread exit naturally
    // and don't wait for it (prevents blocking on signals)
  }

  // Stop debug sync thread before destroying log buffer to prevent use-after-free
  log_debug("[CLEANUP] About to call debug_sync_cleanup_thread()");
  debug_sync_cleanup_thread();
  log_debug("[CLEANUP] debug_sync_cleanup_thread() returned");

  // Cleanup session log buffer (used by splash screen)
  log_debug("[CLEANUP] About to call session_log_buffer_destroy()");
  session_log_buffer_destroy();
  log_debug("[CLEANUP] session_log_buffer_destroy() returned");

  // Disable keepawake (re-allow OS to sleep)
  log_debug("[CLEANUP] Disabling keepawake");
  platform_disable_keepawake();
  log_debug("[CLEANUP] keepawake disabled");

  // Write newline to separate final frame from prompt (if configured)
  log_debug("About to check print_newline_on_tty_exit");
  if (config->print_newline_on_tty_exit && terminal_is_stdout_tty()) {
    log_debug("Writing newline");
    const char newline = '\n';
    (void)platform_write_all(STDOUT_FILENO, &newline, 1);
  }

  log_set_terminal_output(false);

  return result;
}
