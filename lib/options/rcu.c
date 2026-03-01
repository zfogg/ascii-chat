/**
 * @file options_state.c
 * @brief 🔒 RCU-based thread-safe options state implementation
 */

#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/schema.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/process.h>

#include <ascii-chat/atomic.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <pthread.h>
#endif

// ============================================================================
// RCU State: Global Atomic Pointer
// ============================================================================

/**
 * @brief Global atomic pointer to current options
 *
 * This is the heart of the RCU pattern:
 * - Readers use atomic_ptr_load (lock-free, fast)
 * - Writers use atomic_ptr_exchange (serialized with mutex)
 * - Memory ordering: sequentially consistent for proper visibility
 */
static _Atomic(void *) g_options = NULL;

/**
 * @brief Mutex for serializing writers
 *
 * Multiple writers need serialization to avoid lost updates:
 * - Writer A reads current options
 * - Writer B reads current options (same as A)
 * - Writer A modifies and swaps
 * - Writer B modifies and swaps (loses A's changes!)
 *
 * Mutex ensures writers see each other's changes.
 */
static mutex_t g_options_write_mutex;

/**
 * @brief Lifecycle for options initialization (handles TOCTOU race prevention)
 */
static lifecycle_t g_options_lifecycle = LIFECYCLE_INIT;
// Track the PID to detect fork() - after fork, child needs to reinitialize
static pid_t g_init_pid = -1;

/**
 * @brief Static default options fallback - returned when options not yet initialized or destroyed
 *
 * This static struct provides a safe fallback that:
 * 1. Outlives the lifetime of any dynamically allocated options structs
 * 2. Never gets freed (static memory lifetime matches program lifetime)
 * 3. Ensures atexit handlers can safely call GET_OPTION() during cleanup
 * 4. Ensures early startup code (before options_state_init) has sensible defaults
 * 5. Ensures code after options_state_destroy() continues working with fallback defaults
 *
 * All non-zero defaults are explicitly initialized from OPT_*_DEFAULT constants
 * to match the defaults created by options_t_new() function. This prevents
 * issues like division-by-zero, invalid enum values, or NULL pointer dereferences
 * when options haven't been properly initialized yet.
 *
 * Memory: Static const storage - allocated once at startup, never freed
 * Lifetime: Program lifetime (outlives all dynamically allocated options)
 * Thread-safe: Read-only after initialization, no synchronization needed
 *
 * @see options_get() - Returns this struct as fallback when g_options is NULL
 * @see options_state_init() - Initializes g_options, this becomes fallback
 * @see options_state_destroy() - Clears g_options, this becomes fallback again
 */
