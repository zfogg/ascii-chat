/**
 * @file options_state.c
 * @brief 🔒 RCU-based thread-safe options state implementation
 */

#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/runtime.h>
#include <math.h>
#include <yyjson.h>
#include <limits.h>
#include <ascii-chat/options/schema.h>
#include <ascii-chat/options/registry/core.h>
#include <ascii-chat/options/registry/common.h>
#include <ascii-chat/options/builder.h>
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
static atomic_ptr_t g_options = {0};

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
    .webrtc_relay_only = false,
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
    .fps_explicitly_set = false,
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
    .audio_fft_size = 1024,
    .test_audio = false,
    .audio_spectral = true,
    .audio_spectral_mixer = false,
    .audio_spectral_gate = true,
    .audio_enabled = OPT_AUDIO_ENABLED_DEFAULT,
    .audio_source = OPT_AUDIO_SOURCE_DEFAULT,
    .audio_capture_source = OPT_AUDIO_CAPTURE_SOURCE_DEFAULT,
    .waveform = false,
    .fft = false,
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
typedef struct retired_options {
  options_t *value;
  struct retired_options *next;
} retired_options_t;
static retired_options_t *g_retired;
static size_t g_retired_count;
static mutex_t g_deferred_free_mutex;
static uint64_t g_revision;
static options_origin_t g_origin;

// options_get() borrows snapshots until shutdown. Preserve that public lifetime;
// never free a published snapshot while a legacy reader can still hold it.
static void deferred_free_add(options_t *old_opts) {
  retired_options_t *node = SAFE_MALLOC(sizeof(*node), retired_options_t *);
  node->value = old_opts;
  mutex_lock(&g_deferred_free_mutex);
  node->next = g_retired;
  g_retired = node;
  ++g_retired_count;
  mutex_unlock(&g_deferred_free_mutex);
}

size_t options_retired_snapshot_count(void) {
  if (!lifecycle_is_initialized(&g_options_lifecycle))
    return 0;
  mutex_lock(&g_deferred_free_mutex);
  size_t count = g_retired_count;
  mutex_unlock(&g_deferred_free_mutex);
  return count;
}

