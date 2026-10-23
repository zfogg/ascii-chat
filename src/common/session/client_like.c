#include <ascii-chat/otel.h>
#include <ascii-chat/debug/stats.h>
#include <ascii-chat/debug/errno.h>
#include <ascii-chat/stats/runtime.h>
/**
 * @file client_like.c
 * @ingroup session
 * @brief Shared initialization and teardown for client-like modes
 */

#include <ascii-chat/tui/controller.h>
#include "session/client_like.h"
#include "session/capture.h"
#include "session/display.h"
#include "session/render.h"
#include <ascii-chat/terminal/fd/reader.h>
#include "../../client/audio.h"

#include <ascii-chat/media/source.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/tui/splash.h>
#include <ascii-chat/tui/update_banner.h>
#include <ascii-chat/tui/keyboard_help.h>
#include <ascii-chat/debug/sync.h>

#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/runtime.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/util/url.h>
#include <ascii-chat/app_callbacks.h>

#include <string.h>
#include <ascii-chat/atomic.h>

// Forward declarations (implemented below)
static bool client_like_capture_should_exit_adapter(void *user_data);
static bool client_like_display_should_exit_adapter(void *user_data);
static bool client_like_is_networked(session_client_like_kind_t kind);

static asciichat_error_t client_like_validate_mirror_webcam_selection(const session_client_like_config_t *config) {
  if (!config || config->kind != SESSION_CLIENT_LIKE_KIND_MIRROR || !terminal_is_stdout_tty() ||
      GET_OPTION(test_pattern)) {
    return ASCIICHAT_OK;
  }

  const char *media_file = GET_OPTION(media_file);
  const char *media_url = GET_OPTION(media_url);
  if ((media_file && media_file[0] != '\0') || (media_url && media_url[0] != '\0')) {
    return ASCIICHAT_OK;
  }

  webcam_device_info_t *devices = NULL;
  unsigned int device_count = 0;
  asciichat_error_t result = webcam_list_devices(&devices, &device_count);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_WEBCAM, "Unable to enumerate webcams before starting mirror mode");
  }

  unsigned int selected_index = (unsigned int)GET_OPTION(webcam_index);
  if (selected_index >= device_count) {
    if (device_count == 0) {
      result =
          SET_ERRNO(ERROR_WEBCAM, "Webcam index %u is unavailable: no webcam devices were found (run --list-webcams)",
                    selected_index);
    } else {
      result = SET_ERRNO(ERROR_WEBCAM,
                         "Webcam index %u is unavailable: %u webcam device(s) found; valid indices are 0-%u "
                         "(run --list-webcams)",
                         selected_index, device_count, device_count - 1);
    }
  } else {
    result = ASCIICHAT_OK;
  }

  webcam_free_device_list(devices);
  return result;
}

// Adapter callbacks own a config snapshot for the duration of the session.
static session_client_like_config_t g_current_config = {0};

// Module-level adapter functions for render loop access
static bool (*g_render_should_exit)(void *) = NULL;

// Module-level stdin reader for ASCII-to-video rendering (stdin render mode only)
static terminal_fd_reader_t *g_stdin_reader = NULL;

/* ============================================================================
 * Public Accessors
 * ============================================================================ */

bool (*session_client_like_get_render_should_exit(void))(void *) {
  return g_render_should_exit;
}

terminal_fd_reader_t *session_client_like_get_stdin_reader(void) {
  return g_stdin_reader;
}

/* ============================================================================
 * Exit Condition Adapters
 * ============================================================================ */

/**
 * Adapter for capture should_exit callback
 * Checks both global exit flag and mode-specific custom exit condition
 */
static bool client_like_capture_should_exit_adapter(void *user_data) {
  (void)user_data;
  if (APP_CALLBACK_BOOL(should_exit)) {
    return true;
  }
  if (g_current_config.custom_should_exit) {
    return g_current_config.custom_should_exit(g_current_config.exit_user_data);
  }
  return false;
}

/**
 * Adapter for display should_exit callback
 * Checks both global exit flag and mode-specific custom exit condition
 */
static bool client_like_display_should_exit_adapter(void *user_data) {
  (void)user_data;
  if (APP_CALLBACK_BOOL(should_exit)) {
    return true;
  }
  if (g_current_config.custom_should_exit) {
    return g_current_config.custom_should_exit(g_current_config.exit_user_data);
  }
  return false;
}

static bool client_like_is_networked(session_client_like_kind_t kind) {
  return kind == SESSION_CLIENT_LIKE_KIND_CLIENT || kind == SESSION_CLIENT_LIKE_KIND_DISCOVERY;
}