static const options_t g_default_options = (options_t){
    // ========================================================================
    // Mode Detection (auto-detected, default here is uninitialized)
    // ========================================================================
    .detected_mode = MODE_INVALID,

    // ========================================================================
    // Binary-Level Options (parsed before mode selection)
    // ========================================================================
    .help = OPT_HELP_DEFAULT,
    .version = OPT_VERSION_DEFAULT,
    .config_file = OPT_STRING_EMPTY_DEFAULT,

    // ========================================================================
    // Terminal Dimensions
    // ========================================================================
    .width = OPT_WIDTH_DEFAULT,
    .height = OPT_HEIGHT_DEFAULT,
    .auto_width = OPT_AUTO_WIDTH_DEFAULT,
    .auto_height = OPT_AUTO_HEIGHT_DEFAULT,

    // ========================================================================
    // Network Options
    // ========================================================================
    .address = OPT_ADDRESS_DEFAULT,
    .address6 = OPT_ADDRESS6_DEFAULT,
    .port = OPT_PORT_INT_DEFAULT,
    .websocket_port = OPT_WEBSOCKET_PORT_SERVER_DEFAULT,
    .max_clients = OPT_MAX_CLIENTS_DEFAULT,
    .session_string = OPT_STRING_EMPTY_DEFAULT,

    // ========================================================================
    // Discovery Service (ACDS) Options
    // ========================================================================
    .discovery = OPT_ACDS_DEFAULT,
    .discovery_expose_ip = OPT_ACDS_EXPOSE_IP_DEFAULT,
    .discovery_insecure = OPT_ACDS_INSECURE_DEFAULT,
    .discovery_port = OPT_ACDS_PORT_INT_DEFAULT,
    .discovery_server = OPT_STRING_EMPTY_DEFAULT,
    .discovery_service_key = OPT_STRING_EMPTY_DEFAULT,
    .discovery_database_path = OPT_STRING_EMPTY_DEFAULT,
    .webrtc = OPT_WEBRTC_DEFAULT,

    // ========================================================================
    // LAN Discovery & WebRTC Options
    // ========================================================================
    .lan_discovery = OPT_LAN_DISCOVERY_DEFAULT,
    .no_mdns_advertise = OPT_NO_MDNS_ADVERTISE_DEFAULT,
    .enable_upnp = OPT_ENABLE_UPNP_DEFAULT,

    // ========================================================================
    // WebRTC Connection Strategy Options
    // ========================================================================
    .prefer_webrtc = OPT_PREFER_WEBRTC_DEFAULT,
    .no_webrtc = OPT_NO_WEBRTC_DEFAULT,
    .webrtc_skip_stun = OPT_WEBRTC_SKIP_STUN_DEFAULT,
    .webrtc_disable_turn = OPT_WEBRTC_DISABLE_TURN_DEFAULT,
    .webrtc_skip_host = OPT_WEBRTC_SKIP_HOST_DEFAULT,
    .webrtc_ice_timeout_ms = OPT_WEBRTC_ICE_TIMEOUT_MS_DEFAULT,
    .webrtc_reconnect_attempts = OPT_WEBRTC_RECONNECT_ATTEMPTS_DEFAULT,

    // ========================================================================
    // WebRTC Connectivity Options (ACDS mode)
    // ========================================================================
    .stun_servers = OPT_STUN_SERVERS_DEFAULT,
    .turn_servers = OPT_TURN_SERVERS_DEFAULT,
    .turn_username = OPT_TURN_USERNAME_DEFAULT,
    .turn_credential = OPT_TURN_CREDENTIAL_DEFAULT,
    .turn_secret = OPT_STRING_EMPTY_DEFAULT,

    // ========================================================================
    // Encryption & Security Options
    // ========================================================================
    .encrypt_enabled = OPT_ENCRYPT_ENABLED_DEFAULT,
    .no_encrypt = OPT_NO_ENCRYPT_DEFAULT,
    .no_auth = OPT_NO_AUTH_DEFAULT,
    .encrypt_key = OPT_STRING_EMPTY_DEFAULT,
    .encrypt_keyfile = OPT_STRING_EMPTY_DEFAULT,
    .password = OPT_STRING_EMPTY_DEFAULT,
    .server_key = OPT_STRING_EMPTY_DEFAULT,
    .client_keys = OPT_STRING_EMPTY_DEFAULT,
    .require_server_identity = OPT_REQUIRE_SERVER_IDENTITY_DEFAULT,
    .require_client_identity = OPT_REQUIRE_CLIENT_IDENTITY_DEFAULT,
    .require_server_verify = OPT_REQUIRE_SERVER_VERIFY_DEFAULT,
    .require_client_verify = OPT_REQUIRE_CLIENT_VERIFY_DEFAULT,

    // ========================================================================
    // Media Options
    // ========================================================================
    .media_file = OPT_STRING_EMPTY_DEFAULT,
    .media_url = OPT_STRING_EMPTY_DEFAULT,
    .media_loop = OPT_MEDIA_LOOP_DEFAULT,
    .media_from_stdin = OPT_MEDIA_FROM_STDIN_DEFAULT,
    .media_seek_timestamp = OPT_MEDIA_SEEK_TIMESTAMP_DEFAULT,
    .pause = OPT_PAUSE_DEFAULT,
    .yt_dlp_options = OPT_STRING_EMPTY_DEFAULT,

    // ========================================================================
    // Webcam Options
    // ========================================================================
    .webcam_index = OPT_WEBCAM_INDEX_DEFAULT,
    .test_pattern = OPT_TEST_PATTERN_DEFAULT,
    .no_audio_mixer = OPT_NO_AUDIO_MIXER_DEFAULT,
    .stretch = OPT_STRETCH_DEFAULT,
    .flip_x = OPT_FLIP_X_DEFAULT,
    .flip_y = OPT_FLIP_Y_DEFAULT,

    // ========================================================================
    // Display Options
    // ========================================================================
    .color = OPT_COLOR_DEFAULT,
    .color_mode = OPT_COLOR_MODE_DEFAULT,
    .color_filter = OPT_COLOR_FILTER_DEFAULT,
    .color_scheme_name = OPT_COLOR_SCHEME_NAME_DEFAULT,
    .render_mode = OPT_RENDER_MODE_DEFAULT,
    .show_capabilities = OPT_SHOW_CAPABILITIES_DEFAULT,
    .fps = OPT_FPS_DEFAULT,
    .palette_type = OPT_PALETTE_TYPE_DEFAULT,
    .palette_custom = OPT_STRING_EMPTY_DEFAULT,
    .palette_custom_set = OPT_PALETTE_CUSTOM_SET_DEFAULT,

    // ========================================================================
    // Compression Options
    // ========================================================================
    .compression_level = OPT_COMPRESSION_LEVEL_DEFAULT,
    .no_compress = OPT_NO_COMPRESS_DEFAULT,

    // ========================================================================
    // Audio Options
    // ========================================================================
    .audio_enabled = OPT_AUDIO_ENABLED_DEFAULT,
    .audio_source = OPT_AUDIO_SOURCE_DEFAULT,
    .microphone_index = OPT_MICROPHONE_INDEX_DEFAULT,
    .speakers_index = OPT_SPEAKERS_INDEX_DEFAULT,
    .microphone_sensitivity = OPT_MICROPHONE_SENSITIVITY_DEFAULT,
    .speakers_volume = OPT_SPEAKERS_VOLUME_DEFAULT,
    .audio_analysis_enabled = OPT_AUDIO_ANALYSIS_ENABLED_DEFAULT,
    .audio_no_playback = OPT_AUDIO_NO_PLAYBACK_DEFAULT,
    .encode_audio = OPT_ENCODE_AUDIO_DEFAULT,

    // ========================================================================
    // Terminal & Output Options (consolidated)
    // ========================================================================
    .force_utf8 = OPT_FORCE_UTF8_DEFAULT,
    .quiet = OPT_QUIET_DEFAULT,
    .verbose_level = OPT_VERBOSE_LEVEL_DEFAULT,
    .snapshot_mode = OPT_SNAPSHOT_MODE_DEFAULT,
    .snapshot_delay = OPT_SNAPSHOT_DELAY_DEFAULT,
    .matrix_rain = OPT_MATRIX_RAIN_DEFAULT,
    .strip_ansi = OPT_STRIP_ANSI_DEFAULT,
    .log_file = OPT_STRING_EMPTY_DEFAULT,
    .log_level = OPT_LOG_LEVEL_DEFAULT,
    .grep_pattern = OPT_GREP_PATTERN_DEFAULT,
    .json = OPT_JSON_DEFAULT,
    .log_template = OPT_LOG_TEMPLATE_DEFAULT,
    .log_format_console_only = OPT_LOG_FORMAT_CONSOLE_DEFAULT,
    .enable_keepawake = OPT_ENABLE_KEEPAWAKE_DEFAULT,
    .disable_keepawake = OPT_DISABLE_KEEPAWAKE_DEFAULT,

    // ========================================================================
    // UI Screen Options
    // ========================================================================
    .splash_screen = OPT_SPLASH_DEFAULT,
    .status_screen = OPT_STATUS_SCREEN_DEFAULT,

    // ========================================================================
    // Remaining Options (zero-initialized by default)
    // ========================================================================
    .reconnect_attempts = OPT_RECONNECT_ATTEMPTS_DEFAULT,
    .splash_screen_explicitly_set = OPT_SPLASH_SCREEN_EXPLICITLY_SET_DEFAULT,
    .status_screen_explicitly_set = OPT_STATUS_SCREEN_EXPLICITLY_SET_DEFAULT,
    .no_check_update = OPT_NO_CHECK_UPDATE_DEFAULT,

    // All remaining array fields (identity_keys, num_identity_keys, etc.)
    // are zero-initialized (empty strings, NULL pointers, 0 values, etc.)
    // which is appropriate for their types.
};