static void deferred_free_all(void) {
  while (g_retired) {
    retired_options_t *node = g_retired;
    g_retired = node->next;
    --g_retired_count;
    SAFE_FREE(node->value);
    SAFE_FREE(node);
  }
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
  // RCU setters validate against the option registry, including when callers
  // use the state API directly in tests or embedded integrations. Initialize
  // the registry here so those callers observe the same metadata as the CLI.
  registry_init_size();

  // Detect fork: after fork(), child process must reinitialize
  pid_t current_pid = platform_get_pid();
  if ((lifecycle_is_initialized(&g_options_lifecycle) || g_init_pid != -1) && g_init_pid != current_pid) {
    log_debug("Detected fork (parent PID %d, child PID %d) - resetting lifecycle", g_init_pid, current_pid);
    /* Reset lifecycle to allow reinitialization after fork */
    lifecycle_shutdown(&g_options_lifecycle);
    g_init_pid = current_pid;
  }

  // Check if already initialized in this process using lifecycle
  if (lifecycle_is_initialized(&g_options_lifecycle) && g_init_pid == current_pid &&
      atomic_ptr_load(&g_options) != NULL) {
    log_warn("Options state already initialized");
    return ASCIICHAT_OK;
  }
  if (lifecycle_is_initialized(&g_options_lifecycle) && atomic_ptr_load(&g_options) == NULL) {
    lifecycle_shutdown(&g_options_lifecycle);
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

  ++g_revision;
  g_origin = OPTIONS_ORIGIN_CONFIG;
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

  mutex_lock(&g_options_write_mutex);
  options_t *next = SAFE_MALLOC(sizeof(*next), options_t *);
  *next = *opts;
  options_t *previous = atomic_ptr_load(&g_options);
  atomic_ptr_store(&g_options, next);
  ++g_revision;
  g_origin = OPTIONS_ORIGIN_CONFIG;
  deferred_free_add(previous);
  mutex_unlock(&g_options_write_mutex);
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
  options_t *current = atomic_ptr_load(&g_options);

  // Clear the atomic pointer (prevent further reads)
  atomic_ptr_store(&g_options, NULL);

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
  options_t *current = atomic_ptr_load(&g_options);

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

typedef struct {
  const char *name;
  options_value_type_t type;
  size_t offset, size;
  const char *cli;
} runtime_field_t;
#define FIELD(name, type, cli) {#name, type, offsetof(options_t, name), sizeof(((options_t *)0)->name), cli}
static const runtime_field_t runtime_fields[] = {
    FIELD(width, OPTIONS_VALUE_INT, "width"),
    FIELD(height, OPTIONS_VALUE_INT, "height"),
    FIELD(max_clients, OPTIONS_VALUE_INT, "max-clients"),
    FIELD(compression_level, OPTIONS_VALUE_INT, "compression-level"),
    FIELD(reconnect_attempts, OPTIONS_VALUE_INT, "reconnect-attempts"),
    FIELD(microphone_index, OPTIONS_VALUE_INT, "microphone-index"),
    FIELD(speakers_index, OPTIONS_VALUE_INT, "speakers-index"),
    FIELD(discovery_port, OPTIONS_VALUE_INT, "discovery-port"),
    FIELD(port, OPTIONS_VALUE_INT, "port"),
    FIELD(fps, OPTIONS_VALUE_INT, "fps"),
    FIELD(color_mode, OPTIONS_VALUE_INT, "color-mode"),
    FIELD(color_filter, OPTIONS_VALUE_INT, "color-filter"),
    FIELD(render_mode, OPTIONS_VALUE_INT, "render-mode"),
    FIELD(log_level, OPTIONS_VALUE_INT, "log-level"),
    FIELD(palette_type, OPTIONS_VALUE_INT, "palette"),
    FIELD(no_compress, OPTIONS_VALUE_BOOL, "no-compress"),
    FIELD(encode_audio, OPTIONS_VALUE_BOOL, "encode-audio"),
    FIELD(flip_x, OPTIONS_VALUE_BOOL, "flip-x"),
    FIELD(flip_y, OPTIONS_VALUE_BOOL, "flip-y"),
    FIELD(test_pattern, OPTIONS_VALUE_BOOL, "test-pattern"),
    FIELD(no_audio_mixer, OPTIONS_VALUE_BOOL, "no-audio-mixer"),
    FIELD(show_capabilities, OPTIONS_VALUE_BOOL, "show-capabilities"),
    FIELD(force_utf8, OPTIONS_VALUE_BOOL, "force-utf8"),
    FIELD(audio_enabled, OPTIONS_VALUE_BOOL, "audio-enabled"),
    FIELD(audio_analysis_enabled, OPTIONS_VALUE_BOOL, "audio-analysis-enabled"),
    FIELD(audio_no_playback, OPTIONS_VALUE_BOOL, "audio-no-playback"),
    FIELD(stretch, OPTIONS_VALUE_BOOL, "stretch"),
    FIELD(snapshot_mode, OPTIONS_VALUE_BOOL, "snapshot-mode"),
    FIELD(strip_ansi, OPTIONS_VALUE_BOOL, "strip-ansi"),
    FIELD(quiet, OPTIONS_VALUE_BOOL, "quiet"),
    FIELD(encrypt_enabled, OPTIONS_VALUE_BOOL, "encrypt-enabled"),
    FIELD(no_encrypt, OPTIONS_VALUE_BOOL, "no-encrypt"),
    FIELD(discovery, OPTIONS_VALUE_BOOL, "discovery"),
    FIELD(discovery_expose_ip, OPTIONS_VALUE_BOOL, "discovery-expose-ip"),
    FIELD(discovery_insecure, OPTIONS_VALUE_BOOL, "discovery-insecure"),
    FIELD(webrtc, OPTIONS_VALUE_BOOL, "webrtc"),
    FIELD(lan_discovery, OPTIONS_VALUE_BOOL, "lan-discovery"),
    FIELD(no_mdns_advertise, OPTIONS_VALUE_BOOL, "no-mdns-advertise"),
    FIELD(prefer_webrtc, OPTIONS_VALUE_BOOL, "prefer-webrtc"),
    FIELD(no_webrtc, OPTIONS_VALUE_BOOL, "no-webrtc"),
    FIELD(webrtc_skip_stun, OPTIONS_VALUE_BOOL, "webrtc-skip-stun"),
    FIELD(webrtc_relay_only, OPTIONS_VALUE_BOOL, "webrtc-relay-only"),
    FIELD(webrtc_disable_turn, OPTIONS_VALUE_BOOL, "webrtc-disable-turn"),
    FIELD(enable_upnp, OPTIONS_VALUE_BOOL, "enable-upnp"),
    FIELD(require_server_identity, OPTIONS_VALUE_BOOL, "require-server-identity"),
    FIELD(require_client_identity, OPTIONS_VALUE_BOOL, "require-client-identity"),
    FIELD(require_server_verify, OPTIONS_VALUE_BOOL, "require-server-verify"),
    FIELD(require_client_verify, OPTIONS_VALUE_BOOL, "require-client-verify"),
    FIELD(palette_custom_set, OPTIONS_VALUE_BOOL, "palette-custom-set"),
    FIELD(media_loop, OPTIONS_VALUE_BOOL, "media-loop"),
    FIELD(media_from_stdin, OPTIONS_VALUE_BOOL, "media-from-stdin"),
    FIELD(auto_width, OPTIONS_VALUE_BOOL, "auto-width"),
    FIELD(auto_height, OPTIONS_VALUE_BOOL, "auto-height"),
    FIELD(splash_screen, OPTIONS_VALUE_BOOL, "splash-screen"),
    FIELD(status_screen, OPTIONS_VALUE_BOOL, "status-screen"),
    FIELD(matrix_rain, OPTIONS_VALUE_BOOL, "matrix-rain"),
    FIELD(waveform, OPTIONS_VALUE_BOOL, "waveform"),
    FIELD(fft, OPTIONS_VALUE_BOOL, "fft"),
    FIELD(fps_counter, OPTIONS_VALUE_BOOL, "fps-counter"),
    FIELD(address, OPTIONS_VALUE_STRING, "address"),
    FIELD(address6, OPTIONS_VALUE_STRING, "address6"),
    FIELD(encrypt_key, OPTIONS_VALUE_STRING, "encrypt-key"),
    FIELD(password, OPTIONS_VALUE_STRING, "password"),
    FIELD(encrypt_keyfile, OPTIONS_VALUE_STRING, "encrypt-keyfile"),
    FIELD(server_key, OPTIONS_VALUE_STRING, "server-key"),
    FIELD(client_keys, OPTIONS_VALUE_STRING, "client-keys"),
    FIELD(discovery_server, OPTIONS_VALUE_STRING, "discovery-server"),
    FIELD(discovery_service_key, OPTIONS_VALUE_STRING, "discovery-service-key"),
    FIELD(discovery_database_path, OPTIONS_VALUE_STRING, "discovery-database-path"),
    FIELD(log_file, OPTIONS_VALUE_STRING, "log-file"),
    FIELD(media_file, OPTIONS_VALUE_STRING, "media-file"),
    FIELD(palette_custom, OPTIONS_VALUE_STRING, "palette-chars"),
    FIELD(stun_servers, OPTIONS_VALUE_STRING, "stun-servers"),
    FIELD(turn_servers, OPTIONS_VALUE_STRING, "turn-servers"),
    FIELD(turn_username, OPTIONS_VALUE_STRING, "turn-username"),
    FIELD(turn_credential, OPTIONS_VALUE_STRING, "turn-credential"),
    FIELD(turn_secret, OPTIONS_VALUE_STRING, "turn-secret"),
    FIELD(session_string, OPTIONS_VALUE_STRING, "session-string"),
    FIELD(snapshot_delay, OPTIONS_VALUE_DOUBLE, "snapshot-delay"),
    FIELD(microphone_sensitivity, OPTIONS_VALUE_DOUBLE, "microphone-volume"),
    FIELD(speakers_volume, OPTIONS_VALUE_DOUBLE, "speakers-volume"),
};
#undef FIELD

static const runtime_field_t *find_field(const char *name) {
  if (!name)
    return NULL;
  if (!strcmp(name, "volume") || !strcmp(name, "speakers-volume"))
    name = "speakers_volume";
  if (!strcmp(name, "ivolume") || !strcmp(name, "microphone-volume"))
    name = "microphone_sensitivity";
  for (size_t i = 0; i < sizeof(runtime_fields) / sizeof(*runtime_fields); ++i)
    if (!strcmp(name, runtime_fields[i].name) || !strcmp(name, runtime_fields[i].cli))
      return &runtime_fields[i];
  return NULL;
}

bool options_remote_render_field(const char *name) {
  const runtime_field_t *field = find_field(name);
  if (!field)
    return false;
  const char *allowed[] = {"width",        "height",         "fps",        "color_mode", "color_filter", "render_mode",
                           "palette_type", "palette_custom", "matrix_rain"};
  for (size_t i = 0; i < sizeof(allowed) / sizeof(*allowed); ++i)
    if (!strcmp(field->name, allowed[i]))
      return true;
  return false;
}

static asciichat_error_t validate_field(const runtime_field_t *field, const options_t *candidate) {
  const registry_entry_t *entry = registry_find_entry_by_name(field->cli);
  const char *ptr = (const char *)candidate + field->offset;
  if (entry && entry->validate_fn && !entry->validate_fn(candidate))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid option '%s'", field->name);
  if (field->type == OPTIONS_VALUE_INT || field->type == OPTIONS_VALUE_DOUBLE) {
    double value = field->type == OPTIONS_VALUE_INT ? *(const int *)ptr
                   : field->size == sizeof(float)   ? *(const float *)ptr
                                                    : *(const double *)ptr;
    if (!isfinite(value))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Nonfinite option '%s'", field->name);
    if (entry && (entry->metadata.numeric_range.min || entry->metadata.numeric_range.max) &&
        (value < entry->metadata.numeric_range.min || value > entry->metadata.numeric_range.max))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Option '%s' is out of range", field->name);
    if (field->type == OPTIONS_VALUE_INT && entry && entry->metadata.enum_values &&
        entry->metadata.input_type == OPTION_INPUT_ENUM) {
      bool found = false;
      for (size_t i = 0; entry->metadata.enum_values[i]; ++i) {
        int enum_value = entry->metadata.enum_integer_values ? entry->metadata.enum_integer_values[i] : (int)i;
        if (*(const int *)ptr == enum_value)
          found = true;
      }
      if (!found)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Unknown value for '%s'", field->name);
    }
    if ((!strcmp(field->name, "port") || !strcmp(field->name, "discovery_port")) && (value < 1 || value > 65535))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Port must be in [1, 65535]");
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t edit_candidate(options_t *candidate, const options_edit_t *edit) {
  const runtime_field_t *field = find_field(edit->name);
  if (!field || field->type != edit->type)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unknown option or wrong type: %s", edit->name ? edit->name : "(null)");
  char *ptr = (char *)candidate + field->offset;
  switch (edit->type) {
  case OPTIONS_VALUE_INT: {
    int value = edit->value.integer;
    if (edit->operation == OPTIONS_CYCLE) {
      if (value <= 0 || value > 256)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid cycle size");
      value = (*(int *)ptr + 1) % value;
    } else if (edit->operation != OPTIONS_ASSIGN)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid integer operation");
    memcpy(ptr, &value, sizeof(value));
    break;
  }
  case OPTIONS_VALUE_BOOL: {
    bool value = edit->value.boolean;
    if (edit->operation == OPTIONS_TOGGLE)
      value = field->size == sizeof(int) ? !*(int *)ptr : !*(bool *)ptr;
    else if (edit->operation != OPTIONS_ASSIGN)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid boolean operation");
    if (field->size == sizeof(int)) {
      int integer = value;
      memcpy(ptr, &integer, sizeof(integer));
    } else
      memcpy(ptr, &value, sizeof(value));
    // An animation selection is one coherent change, including its exclusions.
    if (value &&
        (!strcmp(field->name, "matrix_rain") || !strcmp(field->name, "waveform") || !strcmp(field->name, "fft"))) {
      candidate->matrix_rain = !strcmp(field->name, "matrix_rain");
      candidate->waveform = !strcmp(field->name, "waveform");
      candidate->fft = !strcmp(field->name, "fft");
    }
    break;
  }
  case OPTIONS_VALUE_DOUBLE: {
    double value = edit->value.number;
    if (edit->operation == OPTIONS_ADD) {
      value += field->size == sizeof(float) ? *(float *)ptr : *(double *)ptr;
      if (!strcmp(field->name, "speakers_volume") || !strcmp(field->name, "microphone_sensitivity"))
        value = fmax(0.0, fmin(1.0, value));
    } else if (edit->operation != OPTIONS_ASSIGN)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid number operation");
    if (!isfinite(value))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Nonfinite option '%s'", field->name);
    if (field->size == sizeof(float)) {
      float number = (float)value;
      memcpy(ptr, &number, sizeof(number));
    } else
      memcpy(ptr, &value, sizeof(value));
    break;
  }
  case OPTIONS_VALUE_STRING: {
    if (edit->operation != OPTIONS_ASSIGN || !edit->value.string)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid string option '%s'", field->name);
    size_t length = strlen(edit->value.string);
    if (length >= field->size)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Overlong string option '%s'", field->name);
    memset(ptr, 0, field->size);
    memcpy(ptr, edit->value.string, length);
    if (!strcmp(field->name, "palette_custom"))
      candidate->palette_custom_set = ptr[0] != 0;
    break;
  }
  }
  return ASCIICHAT_OK;
}

asciichat_error_t options_prepare_patch(const options_t *current, const options_patch_t *patch, options_t *candidate,
                                        bool remote_only) {
  if (!current || !candidate || current == candidate || !patch || !patch->edits || !patch->count ||
      patch->count > OPTIONS_PATCH_MAX_EDITS)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid options patch");
  *candidate = *current;
  for (size_t i = 0; i < patch->count; ++i) {
    if (remote_only &&
        (!options_remote_render_field(patch->edits[i].name) || patch->edits[i].operation != OPTIONS_ASSIGN))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Option is not a remote render preference");
    asciichat_error_t error = edit_candidate(candidate, &patch->edits[i]);
    if (error != ASCIICHAT_OK)
      return error;
  }
  for (size_t i = 0; i < patch->count; ++i) {
    asciichat_error_t error = validate_field(find_field(patch->edits[i].name), candidate);
    if (error != ASCIICHAT_OK)
      return error;
  }
  return ASCIICHAT_OK;
}

asciichat_error_t options_apply_patch(const options_patch_t *patch, options_patch_result_t *result) {
  if (!patch || !patch->edits || !patch->count || patch->count > OPTIONS_PATCH_MAX_EDITS)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid options patch");
  if (!lifecycle_is_initialized(&g_options_lifecycle))
    return SET_ERRNO(ERROR_INVALID_STATE, "Options are not initialized");
  mutex_lock(&g_options_write_mutex);
  if (result)
    *result = (options_patch_result_t){.revision = g_revision};
  if (patch->expected_revision != OPTIONS_REVISION_ANY && patch->expected_revision != g_revision) {
    mutex_unlock(&g_options_write_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Options changed; refresh and retry");
  }
  options_t *current = atomic_ptr_load(&g_options);
  options_t *candidate = SAFE_MALLOC(sizeof(*candidate), options_t *);
  asciichat_error_t error = options_prepare_patch(current, patch, candidate, false);
  if (error == ASCIICHAT_OK && memcmp(current, candidate, sizeof(*current))) {
    atomic_ptr_store(&g_options, candidate);
    deferred_free_add(current);
    ++g_revision;
    g_origin = patch->origin;
    if (result)
      *result = (options_patch_result_t){.revision = g_revision, .changed = true};
  } else
    SAFE_FREE(candidate);
  mutex_unlock(&g_options_write_mutex);
  return error;
}

asciichat_error_t options_read_snapshot(options_snapshot_t *snapshot) {
  if (!snapshot || !lifecycle_is_initialized(&g_options_lifecycle))
    return SET_ERRNO(ERROR_INVALID_STATE, "Options are not initialized");
  mutex_lock(&g_options_write_mutex);
  snapshot->values = *(options_t *)atomic_ptr_load(&g_options);
  snapshot->revision = g_revision;
  snapshot->origin = g_origin;
  mutex_unlock(&g_options_write_mutex);
  return ASCIICHAT_OK;
}

uint64_t options_revision(void) {
  if (!lifecycle_is_initialized(&g_options_lifecycle))
    return 0;
  mutex_lock(&g_options_write_mutex);
  uint64_t revision = g_revision;
  mutex_unlock(&g_options_write_mutex);
  return revision;
}

static asciichat_error_t apply_one(options_edit_t edit) {
  options_patch_t patch = {.edits = &edit, .count = 1, .expected_revision = OPTIONS_REVISION_ANY};
  return options_apply_patch(&patch, NULL);
}
/**
 * Assign an integer through the serialized writer transaction.
 * Resolve aliases and validate the final candidate before publishing it.
 * A rejected edit leaves the current snapshot and revision intact.
 */
asciichat_error_t options_set_int(const char *name, int value) {
  return apply_one((options_edit_t){.name = name, .type = OPTIONS_VALUE_INT, .value.integer = value});
}
/**
 * Assign a boolean through the same transaction used by patches and toggles.
 * Field descriptors account for both bool and integer-backed storage.
 * Animation exclusions are applied atomically with the requested value.
 */
asciichat_error_t options_set_bool(const char *name, bool value) {
  return apply_one((options_edit_t){.name = name, .type = OPTIONS_VALUE_BOOL, .value.boolean = value});
}
asciichat_error_t options_set_double(const char *name, double value) {
  return apply_one((options_edit_t){.name = name, .type = OPTIONS_VALUE_DOUBLE, .value.number = value});
}
asciichat_error_t options_set_string(const char *name, const char *value) {
  return apply_one((options_edit_t){.name = name, .type = OPTIONS_VALUE_STRING, .value.string = value});
}
asciichat_error_t options_toggle(const char *name) {
  return apply_one((options_edit_t){.name = name, .type = OPTIONS_VALUE_BOOL, .operation = OPTIONS_TOGGLE});
}
asciichat_error_t options_adjust(const char *name, double amount) {
  return apply_one(
      (options_edit_t){.name = name, .type = OPTIONS_VALUE_DOUBLE, .operation = OPTIONS_ADD, .value.number = amount});
}
asciichat_error_t options_cycle(const char *name, int count) {
  return apply_one(
      (options_edit_t){.name = name, .type = OPTIONS_VALUE_INT, .operation = OPTIONS_CYCLE, .value.integer = count});
}

static void *json_allocate(void *context, size_t size) {
  (void)context;
  return SAFE_MALLOC(size, void *);
}
static void *json_reallocate(void *context, void *ptr, size_t old_size, size_t size) {
  (void)context;
  (void)old_size;
  return SAFE_REALLOC(ptr, size, void *);
}
static void json_release(void *context, void *ptr) {
  (void)context;
  SAFE_FREE(ptr);
}
static const yyjson_alc runtime_json_allocator = {json_allocate, json_reallocate, json_release, NULL};

static asciichat_error_t parse_json_edits(const char *json, size_t length, yyjson_doc **doc, options_edit_t *edits,
                                          size_t *count) {
  if (!json || !length || length > 4096)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid settings payload size");
  *doc = yyjson_read_opts((char *)json, length, 0, &runtime_json_allocator, NULL);
  yyjson_val *root = *doc ? yyjson_doc_get_root(*doc) : NULL;
  if (!yyjson_is_obj(root) || !yyjson_obj_size(root) || yyjson_obj_size(root) > OPTIONS_PATCH_MAX_EDITS)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Expected a nonempty settings object");
  size_t index, max;
  yyjson_val *key, *value;
  *count = 0;
  yyjson_obj_foreach(root, index, max, key, value) {
    const char *name = yyjson_get_str(key);
    if (strlen(name) != yyjson_get_len(key))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Setting names cannot contain NUL");
    const runtime_field_t *field = find_field(name);
    if (!field)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Unknown setting '%s'", name);
    for (size_t j = 0; j < *count; ++j)
      if (find_field(edits[j].name) == field)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Duplicate setting '%s'", name);
    options_edit_t edit = {.name = name, .type = field->type};
    switch (field->type) {
    case OPTIONS_VALUE_INT:
      if (!yyjson_is_int(value) || yyjson_get_sint(value) < INT_MIN || yyjson_get_sint(value) > INT_MAX)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Expected integer '%s'", name);
      edit.value.integer = (int)yyjson_get_sint(value);
      break;
    case OPTIONS_VALUE_BOOL:
      if (!yyjson_is_bool(value))
        return SET_ERRNO(ERROR_INVALID_PARAM, "Expected boolean '%s'", name);
      edit.value.boolean = yyjson_get_bool(value);
      break;
    case OPTIONS_VALUE_DOUBLE:
      if (!yyjson_is_num(value))
        return SET_ERRNO(ERROR_INVALID_PARAM, "Expected number '%s'", name);
      edit.value.number = yyjson_get_num(value);
      break;
    case OPTIONS_VALUE_STRING:
      if (!yyjson_is_str(value) || strlen(yyjson_get_str(value)) != yyjson_get_len(value))
        return SET_ERRNO(ERROR_INVALID_PARAM, "Expected string without NUL '%s'", name);
      edit.value.string = yyjson_get_str(value);
      break;
    }
    edits[(*count)++] = edit;
  }
  return ASCIICHAT_OK;
}

asciichat_error_t options_prepare_json(const options_t *current, const char *json, size_t length, options_t *candidate,
                                       bool remote_only) {
  yyjson_doc *doc = NULL;
  options_edit_t edits[OPTIONS_PATCH_MAX_EDITS];
  size_t count = 0;
  asciichat_error_t error = parse_json_edits(json, length, &doc, edits, &count);
  options_patch_t patch = {.edits = edits, .count = count, .expected_revision = OPTIONS_REVISION_ANY};
  if (error == ASCIICHAT_OK)
    error = options_prepare_patch(current, &patch, candidate, remote_only);
  yyjson_doc_free(doc);
  return error;
}

asciichat_error_t options_apply_json(const char *json, size_t length, uint64_t expected_revision,
                                     options_origin_t origin, options_patch_result_t *result) {
  yyjson_doc *doc = NULL;
  options_edit_t edits[OPTIONS_PATCH_MAX_EDITS];
  size_t count = 0;
  asciichat_error_t error = parse_json_edits(json, length, &doc, edits, &count);
  options_patch_t patch = {.edits = edits, .count = count, .expected_revision = expected_revision, .origin = origin};
  if (error == ASCIICHAT_OK)
    error = options_apply_patch(&patch, result);
  yyjson_doc_free(doc);
  return error;
}

char *options_values_json(const options_t *values, bool remote_only) {
  yyjson_mut_doc *doc = yyjson_mut_doc_new(&runtime_json_allocator);
  if (!doc)
    return NULL;
  yyjson_mut_val *root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);
  for (size_t i = 0; i < sizeof(runtime_fields) / sizeof(*runtime_fields); ++i) {
    const runtime_field_t *field = &runtime_fields[i];
    bool exposed = options_remote_render_field(field->name);
    if (!remote_only && (!strcmp(field->name, "flip_x") || !strcmp(field->name, "flip_y") ||
                         !strcmp(field->name, "waveform") || !strcmp(field->name, "fft") ||
                         !strcmp(field->name, "fps_counter") || !strcmp(field->name, "speakers_volume") ||
                         !strcmp(field->name, "microphone_sensitivity") || !strcmp(field->name, "audio_enabled")))
      exposed = true;
    if (!exposed)
      continue;
    const char *ptr = (const char *)values + field->offset;
    switch (field->type) {
    case OPTIONS_VALUE_INT:
      yyjson_mut_obj_add_int(doc, root, field->name, *(const int *)ptr);
      break;
    case OPTIONS_VALUE_BOOL:
      yyjson_mut_obj_add_bool(doc, root, field->name, *(const bool *)ptr);
      break;
    case OPTIONS_VALUE_DOUBLE:
      yyjson_mut_obj_add_real(doc, root, field->name,
                              field->size == sizeof(float) ? *(const float *)ptr : *(const double *)ptr);
      break;
    case OPTIONS_VALUE_STRING:
      yyjson_mut_obj_add_str(doc, root, field->name, ptr);
      break;
    }
  }
  char *json = yyjson_mut_write_opts(doc, 0, &runtime_json_allocator, NULL, NULL);
  yyjson_mut_doc_free(doc);
  return json;
}