// Resources owned by the session; setup helpers borrow this state so every
// partially initialized session follows the same teardown order.
typedef struct {
  session_capture_ctx_t *capture;
  session_display_ctx_t *display;
  audio_context_t *audio_ctx;
  media_source_t *probe_source;
  session_capture_config_t capture_config;
  // Source strings borrow the published options storage.
  const char *media_url_val;
  const char *media_file_val;
  bool audio_available;
  bool stdin_render_mode;
  bool should_init_audio;
  uint64_t audio_failed_revision;
} client_like_state_t;

static void client_like_setup_terminal_logging(void) {

  // Force stderr when stdout is piped (prevents ASCII corruption in output stream)
  const char *output = GET_OPTION(render_file);
  bool should_force_stderr = terminal_should_force_stderr() || GET_OPTION(render_kitty) ||
                             strcmp(output, "--kitty") == 0 || strcmp(output, "-") == 0 || strcmp(output, "pipe:") == 0;
  log_debug("terminal_should_force_stderr()=%d", should_force_stderr);

  if (should_force_stderr) {
    // Redirect logs to stderr to prevent corruption of stdout (for pipes)
    // But keep terminal output enabled so we can see initialization errors
    log_set_force_stderr(true);
  }

  // Note: Force all logging to stderr if encoding to pipe to prevent corrupting binary video output
  // (implemented via should_force_stderr check above)
  const char *render_file_early = GET_OPTION(render_file);
  log_debug("[CLIENT_LIKE_RUN] render_file_early=%s (ptr=%p)", render_file_early ? render_file_early : "(null)",
            (void *)render_file_early);
  if (render_file_early && render_file_early[0] != '\0' &&
      (strcmp(render_file_early, "-") == 0 || strcmp(render_file_early, "pipe:") == 0)) {
    log_info("--render-file=- will output binary video to stdout, logging sent to stderr");
  }
}