// ============================================================================
// Memory Reclamation Strategy
// ============================================================================

/**
 * @brief Deferred free list for old options structs
 *
 * We can't immediately free old structs because readers might still be using them.
 * Simple strategy: Keep old pointers in a list, free them at shutdown.
 *
 * **Future improvements:**
 * - Hazard pointers: Track which structs readers are using
 * - Epoch-based reclamation: Free after grace period
 * - Reference counting: Free when no readers
 *
 * **Current approach:** Never free until shutdown (acceptable for rare updates)
 */
#define MAX_DEFERRED_FREES 64
static options_t *g_deferred_frees[MAX_DEFERRED_FREES];
static size_t g_deferred_free_count = 0;
static mutex_t g_deferred_free_mutex;

/**
 * @brief Add old options struct to deferred free list
 *
 * @param old_opts Old options struct to free later
 */
static void deferred_free_add(options_t *old_opts) {
  if (!old_opts) {
    return;
  }

  mutex_lock(&g_deferred_free_mutex);

  if (g_deferred_free_count < MAX_DEFERRED_FREES) {
    g_deferred_frees[g_deferred_free_count++] = old_opts;
    log_debug("Added options struct %p to deferred free list (count=%zu)", (void *)old_opts, g_deferred_free_count);
  } else {
    // List full - free immediately (risky but unlikely with infrequent updates)
    log_warn("Deferred free list full (%d entries), freeing options struct %p immediately", MAX_DEFERRED_FREES,
             (void *)old_opts);
    SAFE_FREE(old_opts);
  }

  mutex_unlock(&g_deferred_free_mutex);
}

/**
 * @brief Free all deferred options structs
 *
 * Called at shutdown when no readers are active.
 */
static void deferred_free_all(void) {
  mutex_lock(&g_deferred_free_mutex);

  log_debug("Freeing %zu deferred options structs", g_deferred_free_count);

  for (size_t i = 0; i < g_deferred_free_count; i++) {
    SAFE_FREE(g_deferred_frees[i]);
  }

  g_deferred_free_count = 0;
  mutex_unlock(&g_deferred_free_mutex);
}

// ============================================================================
// Fork Handler Registration
// ============================================================================

#ifndef _WIN32
/**
 * @brief Handler called in child process after fork()
 *
 * Resets the static mutex state so the child can safely use it.
 * This is called automatically by pthread after fork() in the child process.
 */
