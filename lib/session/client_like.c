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
#include <ascii-chat/network/tcp/client.h>
#include <ascii-chat/network/websocket/client.h>
#include <ascii-chat/util/url.h>

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

// External exit function from src/main.c
extern bool should_exit(void);

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
  // STEP 1: SETUP: Network Transports (TCP/WebSocket) - BEFORE capture context
  // ============================================================================
  // CRITICAL: Create network clients FIRST, before is_network_mode check.
  // This ensures capture context is initialized correctly based on actual
  // clients created (not config parameters which are NULL for client mode).
  //
  // EXECUTION SEQUENCE:
  // 1. Create network clients (this block) ← YOU ARE HERE
  // 2. Check is_network_mode based on created clients
  // 3. Create appropriate capture context (network vs mirror)

  log_info("[STEP 1] === BEGIN: Network Transports Setup ===");
  log_debug("session_client_like_run(): Setting up network transports");
  log_debug("Checking server address and determining transport type");

  // Parse server address to determine TCP vs WebSocket
  const char *server_address = GET_OPTION(address);
  bool is_websocket = server_address && url_is_websocket(server_address);

  log_debug("[STEP 1] Network transport decision: is_websocket=%d, server_address=%s",
            is_websocket, server_address ? server_address : "(null)");

  if (is_websocket) {
    log_debug("[STEP 1] WebSocket URL detected: %s", server_address);
    log_debug("[STEP 1] Creating WebSocket client...");
    g_websocket_client = websocket_client_create();
    if (!g_websocket_client) {
      log_error("[STEP 1] Failed to create WebSocket client");
      result = ERROR_NETWORK;
      goto cleanup;
    }
    log_info("[STEP 1] ✓ WebSocket client created (g_websocket_client=%p)", (void *)g_websocket_client);
  } else if (server_address && strlen(server_address) > 0) {
    log_debug("[STEP 1] TCP client will be created for server: %s:%d", server_address, GET_OPTION(port));
    log_debug("[STEP 1] Creating TCP client...");
    g_tcp_client = tcp_client_create();
    if (!g_tcp_client) {
      log_error("[STEP 1] Failed to create TCP client");
      result = ERROR_NETWORK;
      goto cleanup;
    }
    log_info("[STEP 1] ✓ TCP client created (g_tcp_client=%p)", (void *)g_tcp_client);
  }

  log_info("[STEP 1] === END: Network Transports Setup === (clients ready)");

  // ============================================================================
  // STEP 2: SETUP: Capture Context (based on network clients created in STEP 1)
  // ============================================================================
  // EXECUTION SEQUENCE:
  // 1. ✓ DONE: Create network clients (see STEP 1 above)
  // 2. Check is_network_mode based on created clients ← YOU ARE HERE
  // 3. Create appropriate capture context (network vs mirror)
  //
  // CRITICAL: Check ACTUAL global clients created above, not config parameters
  // (config->tcp_client/websocket_client are always NULL for client mode)

  log_info("[STEP 2] === BEGIN: Capture Context Setup ===");

  // Choose capture type based on mode:
  // - Mirror mode: needs to capture local media (webcam, file, test pattern)
  // - Network modes (client/discovery): receive frames from network, no local capture
  bool is_network_mode = (g_tcp_client != NULL || g_websocket_client != NULL);

  log_info("[STEP 2] *** VALIDATION POINT 1: After network client creation ***");
  log_info("[STEP 2] Checking actual global network clients:");
  log_info("[STEP 2]   g_tcp_client=%p (from STEP 1 creation)", (void *)g_tcp_client);
  log_info("[STEP 2]   g_websocket_client=%p (from STEP 1 creation)", (void *)g_websocket_client);
  log_info("[STEP 2]   is_network_mode=%d (based on ACTUAL globals, NOT config)", is_network_mode);

  if (is_network_mode) {
    // Network mode: create minimal capture context without media source
    log_info("[STEP 2] ✓ RESULT: is_network_mode=TRUE → Using NETWORK capture");
    log_debug("[STEP 2] Network mode detected - using network capture (no local media source)");
    int fps = GET_OPTION(fps);
    capture = session_network_capture_create((uint32_t)(fps > 0 ? fps : 60));
    if (!capture) {
      log_fatal("[STEP 2] Failed to initialize network capture context");
      result = ERROR_MEDIA_INIT;
      goto cleanup;
    }
    log_debug("[STEP 2] Network capture context created successfully");
    if (fps > 0) {
      log_debug("[STEP 2] Network capture FPS set to %d from options", fps);
    }
  } else {
    // Mirror mode: create capture context with local media source
    log_info("[STEP 2] ✓ RESULT: is_network_mode=FALSE → Using MIRROR capture");
    log_debug("[STEP 2] Mirror mode detected - using mirror capture with local media source");
    capture = session_mirror_capture_create(&capture_config);
    if (!capture) {
      log_fatal("[STEP 2] Failed to initialize mirror capture source");
      result = ERROR_MEDIA_INIT;
      goto cleanup;
    }
    log_debug("[STEP 2] Mirror capture context created successfully");
  }

  log_info("[STEP 2] === END: Capture Context Setup === (correct type created)");

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
  // RUN: Mode-Specific Main Loop with Reconnection
  // ============================================================================

  // Connection/attempt loop - wraps run_fn with reconnection logic
  int attempt = 0;
  int max_attempts = config->max_reconnect_attempts;

  while (true) {
    attempt++;

    log_debug("About to call config->run_fn() (attempt %d)", attempt);
    result = config->run_fn(capture, display, config->run_user_data);
    log_debug("config->run_fn() returned with result=%d", result);

    // Exit immediately if run_fn succeeded
    if (result == ASCIICHAT_OK) {
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
    if (!should_retry || should_exit()) {
      break;
    }

    // Log reconnection message
    if (max_attempts == -1) {
      log_info("Connection failed, retrying...");
    } else if (max_attempts > 0) {
      log_info("Connection failed, retrying (attempt %d/%d)...", attempt + 1, max_attempts);
    }

    // Apply reconnection delay if configured
    if (config->reconnect_delay_ms > 0) {
      platform_sleep_ms(config->reconnect_delay_ms);
    }

    // Continue loop to retry
  }

  // ============================================================================
  // CLEANUP (always runs, even on error)
  // ============================================================================

cleanup:
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