static asciichat_error_t client_like_setup_keepawake(void) {

  // Validate mutual exclusivity
  bool enable_ka = GET_OPTION(enable_keepawake);
  bool disable_ka = GET_OPTION(disable_keepawake);
  log_debug("enable_keepawake=%d, disable_keepawake=%d", enable_ka, disable_ka);

  if (enable_ka && disable_ka) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "--keepawake and --no-keepawake are mutually exclusive");
  }

  // Enable keepawake unless explicitly disabled
  if (!disable_ka) {
    (void)platform_enable_keepawake();
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t client_like_create_session_display(client_like_state_t *state) {
  // Create display context early to share TTY initialization between splash and rendering
  // This prevents double TTY init which was corrupting the terminal state
  session_display_config_t display_config = {0};
  display_config.snapshot_mode = GET_OPTION(snapshot_mode);
  display_config.palette_type = GET_OPTION(palette_type);
  display_config.custom_palette = GET_OPTION(palette_custom_set) ? GET_OPTION(palette_custom) : NULL;
  display_config.color_mode = TERM_COLOR_AUTO;
  display_config.should_exit_callback = client_like_display_should_exit_adapter;
  display_config.callback_data = NULL;
  display_config.defer_render_file = true;

  log_debug("[SETUP_DISPLAY_EARLY] Creating display context (before splash)");
  state->display = session_display_create(&display_config);
  if (!state->display) {
    log_debug("[SETUP_DISPLAY_EARLY] session_display_create() returned NULL");
    return SET_ERRNO(ERROR_DISPLAY, "Failed to initialize display context");
  }
  log_debug("[SETUP_DISPLAY_EARLY] Display context created, will use for splash");

  // Register global display context for signal handlers (Ctrl+C help screen closure)
  // This makes the context available to signal handlers for the entire app lifetime
  session_display_set_global_context(state->display);
  return ASCIICHAT_OK;
}

static asciichat_error_t client_like_prepare_session_splash(const session_client_like_config_t *config,
                                                            client_like_state_t *state, bool *stop) {
  asciichat_error_t result = ASCIICHAT_OK;
  log_debug("Starting splash with display context");

  // Use the display context we just created for splash animation
  splash_intro_start(state->display);
  if (splash_is_running()) {
    log_set_terminal_output(false);
  }

  if (config->prepare_fn) {
    result = config->prepare_fn(config->run_user_data);
    if (result != ASCIICHAT_OK)
      return result;
  }

  // Detect if we're using media vs webcam (needed for splash timing)
  const char *media_url = GET_OPTION(media_url);
  const char *media_file = GET_OPTION(media_file);
  bool has_media = (media_url && strlen(media_url) > 0) || (media_file && strlen(media_file) > 0);

  // Show splash briefly for webcam, but skip sleep for media (which takes time anyway)
  if (!has_media && !GET_OPTION(snapshot_mode)) {
    APP_CALLBACK_VOID(platform_pump_events);
    APP_CALLBACK_VOID_UINT(platform_sleep_ms, 250);
  }

  // Restore stderr now that splash animation and post-splash logging are done
  // This allows logs to appear on screen again after this point
  splash_restore_stderr();

  // Discovery keeps the invitation as the only waiting UI.
  update_banner_wait_for_check();
  if (GET_OPTION(detected_mode) != MODE_DISCOVERY) {
    if (update_banner_has_update() && !GET_OPTION(snapshot_mode)) {
      bool wants_update = update_banner_show_prompt(state->display);
      if (wants_update) {
        if (update_banner_install_update()) {
          *stop = true;
          return ASCIICHAT_OK;
        }
        ui_controller_redraw();
      }
      ui_controller_redraw();
    }
  }
  return ASCIICHAT_OK;
}

static void client_like_configure_session_logging(const session_client_like_config_t *config) {
  log_debug("About to disable terminal logging (snapshot=%d, discovery=%d)", GET_OPTION(snapshot_mode),
            config->discovery != NULL);
  // For discovery mode, skip disabling logging initially - we'll handle it after role is determined
  // Discovery participants should show logs, only hosts should have logs disabled for rendering
  bool should_disable_logging = !GET_OPTION(snapshot_mode) && (config->discovery == NULL);
  if (should_disable_logging) {
    log_set_terminal_output(false);
  } else {
    log_debug("Skipping log disable (snapshot=%d or discovery=%d)", GET_OPTION(snapshot_mode),
              config->discovery != NULL);
  }
}

static void client_like_configure_url_capture(client_like_state_t *state) {
  log_info("Using network media source (webcam disabled)");
  state->capture_config.type = MEDIA_SOURCE_FILE;
  state->capture_config.path = state->media_url_val;
  state->capture_config.loop = false;
}

static void client_like_configure_file_capture(client_like_state_t *state) {
  if (strcmp(state->media_file_val, "-") == 0) {
    log_info("Using stdin for media streaming (webcam disabled)");
    state->capture_config.type = MEDIA_SOURCE_STDIN;
    state->capture_config.path = NULL;
    state->capture_config.loop = false;
  } else {
    log_info("Using media file: %s (webcam disabled)", state->media_file_val);
    state->capture_config.type = MEDIA_SOURCE_FILE;
    state->capture_config.path = state->media_file_val;
    state->capture_config.loop = GET_OPTION(media_loop);
  }
}

static uint32_t client_like_select_capture_fps(client_like_state_t *state) {
  if (GET_OPTION(fps_explicitly_set)) {
    uint32_t fps = (uint32_t)GET_OPTION(fps);
    log_info("Using user-specified FPS: %u", fps);
    return fps;
  }

  // Files, URLs, and buffered stdin expose a fixed source FPS. Webcam and test
  // sources have no fixed FPS metadata, so they use the fallback below.
  if (state->capture_config.type == MEDIA_SOURCE_FILE || state->capture_config.type == MEDIA_SOURCE_STDIN) {
    log_debug("Probing media source FPS");
    asciichat_errno_scope_t probe_scope = errno_scope_begin();
    state->probe_source = media_source_create(state->capture_config.type, state->capture_config.path);
    if (state->probe_source) {
      ASSERT_NO_ERRNO_SINCE(probe_scope);
      errno_scope_end(probe_scope, ASCIICHAT_ERRNO_HANDLED);
      double source_fps = media_source_get_video_fps(state->probe_source);
      // Keep conversion in range and never select zero after rounding.
      if (source_fps >= 0.5 && source_fps <= UINT32_MAX - 0.5) {
        uint32_t fps = (uint32_t)(source_fps + 0.5);
        log_info("Using detected source FPS: %u", fps);
        return fps;
      }
      log_warn("Media source has no usable FPS, using default 60 FPS");
    } else {
      errno_scope_end(probe_scope, ASCIICHAT_ERRNO_DISMISSED);
      log_warn("Failed to probe media source FPS, using default 60 FPS");
    }
  }
  return 60;
}

static asciichat_error_t client_like_configure_media_capture(const session_client_like_config_t *config,
                                                             client_like_state_t *state) {
  // capture_config starts zeroed with the session state.
  state->capture_config.resize_for_network = client_like_is_networked(config->kind);
  state->capture_config.should_exit_callback = client_like_capture_should_exit_adapter;
  state->capture_config.callback_data = NULL;

  // Select media source based on options (priority order)
  state->media_url_val = GET_OPTION(media_url);
  state->media_file_val = GET_OPTION(media_file);
  log_debug("media_url=%s, media_file=%s", state->media_url_val ? state->media_url_val : "(null)",
            state->media_file_val ? state->media_file_val : "(null)");

  if (state->media_url_val && strlen(state->media_url_val) > 0) {
    client_like_configure_url_capture(state);
  } else if (state->media_file_val && strlen(state->media_file_val) > 0) {
    client_like_configure_file_capture(state);
  } else if (GET_OPTION(test_pattern)) {
    // Test pattern
    log_debug("Using test pattern");
    log_info("Using test pattern");
    state->capture_config.type = MEDIA_SOURCE_TEST;
    state->capture_config.path = NULL;
    state->capture_config.loop = false;
  } else {
    // Default: Try webcam if stdout is a TTY, otherwise use test pattern
    // (webcam + piped output often fails, test pattern is reliable fallback)
    bool stdout_is_tty = terminal_is_stdout_tty();
    if (stdout_is_tty) {
      // Normal terminal mode: use webcam
      log_debug("Stdout is TTY, using local webcam");
      log_info("Using local webcam");
      state->capture_config.type = MEDIA_SOURCE_WEBCAM;
      static char webcam_index_str[32];
      safe_snprintf(webcam_index_str, sizeof(webcam_index_str), "%u", GET_OPTION(webcam_index));
      state->capture_config.path = webcam_index_str;
      log_info("Using webcam device index %u", GET_OPTION(webcam_index));
      state->capture_config.loop = false;
    } else {
      // Piped output mode: use test pattern (webcam + pipes often fails)
      log_debug("Stdout is piped, defaulting to test pattern for reliability");
      log_info("Stdout is piped - using test pattern instead of webcam");
      state->capture_config.type = MEDIA_SOURCE_TEST;
      state->capture_config.path = NULL;
      state->capture_config.loop = false;
    }
  }

  state->capture_config.target_fps = client_like_select_capture_fps(state);

  // Apply initial seek if specified
  state->capture_config.initial_seek_timestamp = GET_OPTION(media_seek_timestamp);

  // Clean up probe source (don't reuse for actual capture)
  if (state->probe_source) {
    media_source_destroy(state->probe_source);
    state->probe_source = NULL;
  }

  // Pass probed FPS to display context for correct render-file duration
  if (state->capture_config.target_fps > 0) {
    session_display_set_render_fps(state->display, state->capture_config.target_fps);
    log_debug("Set display render FPS to %u for correct video encoding duration", state->capture_config.target_fps);
  }

  asciichat_error_t render_init_result =
      session_display_init_render_file(state->display, state->capture_config.target_fps);
  if (render_init_result != ASCIICHAT_OK) {
    return render_init_result;
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t client_like_create_network_capture(const session_client_like_config_t *config,
                                                            client_like_state_t *state) {
  log_debug("Setting up network transports");

  bool networked_mode = client_like_is_networked(config->kind);

  switch (config->kind) {
  case SESSION_CLIENT_LIKE_KIND_MIRROR:
    log_debug("Mirror mode detected - will use local capture with media source");
    break;
  case SESSION_CLIENT_LIKE_KIND_CLIENT:
    log_debug("Client/Network mode detected - will use network capture without local media source");
    break;
  case SESSION_CLIENT_LIKE_KIND_DISCOVERY:
    log_debug("Discovery mode detected - discovery session will manage networking");
    break;
  }

  // ============================================================================
  // SETUP: Capture Context
  // ============================================================================

  // Check for stdin render mode: read ASCII frames from stdin, render to video
  // Only enable stdin render mode if:
  // 1. render-file is "-" (output to stdout)
  // 2. stdin is not a TTY
  // 3. --height is explicitly specified (user must opt-in to stdin render mode)
  // 4. NO media source is being used (not reading from file/URL/test-pattern)
  // This prevents accidental activation when piping to other processes or using mirror mode
  const char *render_file_opt = GET_OPTION(render_file);
  int frame_height = GET_OPTION(height);
  bool height_explicitly_set = (frame_height != OPT_HEIGHT_DEFAULT);
  bool has_media_source = (state->media_url_val && strlen(state->media_url_val) > 0) ||
                          (state->media_file_val && strlen(state->media_file_val) > 0) || GET_OPTION(test_pattern);

  // Enable audio capture if render-file is active and media source exists
  if (render_file_opt && strlen(render_file_opt) > 0 && has_media_source) {
    state->capture_config.enable_audio = true;
    log_debug("Audio capture enabled for render-file encoding");
  }

  state->stdin_render_mode =
      (render_file_opt && strcmp(render_file_opt, "-") == 0 && !terminal_is_stdin_tty() && height_explicitly_set &&
       !has_media_source && config->kind != SESSION_CLIENT_LIKE_KIND_MIRROR);

  if (state->stdin_render_mode) {
    // Stdin render mode: read ASCII frames from stdin, output video to stdout
    log_info("Stdin render mode enabled: reading ASCII frames from stdin, output to stdout");

    // Get frame dimensions (--height must be explicitly set to reach here)
    int frame_width = GET_OPTION(width);

    // Validate stdin render mode constraints (--height is already validated above since height_explicitly_set is true)
    bool width_was_explicitly_set = (frame_width != OPT_WIDTH_DEFAULT);

    if (width_was_explicitly_set) {
      return SET_ERRNO(ERROR_USAGE, "Stdin render mode does not accept --width (auto-detected from frames).\n"
                                    "Only specify: --height <rows>");
    }

    asciichat_error_t stdin_err = terminal_fd_reader_create(STDIN_FILENO, frame_height, &g_stdin_reader);
    if (stdin_err != ASCIICHAT_OK) {
      return SET_ERRNO(ERROR_MEDIA_INIT, "Failed to initialize stdin frame reader: %s",
                       asciichat_error_string(stdin_err));
    }

    // For stdin render mode, create a minimal capture context for compatibility
    state->capture = session_network_capture_create(state->capture_config.target_fps);
    if (!state->capture) {
      terminal_fd_reader_destroy(g_stdin_reader);
      g_stdin_reader = NULL;
      return SET_ERRNO(ERROR_MEDIA_INIT, "Failed to initialize capture context for stdin rendering");
    }

    log_info("stdin render mode: reading %d-line frames from stdin, auto-detecting width", frame_height);
  }

  // Choose capture type based on mode determined earlier:
  // - Mirror mode: needs to capture local media (webcam, file, test pattern)
  // - Network modes (client/discovery): receive frames from network, no local capture
  // Discovery peers may use a local media source (for example the synthetic
  // test pattern used by headless WebRTC tests) as their outgoing stream.
  // Keep the network-only capture for ordinary client mode, but do not discard
  // an explicitly requested source before the discovery transport is active.
  bool discovery_has_local_source = config->kind == SESSION_CLIENT_LIKE_KIND_DISCOVERY;
  if (!state->stdin_render_mode && networked_mode && !discovery_has_local_source) {
    // Network mode: create minimal capture context without media source
    log_debug("Network mode detected - using network capture (no local media source)");
    uint64_t net_cap_start = time_get_ns();
    state->capture = session_network_capture_create(state->capture_config.target_fps);
    double net_cap_ms = time_ns_to_ms(time_elapsed_ns(net_cap_start, time_get_ns()));
    log_debug("★ INIT_CHECKPOINT: Network capture created in %.1f ms", net_cap_ms);
    if (!state->capture) {
      return SET_ERRNO(ERROR_MEDIA_INIT, "Failed to initialize network capture context");
    }
    log_debug("Network capture FPS set to %u", state->capture_config.target_fps);
  }
  return ASCIICHAT_OK;
}

static void client_like_prepare_session_audio(client_like_state_t *state) {
  // Skip audio for immediate snapshots
  state->should_init_audio = GET_OPTION(audio_enabled);
  if (GET_OPTION(snapshot_mode) && GET_OPTION(snapshot_delay) == 0.0) {
    state->should_init_audio = false;
    log_debug("Skipping audio initialization for immediate snapshot");
  }

  // Pass stdin_reader to display if in stdin render mode
  if (state->stdin_render_mode && g_stdin_reader) {
    session_display_set_stdin_reader(state->display, g_stdin_reader);
    log_debug("stdin_reader passed to display context");
  }
}

// Return true when shutdown was requested before capture startup.
static bool client_like_finish_session_splash(const session_client_like_config_t *config) {
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
    return true;
  }

  // Wait for splash animation to finish before proceeding to render loop
  // (splash_anim thread uses the display, must not be disrupted before cleanup)
  // But skip for immediate snapshots (snapshot-delay=0) which need to output first frame immediately
  // For discovery mode, keep the splash running - it stays visible while waiting for peers
  if (config->discovery == NULL) {
    splash_intro_done();
    if (!(GET_OPTION(snapshot_mode) && GET_OPTION(snapshot_delay) == 0.0)) {
      splash_wait_for_animation();
    }
  } else {
    log_debug("[SETUP_SPLASH] Discovery mode: keeping splash visible while waiting for peers");
  }

  return false;
}

static asciichat_error_t client_like_create_local_capture(client_like_state_t *state) {
  log_debug("[MIRROR_CHECK] stdin_render_mode=%d, capture=%p", state->stdin_render_mode, (void *)state->capture);
  if (!state->stdin_render_mode && !state->capture) {
    // Mirror mode: create capture context with local media source (NOW, after splash finishes)
    log_debug("Mirror mode detected - creating mirror capture after splash screen");
    uint64_t mirror_cap_start = time_get_ns();
    state->capture = session_mirror_capture_create(&state->capture_config);
    double mirror_cap_ms = time_ns_to_ms(time_elapsed_ns(mirror_cap_start, time_get_ns()));
    log_debug("★ INIT_CHECKPOINT: Mirror capture created in %.1f ms (after splash)", mirror_cap_ms);
    if (!state->capture) {
      return SET_ERRNO(ERROR_MEDIA_INIT, "Failed to initialize mirror capture source");
    }
  }
  return ASCIICHAT_OK;
}

static void *client_like_update_mirror_audio(media_source_t *source, void *data) {
  client_like_state_t *state = data;
  bool file_audio = media_source_has_audio(source);
  bool needed = GET_OPTION(audio_enabled) && (file_audio || GET_OPTION(waveform) || GET_OPTION(fft) ||
                                              GET_OPTION(test_audio) || GET_OPTION(render_file)[0]);
  if (!needed) {
    if (state->audio_ctx && state->audio_ctx->running) {
      if (audio_stop_duplex(state->audio_ctx) != ASCIICHAT_OK)
        LOG_ERRNO_IF_SET("Cannot stop idle mirror audio");
      else
        log_info("Mirror audio idle");
    }
    return state->audio_ctx;
  }
  if (state->audio_ctx && state->audio_ctx->running)
    return state->audio_ctx;
  uint64_t revision = options_revision();
  if (state->audio_failed_revision == revision + 1)
    return state->audio_ctx;
  if (!state->audio_ctx) {
    state->audio_ctx = SAFE_CALLOC(1, sizeof(*state->audio_ctx), audio_context_t *);
    if (audio_init(state->audio_ctx) != ASCIICHAT_OK) {
      LOG_ERRNO_IF_SET("Cannot initialize mirror audio");
      SAFE_FREE(state->audio_ctx);
      state->audio_failed_revision = revision + 1;
      return NULL;
    }
  }
  state->audio_ctx->capture_media_source =
      file_audio && GET_OPTION(audio_capture_source) != AUDIO_CAPTURE_SOURCE_MIC ? source : NULL;
  state->audio_ctx->monitor_local_media = true;
  state->audio_ctx->playback_only = !audio_should_enable_microphone(GET_OPTION(audio_capture_source), file_audio);
  media_source_set_audio_context(source, state->audio_ctx);
  if (audio_start_duplex(state->audio_ctx) != ASCIICHAT_OK) {
    LOG_ERRNO_IF_SET("Cannot start mirror audio");
    media_source_set_audio_context(source, NULL);
    // Keep the initialized context alive for input-thread borrowers and retries.
    state->audio_failed_revision = revision + 1;
    state->audio_available = false;
    return state->audio_ctx;
  }
  state->audio_available = true;
  state->audio_failed_revision = 0;
  log_info("Mirror audio started on demand");
  return state->audio_ctx;
}

static void client_like_start_session_audio(const session_client_like_config_t *config, client_like_state_t *state) {
  if (state->should_init_audio && config->kind == SESSION_CLIENT_LIKE_KIND_MIRROR) {
    session_capture_set_audio_updater(state->capture, client_like_update_mirror_audio, state);
    media_source_t *source = session_capture_get_media_source(state->capture);
    session_capture_set_audio_context(state->capture, client_like_update_mirror_audio(source, state));
  }
}

static asciichat_error_t client_like_run_session_attempts(const session_client_like_config_t *config,
                                                          client_like_state_t *state) {
  asciichat_error_t result = ASCIICHAT_OK;
  // Connection/attempt loop - wraps run_fn with reconnection logic
  int attempt = 0;
  int max_attempts = config->max_reconnect_attempts;

  log_debug("[CLIENT_LIKE_LOOP] Starting connection loop, max_attempts=%d", max_attempts);

  while (true) {
    attempt++;
    if (attempt > 1) {
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_RECONNECTS, 1);
      ascii_otel_metric(ASCII_OTEL_METRIC_RECONNECT, 1);
    }

    asciichat_errno_scope_t attempt_scope = errno_scope_begin();
    {
      ASCII_OTEL_SCOPE(SESSION_ATTEMPT);
      ASCII_OTEL_ATTR_INT("session.attempt", attempt);
      ASCII_OTEL_ATTR_STRING("session.attempt.kind", attempt > 1 ? "reconnect" : "initial");
      result = config->run_fn(state->capture, state->display, config->run_user_data);
      ASCII_OTEL_ATTR_INT("session.result", result);
#ifdef ASCIICHAT_WITH_OTEL
      if (result != ASCIICHAT_OK)
        ascii_otel_error("Session attempt failed");
#endif
    }

    // Exit immediately if run_fn succeeded
    if (result == ASCIICHAT_OK) {
      ASSERT_NO_ERRNO_SINCE(attempt_scope);
      errno_scope_end(attempt_scope, ASCIICHAT_ERRNO_HANDLED);
      // Connection succeeded - splash screen cleanup now happens when first frame renders
      // This ensures the splash stays visible during TCP/WebSocket/datachannel connection attempts

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

    LOG_ERRNO_IF_SET("Connection attempt failed; retrying");
    errno_scope_end(attempt_scope, ASCIICHAT_ERRNO_HANDLED);

    // Reconnection will happen - show splash screen during reconnection attempt
    // Reset first_frame flag so splash cleanup runs on next successful connection
    session_display_reset_first_frame(state->display);

    // splash_intro_start() handles all the logic:
    // - If splash should display: shows splash and disables console logging
    // - If splash shouldn't display: restores stderr and returns without changing logging state
    splash_intro_start(state->display);

    // Log reconnection message
    if (max_attempts == -1) {
      log_warn("Connection failed, retrying...");
    } else if (max_attempts > 0) {
      log_warn("Connection failed, retrying (attempt %d/%d)...", attempt + 1, max_attempts);
    }

    // Apply reconnection delay if configured
    // Check APP_CALLBACK_BOOL(should_exit) frequently during sleep so SIGTERM can interrupt reconnection attempts
    unsigned int retry_delay_ms = config->reconnect_delay_ms;
    if (result == ERROR_RATE_LIMITED || result == ERROR_SESSION_FULL) {
      unsigned int exponent = attempt > 5 ? 5 : (unsigned int)attempt;
      unsigned int rejection_delay_ms = 1000U << exponent;
      if (retry_delay_ms < rejection_delay_ms)
        retry_delay_ms = rejection_delay_ms;
    }
    if (retry_delay_ms > 0) {
      ASCII_OTEL_SCOPE(RECONNECT_WAIT);
      ASCII_OTEL_ATTR_INT("reconnect.delay_ms", retry_delay_ms);
      unsigned int remaining_ms = retry_delay_ms;
      const unsigned int check_interval_ms = 100; // Check exit flag every 100ms

      while (remaining_ms > 0 && !APP_CALLBACK_BOOL(should_exit)) {
        unsigned int sleep_ms = (remaining_ms < check_interval_ms) ? remaining_ms : check_interval_ms;
        APP_CALLBACK_VOID(platform_pump_events);
        APP_CALLBACK_VOID_UINT(platform_sleep_ms, sleep_ms);
        remaining_ms -= sleep_ms;
      }
    }

    // Continue loop to retry
  }
  return result;
}

static asciichat_error_t client_like_cleanup_session(const session_client_like_config_t *config,
                                                     client_like_state_t *state, asciichat_error_t result) {
  ASCII_OTEL_SCOPE(SESSION_CLEANUP);
  // Preparation can fail before the normal update check wait above.
  update_banner_wait_for_check();
  // Stop the terminal writer before restoring output or destroying the display.
  splash_intro_done();
  splash_wait_for_animation();
  // Re-enable terminal output for shutdown logs
  log_set_terminal_output(true);

  g_current_config = (session_client_like_config_t){0};
  g_render_should_exit = NULL;

#ifndef NDEBUG
  // Join the registry inspector before freeing the session's synchronization objects.
  debug_sync_cleanup_thread();
  debug_errno_cleanup_thread();
  debug_stats_cleanup_thread();
#endif

  // Stop audio thread before destroying audio context to prevent use-after-free
  // The audio worker thread may still be logging when we destroy the buffer
  APP_CALLBACK_VOID(audio_stop_thread);

  // Stop streams before releasing the local media source.
  if (state->audio_ctx) {
    session_capture_set_audio_context(state->capture, NULL);
    audio_stop_duplex(state->audio_ctx);
    audio_destroy(state->audio_ctx);
    SAFE_FREE(state->audio_ctx);
    state->audio_ctx = NULL;
  }

  // Destroy display (check global context to see if discovery mode already destroyed it)
  // Discovery participant mode may destroy the display early and clear the global context
  session_display_ctx_t *current_global = session_display_get_global_context();
  if (state->display && state->display == current_global) {
    // Display is still valid (not destroyed by discovery mode)
    asciichat_error_t finish_err = session_display_finish_render_file(state->display);
    if (result == ASCIICHAT_OK)
      result = finish_err;
    session_display_destroy(state->display);
    state->display = NULL;
  } else if (state->display && state->display != current_global) {
    // Display was already destroyed (by discovery participant mode)
    // Just NULL it out, don't call destroy again
    log_debug("[CLEANUP] Display was already destroyed by discovery mode, skipping destroy");
    state->display = NULL;
  }

  // Destroy capture
  if (state->capture) {
    session_capture_destroy(state->capture);
    state->capture = NULL;
  }

  // Destroy stdin reader if active (stdin render mode)
  if (g_stdin_reader) {
    terminal_fd_reader_destroy(g_stdin_reader);
    g_stdin_reader = NULL;
  }

  // Clean up probe source if still allocated (shouldn't happen, but be safe)
  if (state->probe_source) {
    media_source_destroy(state->probe_source);
    state->probe_source = NULL;
  }

  // Disable keepawake (re-allow OS to sleep)
  log_debug("[CLEANUP] Disabling keepawake");
  platform_disable_keepawake();
  log_debug("[CLEANUP] keepawake disabled");

  // Write newline to separate final frame from prompt (if configured)
  log_debug("About to check print_newline_on_tty_exit");
  if (config->print_newline_on_tty_exit && terminal_is_stdout_tty()) {
    log_debug("Writing newline");
    const char newline = '\n';
    (void)ui_controller_write(STDOUT_FILENO, &newline, 1);
  }

  log_set_terminal_output(false);

  return result;
}

asciichat_error_t session_client_like_run(const session_client_like_config_t *config) {
  ASCII_OTEL_SCOPE(SESSION_CLIENT_LIKE_RUN);
  if (!config || !config->run_fn) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "config or run_fn is NULL");
  }

  g_current_config = *config;
  g_render_should_exit = client_like_display_should_exit_adapter;
  client_like_state_t state = {0};
  asciichat_error_t result = ASCIICHAT_OK;
  {
    ASCII_OTEL_SCOPE(SESSION_INITIALIZE);
    client_like_setup_terminal_logging();
    result = client_like_setup_keepawake();
    if (result != ASCIICHAT_OK) {
      goto cleanup;
    }
    result = client_like_validate_mirror_webcam_selection(config);
    if (result != ASCIICHAT_OK) {
      goto cleanup;
    }
    result = client_like_create_session_display(&state);
    if (result != ASCIICHAT_OK) {
      goto cleanup;
    }
    // An accepted update prompt ends the session successfully before media setup.
    bool stop = false;
    result = client_like_prepare_session_splash(config, &state, &stop);
    if (result != ASCIICHAT_OK || stop) {
      goto cleanup;
    }
    client_like_configure_session_logging(config);
    result = client_like_configure_media_capture(config, &state);
    if (result != ASCIICHAT_OK) {
      goto cleanup;
    }
    result = client_like_create_network_capture(config, &state);
    if (result != ASCIICHAT_OK) {
      goto cleanup;
    }
    client_like_prepare_session_audio(&state);
    if (client_like_finish_session_splash(config)) {
      goto cleanup;
    }
    result = client_like_create_local_capture(&state);
    if (result != ASCIICHAT_OK) {
      goto cleanup;
    }
    client_like_start_session_audio(config, &state);
    log_debug("[MARKER] Audio setup complete at line 614");
  }
  log_debug("[SETUP_COMPLETE] All setup complete, about to start connection loop");
  result = client_like_run_session_attempts(config, &state);

cleanup:
  return client_like_cleanup_session(config, &state, result);
}