static void options_rcu_atfork_child(void) {
  /* Reset the lifecycle state to allow reinitialization after fork */
  /* This is safe because lifecycle_t is just an atomic int, can be reset directly */
  atomic_store_int(&g_options_lifecycle.state, LIFECYCLE_UNINITIALIZED);

  /* Reset the atomic options pointer to avoid dangling references */
  /* The parent's allocated memory is not accessible/valid in the child */
  atomic_ptr_store(&g_options, NULL);

  /* Don't reset g_init_pid - we'll detect the fork in options_state_init */
}

/**
 * @brief Constructor to initialize fork handlers at startup
 *
 * This is called before main() by the linker, ensuring fork handlers are
 * registered early.
 */
__attribute__((constructor)) static void register_fork_handlers_constructor(void) {
  // Register the child handler that will be called after fork() in the child process
  pthread_atfork(NULL, NULL, options_rcu_atfork_child);
}
#endif

// ============================================================================
// Public API Implementation
// ============================================================================

asciichat_error_t options_state_init(void) {
  // Detect fork: after fork(), child process must reinitialize
  pid_t current_pid = platform_get_pid();
  if ((lifecycle_is_initialized(&g_options_lifecycle) || g_init_pid != -1) && g_init_pid != current_pid) {
    log_debug("Detected fork in options_state_init (parent PID %d, child PID %d) - resetting lifecycle", g_init_pid,
              current_pid);
    /* Reset lifecycle to allow reinitialization after fork */
    lifecycle_shutdown(&g_options_lifecycle);
    g_init_pid = current_pid;
  }

  // Check if already initialized in this process using lifecycle
  if (lifecycle_is_initialized(&g_options_lifecycle) && g_init_pid == current_pid) {
    log_warn("Options state already initialized");
    return ASCIICHAT_OK;
  }

  /* Use lifecycle to serialize initialization */
  if (!lifecycle_init(&g_options_lifecycle, "options")) {
    /* Another thread is initializing or already initialized */
    if (lifecycle_is_initialized(&g_options_lifecycle)) {
      return ASCIICHAT_OK;
    }
    return SET_ERRNO(ERROR_INVALID_STATE, "Failed to acquire options initialization lock");
  }

  // Initialize write mutex
  if (mutex_init(&g_options_write_mutex, "options_write") != 0) {
    lifecycle_shutdown(&g_options_lifecycle);
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize options write mutex");
  }

  // Initialize deferred free mutex
  if (mutex_init(&g_deferred_free_mutex, "deferred_free") != 0) {
    mutex_destroy(&g_options_write_mutex);
    lifecycle_shutdown(&g_options_lifecycle);
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize deferred free mutex");
  }

  // Allocate initial options struct (will be populated later by options_state_populate_from_globals)
  options_t *initial_opts = SAFE_MALLOC(sizeof(options_t), options_t *);
  if (!initial_opts) {
    mutex_destroy(&g_options_write_mutex);
    mutex_destroy(&g_deferred_free_mutex);
    lifecycle_shutdown(&g_options_lifecycle);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate initial options struct");
  }

  // Initialize all defaults using options_t_new()
  *initial_opts = options_t_new();

  // Publish initial struct (release semantics - make all fields visible to readers)
  atomic_ptr_store(&g_options, initial_opts);

  g_init_pid = current_pid; // Record PID to detect fork later
  log_dev("Options state initialized with RCU pattern (PID %d)", current_pid);

  return ASCIICHAT_OK;
}

asciichat_error_t options_state_set(const options_t *opts) {
  if (!opts) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "opts is NULL");
  }

  // Check initialization state using lifecycle
  if (!lifecycle_is_initialized(&g_options_lifecycle)) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Options state not initialized (call options_state_init first)");
  }

  // Get current struct (should be the initial zero-initialized one during startup)
  options_t *current = atomic_ptr_load(&g_options);

  // Copy provided options into current struct
  // Note: No need for RCU update here since this is called during initialization
  // before any reader threads exist
  memcpy(current, opts, sizeof(options_t));

  log_debug("Options state set from parsed struct");
  return ASCIICHAT_OK;
}

void options_state_destroy(void) {
  // Check if initialized using lifecycle
  if (!lifecycle_is_initialized(&g_options_lifecycle)) {
    return;
  }

  /* Shutdown lifecycle to mark as no longer initialized */
  lifecycle_shutdown(&g_options_lifecycle);

  // Get current options pointer
  options_t *current = atomic_load_explicit(&g_options, memory_order_acquire);

  // Clear the atomic pointer (prevent further reads)
  atomic_store_explicit(&g_options, NULL, memory_order_release);

  // Free current struct
  SAFE_FREE(current);

  // Free all deferred structs
  deferred_free_all();

  // Cleanup schema (frees all dynamically allocated config strings)
  config_schema_destroy();

  // Destroy dynamically created mutexes
  mutex_destroy(&g_options_write_mutex);
  mutex_destroy(&g_deferred_free_mutex);

  log_debug("Options state shutdown complete");
}

void options_cleanup_schema(void) {
  config_schema_destroy();
}

const options_t *options_get(void) {
  // Lock-free read with acquire semantics
  // Guarantees we see all writes made before the pointer was published
  options_t *current = atomic_load_explicit(&g_options, memory_order_acquire);

  // If options not yet published or after destruction, return safe static default.
  // This is a critical fallback that:
  // 1. Allows early startup code (before options_state_init) to work safely
  // 2. Allows atexit handlers and cleanup code to call GET_OPTION() safely
  // 3. Never crashes due to NULL pointer access
  // 4. Provides sensible defaults for all option fields
  //
  // The static default options are:
  // - Allocated once at startup (static storage)
  // - Never freed (outlives all dynamically allocated options)
  // - Thread-safe to read (immutable const data)
  // - Complete with all OPT_*_DEFAULT values matching options_t_new()
  if (!current) {
    return (const options_t *)&g_default_options;
  }

  return current;
}

static asciichat_error_t options_update(void (*updater)(options_t *, void *), void *context) {
  if (!updater) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "updater function is NULL");
  }

  if (!lifecycle_is_initialized(&g_options_lifecycle)) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Options state not initialized");
  }

  // Serialize writers with mutex
  mutex_lock(&g_options_write_mutex);

  // 1. Load current options (acquire semantics)
  options_t *old_opts = atomic_load_explicit(&g_options, memory_order_acquire);

  // 2. Allocate new options struct
  options_t *new_opts = SAFE_MALLOC(sizeof(options_t), options_t *);
  if (!new_opts) {
    mutex_unlock(&g_options_write_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate new options struct");
  }

  // 3. Copy current values to new struct
  memcpy(new_opts, old_opts, sizeof(options_t));

  // 4. Call user updater to modify the new struct
  updater(new_opts, context);

  // 5. Atomically swap global pointer (release semantics)
  // This makes the new struct visible to all readers
  atomic_store_explicit(&g_options, new_opts, memory_order_release);

  // 6. Add old struct to deferred free list
  deferred_free_add(old_opts);

  mutex_unlock(&g_options_write_mutex);

  log_debug("Options updated via RCU (old=%p, new=%p)", (void *)old_opts, (void *)new_opts);
  return ASCIICHAT_OK;
}

// ============================================================================
// Generic Option Setters
// ============================================================================

/**
 * @brief Context for integer field updates in RCU updater callback
 */
typedef struct {
  const char *field_name; ///< Name of the field to update
  int value;              ///< New value to set
} int_field_ctx_t;

static void int_field_updater(options_t *opts, void *context) {
  int_field_ctx_t *ctx = (int_field_ctx_t *)context;
  if (strcmp(ctx->field_name, "width") == 0)
    opts->width = ctx->value;
  else if (strcmp(ctx->field_name, "height") == 0)
    opts->height = ctx->value;
  else if (strcmp(ctx->field_name, "max_clients") == 0)
    opts->max_clients = ctx->value;
  else if (strcmp(ctx->field_name, "compression_level") == 0)
    opts->compression_level = ctx->value;
  else if (strcmp(ctx->field_name, "reconnect_attempts") == 0)
    opts->reconnect_attempts = ctx->value;
  else if (strcmp(ctx->field_name, "microphone_index") == 0)
    opts->microphone_index = ctx->value;
  else if (strcmp(ctx->field_name, "speakers_index") == 0)
    opts->speakers_index = ctx->value;
  else if (strcmp(ctx->field_name, "discovery_port") == 0)
    opts->discovery_port = ctx->value;
  else if (strcmp(ctx->field_name, "port") == 0)
    opts->port = ctx->value;
  else if (strcmp(ctx->field_name, "fps") == 0)
    opts->fps = ctx->value;
  else if (strcmp(ctx->field_name, "color_mode") == 0)
    opts->color_mode = (terminal_color_mode_t)ctx->value;
  else if (strcmp(ctx->field_name, "color_filter") == 0)
    opts->color_filter = (color_filter_t)ctx->value;
  else if (strcmp(ctx->field_name, "render_mode") == 0)
    opts->render_mode = (render_mode_t)ctx->value;
  else if (strcmp(ctx->field_name, "log_level") == 0)
    opts->log_level = (log_level_t)ctx->value;
  else if (strcmp(ctx->field_name, "palette_type") == 0)
    opts->palette_type = (palette_type_t)ctx->value;
}

asciichat_error_t options_set_int(const char *field_name, int value) {
  if (!field_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "field_name is NULL");
    return ERROR_INVALID_PARAM;
  }

  if (strcmp(field_name, "width") != 0 && strcmp(field_name, "height") != 0 && strcmp(field_name, "max_clients") != 0 &&
      strcmp(field_name, "compression_level") != 0 && strcmp(field_name, "reconnect_attempts") != 0 &&
      strcmp(field_name, "microphone_index") != 0 && strcmp(field_name, "speakers_index") != 0 &&
      strcmp(field_name, "discovery_port") != 0 && strcmp(field_name, "port") != 0 && strcmp(field_name, "fps") != 0 &&
      strcmp(field_name, "color_mode") != 0 && strcmp(field_name, "color_filter") != 0 &&
      strcmp(field_name, "render_mode") != 0 && strcmp(field_name, "log_level") != 0 &&
      strcmp(field_name, "palette_type") != 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Unknown integer field: %s", field_name);
    return ERROR_INVALID_PARAM;
  }

  int_field_ctx_t ctx = {.field_name = field_name, .value = value};
  return options_update(int_field_updater, &ctx);
}

/**
 * @brief Context for boolean field updates in RCU updater callback
 */
typedef struct {
  const char *field_name; ///< Name of the field to update
  bool value;             ///< New value to set
} bool_field_ctx_t;

static void bool_field_updater(options_t *opts, void *context) {
  bool_field_ctx_t *ctx = (bool_field_ctx_t *)context;
  if (strcmp(ctx->field_name, "no_compress") == 0)
    opts->no_compress = ctx->value;
  else if (strcmp(ctx->field_name, "encode_audio") == 0)
    opts->encode_audio = ctx->value;
  else if (strcmp(ctx->field_name, "flip_x") == 0)
    opts->flip_x = ctx->value;
  else if (strcmp(ctx->field_name, "flip_y") == 0)
    opts->flip_y = ctx->value;
  else if (strcmp(ctx->field_name, "test_pattern") == 0)
    opts->test_pattern = ctx->value;
  else if (strcmp(ctx->field_name, "no_audio_mixer") == 0)
    opts->no_audio_mixer = ctx->value;
  else if (strcmp(ctx->field_name, "show_capabilities") == 0)
    opts->show_capabilities = ctx->value;
  else if (strcmp(ctx->field_name, "force_utf8") == 0)
    opts->force_utf8 = ctx->value;
  else if (strcmp(ctx->field_name, "audio_enabled") == 0)
    opts->audio_enabled = ctx->value;
  else if (strcmp(ctx->field_name, "audio_analysis_enabled") == 0)
    opts->audio_analysis_enabled = ctx->value;
  else if (strcmp(ctx->field_name, "audio_no_playback") == 0)
    opts->audio_no_playback = ctx->value;
  else if (strcmp(ctx->field_name, "stretch") == 0)
    opts->stretch = ctx->value;
  else if (strcmp(ctx->field_name, "snapshot_mode") == 0)
    opts->snapshot_mode = ctx->value;
  else if (strcmp(ctx->field_name, "strip_ansi") == 0)
    opts->strip_ansi = ctx->value;
  else if (strcmp(ctx->field_name, "quiet") == 0)
    opts->quiet = ctx->value;
  else if (strcmp(ctx->field_name, "encrypt_enabled") == 0)
    opts->encrypt_enabled = ctx->value;
  else if (strcmp(ctx->field_name, "no_encrypt") == 0)
    opts->no_encrypt = ctx->value;
  else if (strcmp(ctx->field_name, "discovery") == 0)
    opts->discovery = ctx->value;
  else if (strcmp(ctx->field_name, "discovery_expose_ip") == 0)
    opts->discovery_expose_ip = ctx->value;
  else if (strcmp(ctx->field_name, "discovery_insecure") == 0)
    opts->discovery_insecure = ctx->value;
  else if (strcmp(ctx->field_name, "webrtc") == 0)
    opts->webrtc = ctx->value;
  else if (strcmp(ctx->field_name, "lan_discovery") == 0)
    opts->lan_discovery = ctx->value;
  else if (strcmp(ctx->field_name, "no_mdns_advertise") == 0)
    opts->no_mdns_advertise = ctx->value;
  else if (strcmp(ctx->field_name, "prefer_webrtc") == 0)
    opts->prefer_webrtc = ctx->value;
  else if (strcmp(ctx->field_name, "no_webrtc") == 0)
    opts->no_webrtc = ctx->value;
  else if (strcmp(ctx->field_name, "webrtc_skip_stun") == 0)
    opts->webrtc_skip_stun = ctx->value;
  else if (strcmp(ctx->field_name, "webrtc_disable_turn") == 0)
    opts->webrtc_disable_turn = ctx->value;
  else if (strcmp(ctx->field_name, "enable_upnp") == 0)
    opts->enable_upnp = ctx->value;
  else if (strcmp(ctx->field_name, "require_server_identity") == 0)
    opts->require_server_identity = ctx->value;
  else if (strcmp(ctx->field_name, "require_client_identity") == 0)
    opts->require_client_identity = ctx->value;
  else if (strcmp(ctx->field_name, "require_server_verify") == 0)
    opts->require_server_verify = ctx->value;
  else if (strcmp(ctx->field_name, "require_client_verify") == 0)
    opts->require_client_verify = ctx->value;
  else if (strcmp(ctx->field_name, "palette_custom_set") == 0)
    opts->palette_custom_set = ctx->value;
  else if (strcmp(ctx->field_name, "media_loop") == 0)
    opts->media_loop = ctx->value;
  else if (strcmp(ctx->field_name, "media_from_stdin") == 0)
    opts->media_from_stdin = ctx->value;
  else if (strcmp(ctx->field_name, "auto_width") == 0)
    opts->auto_width = ctx->value;
  else if (strcmp(ctx->field_name, "auto_height") == 0)
    opts->auto_height = ctx->value;
  else if (strcmp(ctx->field_name, "splash_screen") == 0)
    opts->splash_screen = ctx->value;
  else if (strcmp(ctx->field_name, "status_screen") == 0)
    opts->status_screen = ctx->value;
  else if (strcmp(ctx->field_name, "matrix_rain") == 0)
    opts->matrix_rain = ctx->value;
}

asciichat_error_t options_set_bool(const char *field_name, bool value) {
  if (!field_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "field_name is NULL");
    return ERROR_INVALID_PARAM;
  }

  // Validate field exists
  if (strcmp(field_name, "no_compress") != 0 && strcmp(field_name, "encode_audio") != 0 &&
      strcmp(field_name, "flip_x") != 0 && strcmp(field_name, "flip_y") != 0 &&
      strcmp(field_name, "test_pattern") != 0 && strcmp(field_name, "no_audio_mixer") != 0 &&
      strcmp(field_name, "show_capabilities") != 0 && strcmp(field_name, "force_utf8") != 0 &&
      strcmp(field_name, "audio_enabled") != 0 && strcmp(field_name, "audio_analysis_enabled") != 0 &&
      strcmp(field_name, "audio_no_playback") != 0 && strcmp(field_name, "stretch") != 0 &&
      strcmp(field_name, "snapshot_mode") != 0 && strcmp(field_name, "strip_ansi") != 0 &&
      strcmp(field_name, "quiet") != 0 && strcmp(field_name, "encrypt_enabled") != 0 &&
      strcmp(field_name, "no_encrypt") != 0 && strcmp(field_name, "discovery") != 0 &&
      strcmp(field_name, "discovery_expose_ip") != 0 && strcmp(field_name, "discovery_insecure") != 0 &&
      strcmp(field_name, "webrtc") != 0 && strcmp(field_name, "lan_discovery") != 0 &&
      strcmp(field_name, "no_mdns_advertise") != 0 && strcmp(field_name, "prefer_webrtc") != 0 &&
      strcmp(field_name, "no_webrtc") != 0 && strcmp(field_name, "webrtc_skip_stun") != 0 &&
      strcmp(field_name, "webrtc_disable_turn") != 0 && strcmp(field_name, "enable_upnp") != 0 &&
      strcmp(field_name, "require_server_identity") != 0 && strcmp(field_name, "require_client_identity") != 0 &&
      strcmp(field_name, "require_server_verify") != 0 && strcmp(field_name, "require_client_verify") != 0 &&
      strcmp(field_name, "palette_custom_set") != 0 && strcmp(field_name, "media_loop") != 0 &&
      strcmp(field_name, "media_from_stdin") != 0 && strcmp(field_name, "auto_width") != 0 &&
      strcmp(field_name, "auto_height") != 0 && strcmp(field_name, "splash_screen") != 0 &&
      strcmp(field_name, "status_screen") != 0 && strcmp(field_name, "matrix_rain") != 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Unknown boolean field: %s", field_name);
    return ERROR_INVALID_PARAM;
  }

  bool_field_ctx_t ctx = {.field_name = field_name, .value = value};
  return options_update(bool_field_updater, &ctx);
}

/**
 * @brief Context for string field updates in RCU updater callback
 */
typedef struct {
  const char *field_name; ///< Name of the field to update
  const char *value;      ///< New value to set
} string_field_ctx_t;

static void string_field_updater(options_t *opts, void *context) {
  string_field_ctx_t *ctx = (string_field_ctx_t *)context;
  if (strcmp(ctx->field_name, "address") == 0)
    SAFE_STRNCPY(opts->address, ctx->value, sizeof(opts->address));
  else if (strcmp(ctx->field_name, "address6") == 0)
    SAFE_STRNCPY(opts->address6, ctx->value, sizeof(opts->address6));
  else if (strcmp(ctx->field_name, "encrypt_key") == 0)
    SAFE_STRNCPY(opts->encrypt_key, ctx->value, sizeof(opts->encrypt_key));
  else if (strcmp(ctx->field_name, "password") == 0)
    SAFE_STRNCPY(opts->password, ctx->value, sizeof(opts->password));
  else if (strcmp(ctx->field_name, "encrypt_keyfile") == 0)
    SAFE_STRNCPY(opts->encrypt_keyfile, ctx->value, sizeof(opts->encrypt_keyfile));
  else if (strcmp(ctx->field_name, "server_key") == 0)
    SAFE_STRNCPY(opts->server_key, ctx->value, sizeof(opts->server_key));
  else if (strcmp(ctx->field_name, "client_keys") == 0)
    SAFE_STRNCPY(opts->client_keys, ctx->value, sizeof(opts->client_keys));
  else if (strcmp(ctx->field_name, "discovery_server") == 0)
    SAFE_STRNCPY(opts->discovery_server, ctx->value, sizeof(opts->discovery_server));
  else if (strcmp(ctx->field_name, "discovery_service_key") == 0)
    SAFE_STRNCPY(opts->discovery_service_key, ctx->value, sizeof(opts->discovery_service_key));
  else if (strcmp(ctx->field_name, "discovery_database_path") == 0)
    SAFE_STRNCPY(opts->discovery_database_path, ctx->value, sizeof(opts->discovery_database_path));
  else if (strcmp(ctx->field_name, "log_file") == 0)
    SAFE_STRNCPY(opts->log_file, ctx->value, sizeof(opts->log_file));
  else if (strcmp(ctx->field_name, "media_file") == 0)
    SAFE_STRNCPY(opts->media_file, ctx->value, sizeof(opts->media_file));
  else if (strcmp(ctx->field_name, "palette_custom") == 0)
    SAFE_STRNCPY(opts->palette_custom, ctx->value, sizeof(opts->palette_custom));
  else if (strcmp(ctx->field_name, "stun_servers") == 0)
    SAFE_STRNCPY(opts->stun_servers, ctx->value, sizeof(opts->stun_servers));
  else if (strcmp(ctx->field_name, "turn_servers") == 0)
    SAFE_STRNCPY(opts->turn_servers, ctx->value, sizeof(opts->turn_servers));
  else if (strcmp(ctx->field_name, "turn_username") == 0)
    SAFE_STRNCPY(opts->turn_username, ctx->value, sizeof(opts->turn_username));
  else if (strcmp(ctx->field_name, "turn_credential") == 0)
    SAFE_STRNCPY(opts->turn_credential, ctx->value, sizeof(opts->turn_credential));
  else if (strcmp(ctx->field_name, "turn_secret") == 0)
    SAFE_STRNCPY(opts->turn_secret, ctx->value, sizeof(opts->turn_secret));
  else if (strcmp(ctx->field_name, "session_string") == 0)
    SAFE_STRNCPY(opts->session_string, ctx->value, sizeof(opts->session_string));
}

asciichat_error_t options_set_string(const char *field_name, const char *value) {
  if (!field_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "field_name is NULL");
    return ERROR_INVALID_PARAM;
  }

  if (!value) {
    SET_ERRNO(ERROR_INVALID_PARAM, "value is NULL");
    return ERROR_INVALID_PARAM;
  }

  // Validate field exists
  if (strcmp(field_name, "address") != 0 && strcmp(field_name, "address6") != 0 &&
      strcmp(field_name, "encrypt_key") != 0 && strcmp(field_name, "password") != 0 &&
      strcmp(field_name, "encrypt_keyfile") != 0 && strcmp(field_name, "server_key") != 0 &&
      strcmp(field_name, "client_keys") != 0 && strcmp(field_name, "discovery_server") != 0 &&
      strcmp(field_name, "discovery_service_key") != 0 && strcmp(field_name, "discovery_database_path") != 0 &&
      strcmp(field_name, "log_file") != 0 && strcmp(field_name, "media_file") != 0 &&
      strcmp(field_name, "palette_custom") != 0 && strcmp(field_name, "stun_servers") != 0 &&
      strcmp(field_name, "turn_servers") != 0 && strcmp(field_name, "turn_username") != 0 &&
      strcmp(field_name, "turn_credential") != 0 && strcmp(field_name, "turn_secret") != 0 &&
      strcmp(field_name, "session_string") != 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Unknown string field: %s", field_name);
    return ERROR_INVALID_PARAM;
  }

  string_field_ctx_t ctx = {.field_name = field_name, .value = value};
  return options_update(string_field_updater, &ctx);
}

/**
 * @brief Context for double field updates in RCU updater callback
 */
typedef struct {
  const char *field_name; ///< Name of the field to update
  double value;           ///< New value to set
} double_field_ctx_t;

static void double_field_updater(options_t *opts, void *context) {
  double_field_ctx_t *ctx = (double_field_ctx_t *)context;
  if (strcmp(ctx->field_name, "snapshot_delay") == 0)
    opts->snapshot_delay = ctx->value;
  else if (strcmp(ctx->field_name, "microphone_sensitivity") == 0)
    opts->microphone_sensitivity = ctx->value;
  else if (strcmp(ctx->field_name, "speakers_volume") == 0)
    opts->speakers_volume = ctx->value;
}

asciichat_error_t options_set_double(const char *field_name, double value) {
  if (!field_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "field_name is NULL");
    return ERROR_INVALID_PARAM;
  }

  // Normalize option names to internal field names
  const char *internal_name = field_name;
  if (strcmp(field_name, "microphone-volume") == 0 || strcmp(field_name, "ivolume") == 0) {
    internal_name = "microphone_sensitivity";
  } else if (strcmp(field_name, "speakers-volume") == 0 || strcmp(field_name, "volume") == 0) {
    internal_name = "speakers_volume";
  }

  if (strcmp(internal_name, "snapshot_delay") != 0 && strcmp(internal_name, "microphone_sensitivity") != 0 &&
      strcmp(internal_name, "speakers_volume") != 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Unknown double field: %s", field_name);
    return ERROR_INVALID_PARAM;
  }

  double_field_ctx_t ctx = {.field_name = internal_name, .value = value};
  return options_update(double_field_updater, &ctx);
}
