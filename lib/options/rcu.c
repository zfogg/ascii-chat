/**
 * @file options_state.c
 * @brief 🔒 RCU-based thread-safe options state implementation
 */

#include "options/rcu.h"
#include "options/schema.h"
#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "platform/abstraction.h"
#include "platform/init.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
#endif

// ============================================================================
// RCU State: Global Atomic Pointer
// ============================================================================

/**
 * @brief Global atomic pointer to current options
 *
 * This is the heart of the RCU pattern:
 * - Readers use atomic_load (lock-free, fast)
 * - Writers use atomic_exchange (serialized with mutex)
 * - Memory ordering: acquire/release for proper visibility
 */
static _Atomic(options_t *) g_options = NULL;

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
 * @brief Initialization flag
 */
static bool g_options_initialized = false;
// Mutex to protect initialization check (TOCTOU race prevention)
static static_mutex_t g_options_init_mutex = STATIC_MUTEX_INIT;
// Track the PID to detect fork() - after fork, child needs to reinitialize
static pid_t g_init_pid = -1;

/**
 * @brief Static default options used when options_get() is called before initialization
 * or during cleanup. This ensures atexit handlers can safely call GET_OPTION().
 *
 * Critical note: This must match the defaults in options_t_new(). Any field with a
 * non-zero default should be explicitly set here to avoid division-by-zero or other
 * issues when options haven't been initialized yet (e.g., during early startup or
 * atexit handlers).
 */
static const options_t g_default_options = (options_t){
    // Binary-Level Options (must match options_t_new() defaults)
    .help = OPT_HELP_DEFAULT,
    .version = OPT_VERSION_DEFAULT,

    // Logging
    .log_level = LOG_INFO,
    .quiet = false,
    .verbose_level = OPT_VERBOSE_LEVEL_DEFAULT,

    // Terminal Dimensions
    .width = OPT_WIDTH_DEFAULT,
    .height = OPT_HEIGHT_DEFAULT,
    .auto_width = OPT_AUTO_WIDTH_DEFAULT,
    .auto_height = OPT_AUTO_HEIGHT_DEFAULT,

    // Display
    .color_mode = COLOR_MODE_AUTO,
    .palette_type = PALETTE_STANDARD,
    .render_mode = RENDER_MODE_FOREGROUND,
    .fps = OPT_FPS_DEFAULT,

    // Performance
    .compression_level = OPT_COMPRESSION_LEVEL_DEFAULT,

    // Webcam
    .test_pattern = false,
    .webcam_index = OPT_WEBCAM_INDEX_DEFAULT,

    // Network
    .max_clients = OPT_MAX_CLIENTS_DEFAULT,
    .discovery_port = OPT_ACDS_PORT_INT_DEFAULT,
    .port = OPT_PORT_DEFAULT,

    // Audio
    .audio_enabled = OPT_AUDIO_ENABLED_DEFAULT,
    .microphone_index = OPT_MICROPHONE_INDEX_DEFAULT,
    .speakers_index = OPT_SPEAKERS_INDEX_DEFAULT,
    .microphone_sensitivity = OPT_MICROPHONE_SENSITIVITY_DEFAULT,
    .speakers_volume = OPT_SPEAKERS_VOLUME_DEFAULT,

    // All other fields are zero-initialized (empty strings, NULL, 0, false, etc.)
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
// Public API Implementation
// ============================================================================

asciichat_error_t options_state_init(void) {
  static_mutex_lock(&g_options_init_mutex);

  // Detect fork: after fork(), child process must reinitialize mutexes
  // Mutexes inherited from parent are in inconsistent state
  pid_t current_pid = getpid();
  if (g_options_initialized && g_init_pid == current_pid) {
    // Already initialized in this process
    static_mutex_unlock(&g_options_init_mutex);
    log_warn("Options state already initialized");
    return ASCIICHAT_OK;
  }

  // If we forked (pid changed), we need to clean up old state
  if (g_options_initialized && g_init_pid != current_pid) {
    log_debug("Detected fork (parent PID %d, child PID %d) - reinitializing options state", g_init_pid, current_pid);
    // Mutexes are in bad state after fork - they need to be destroyed and recreated
    // But we can't safely call mutex_destroy on inherited mutexes
    // Just reset the flags to force reinitialization
    g_options_initialized = false;
    g_init_pid = -1; // Reset so we initialize fresh
  }

  // Also check if this is the very first call in a forked child where parent was never initialized
  // In this case, g_options_initialized will be false but we might still have inherited invalid state
  if (!g_options_initialized && g_init_pid != -1 && g_init_pid != current_pid) {
    log_debug("First init in forked child - resetting inherited state (parent PID %d, child PID %d)", g_init_pid,
              current_pid);
    g_init_pid = -1; // Clear inherited parent's PID
  }

  // Initialize write mutex
  if (mutex_init(&g_options_write_mutex) != 0) {
    static_mutex_unlock(&g_options_init_mutex);
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize options write mutex");
  }

  // Initialize deferred free mutex
  if (mutex_init(&g_deferred_free_mutex) != 0) {
    mutex_destroy(&g_options_write_mutex);
    static_mutex_unlock(&g_options_init_mutex);
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize deferred free mutex");
  }

  // Allocate initial options struct (will be populated later by options_state_populate_from_globals)
  options_t *initial_opts = SAFE_MALLOC(sizeof(options_t), options_t *);
  if (!initial_opts) {
    mutex_destroy(&g_options_write_mutex);
    mutex_destroy(&g_deferred_free_mutex);
    static_mutex_unlock(&g_options_init_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate initial options struct");
  }

  // Initialize all defaults using options_t_new()
  *initial_opts = options_t_new();

  // Publish initial struct (release semantics - make all fields visible to readers)
  atomic_store_explicit(&g_options, initial_opts, memory_order_release);

  g_options_initialized = true;
  g_init_pid = current_pid; // Record PID to detect fork later
  static_mutex_unlock(&g_options_init_mutex);
  log_debug("Options state initialized with RCU pattern (PID %d)", current_pid);

  return ASCIICHAT_OK;
}

asciichat_error_t options_state_set(const options_t *opts) {
  if (!opts) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "opts is NULL");
  }

  // Check initialization flag under lock to prevent TOCTOU race
  static_mutex_lock(&g_options_init_mutex);
  if (!g_options_initialized) {
    static_mutex_unlock(&g_options_init_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Options state not initialized (call options_state_init first)");
  }
  static_mutex_unlock(&g_options_init_mutex);

  // Get current struct (should be the initial zero-initialized one during startup)
  options_t *current = atomic_load_explicit(&g_options, memory_order_acquire);

  // Copy provided options into current struct
  // Note: No need for RCU update here since this is called during initialization
  // before any reader threads exist
  memcpy(current, opts, sizeof(options_t));

  log_debug("Options state set from parsed struct");
  return ASCIICHAT_OK;
}

void options_state_shutdown(void) {
  // Check and clear initialization flag under lock
  static_mutex_lock(&g_options_init_mutex);
  if (!g_options_initialized) {
    static_mutex_unlock(&g_options_init_mutex);
    return;
  }
  g_options_initialized = false; // Clear flag first to prevent new operations
  static_mutex_unlock(&g_options_init_mutex);

  // Get current options pointer
  options_t *current = atomic_load_explicit(&g_options, memory_order_acquire);

  // Clear the atomic pointer (prevent further reads)
  atomic_store_explicit(&g_options, NULL, memory_order_release);

  // Free current struct
  SAFE_FREE(current);

  // Free all deferred structs
  deferred_free_all();

  // Cleanup schema (frees all dynamically allocated config strings)
  config_schema_cleanup();

  // Destroy dynamically created mutexes
  // NOTE: Do NOT destroy g_options_init_mutex - it's statically initialized
  // and needs to persist across shutdown/init cycles (especially in tests)
  mutex_destroy(&g_options_write_mutex);
  mutex_destroy(&g_deferred_free_mutex);

  log_debug("Options state shutdown complete");
}

void options_cleanup_schema(void) {
  config_schema_cleanup();
}

const options_t *options_get(void) {
  // Lock-free read with acquire semantics
  // Guarantees we see all writes made before the pointer was published
  options_t *current = atomic_load_explicit(&g_options, memory_order_acquire);

  // If options not yet published, return safe static default instead of crashing
  // This allows atexit handlers to safely call GET_OPTION() during cleanup
  if (!current) {
    // Return static default - safe for atexit handlers to read
    return (const options_t *)&g_default_options;
  }

  return current;
}

static asciichat_error_t options_update(void (*updater)(options_t *, void *), void *context) {
  if (!updater) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "updater function is NULL");
  }

  if (!g_options_initialized) {
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

struct int_field_ctx {
  const char *field_name;
  int value;
};

static void int_field_updater(options_t *opts, void *context) {
  struct int_field_ctx *ctx = (struct int_field_ctx *)context;
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
  else if (strcmp(ctx->field_name, "fps") == 0)
    opts->fps = ctx->value;
  else if (strcmp(ctx->field_name, "color_mode") == 0)
    opts->color_mode = (terminal_color_mode_t)ctx->value;
  else if (strcmp(ctx->field_name, "render_mode") == 0)
    opts->render_mode = (render_mode_t)ctx->value;
  else if (strcmp(ctx->field_name, "log_level") == 0)
    opts->log_level = (log_level_t)ctx->value;
}

asciichat_error_t options_set_int(const char *field_name, int value) {
  if (!field_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "field_name is NULL");
    return ERROR_INVALID_PARAM;
  }

  if (strcmp(field_name, "width") != 0 && strcmp(field_name, "height") != 0 && strcmp(field_name, "max_clients") != 0 &&
      strcmp(field_name, "compression_level") != 0 && strcmp(field_name, "reconnect_attempts") != 0 &&
      strcmp(field_name, "microphone_index") != 0 && strcmp(field_name, "speakers_index") != 0 &&
      strcmp(field_name, "discovery_port") != 0 && strcmp(field_name, "fps") != 0 &&
      strcmp(field_name, "color_mode") != 0 && strcmp(field_name, "render_mode") != 0 &&
      strcmp(field_name, "log_level") != 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Unknown integer field: %s", field_name);
    return ERROR_INVALID_PARAM;
  }

  struct int_field_ctx ctx = {.field_name = field_name, .value = value};
  return options_update(int_field_updater, &ctx);
}

struct bool_field_ctx {
  const char *field_name;
  bool value;
};

static void bool_field_updater(options_t *opts, void *context) {
  struct bool_field_ctx *ctx = (struct bool_field_ctx *)context;
  if (strcmp(ctx->field_name, "no_compress") == 0)
    opts->no_compress = ctx->value;
  else if (strcmp(ctx->field_name, "encode_audio") == 0)
    opts->encode_audio = ctx->value;
  else if (strcmp(ctx->field_name, "webcam_flip") == 0)
    opts->webcam_flip = ctx->value;
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
}

asciichat_error_t options_set_bool(const char *field_name, bool value) {
  if (!field_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "field_name is NULL");
    return ERROR_INVALID_PARAM;
  }

  // Validate field exists
  if (strcmp(field_name, "no_compress") != 0 && strcmp(field_name, "encode_audio") != 0 &&
      strcmp(field_name, "webcam_flip") != 0 && strcmp(field_name, "test_pattern") != 0 &&
      strcmp(field_name, "no_audio_mixer") != 0 && strcmp(field_name, "show_capabilities") != 0 &&
      strcmp(field_name, "force_utf8") != 0 && strcmp(field_name, "audio_enabled") != 0 &&
      strcmp(field_name, "audio_analysis_enabled") != 0 && strcmp(field_name, "audio_no_playback") != 0 &&
      strcmp(field_name, "stretch") != 0 && strcmp(field_name, "snapshot_mode") != 0 &&
      strcmp(field_name, "strip_ansi") != 0 && strcmp(field_name, "quiet") != 0 &&
      strcmp(field_name, "encrypt_enabled") != 0 && strcmp(field_name, "no_encrypt") != 0 &&
      strcmp(field_name, "discovery") != 0 && strcmp(field_name, "discovery_expose_ip") != 0 &&
      strcmp(field_name, "discovery_insecure") != 0 && strcmp(field_name, "webrtc") != 0 &&
      strcmp(field_name, "lan_discovery") != 0 && strcmp(field_name, "no_mdns_advertise") != 0 &&
      strcmp(field_name, "prefer_webrtc") != 0 && strcmp(field_name, "no_webrtc") != 0 &&
      strcmp(field_name, "webrtc_skip_stun") != 0 && strcmp(field_name, "webrtc_disable_turn") != 0 &&
      strcmp(field_name, "enable_upnp") != 0 && strcmp(field_name, "require_server_identity") != 0 &&
      strcmp(field_name, "require_client_identity") != 0 && strcmp(field_name, "require_server_verify") != 0 &&
      strcmp(field_name, "require_client_verify") != 0 && strcmp(field_name, "palette_custom_set") != 0 &&
      strcmp(field_name, "media_loop") != 0 && strcmp(field_name, "media_from_stdin") != 0 &&
      strcmp(field_name, "auto_width") != 0 && strcmp(field_name, "auto_height") != 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Unknown boolean field: %s", field_name);
    return ERROR_INVALID_PARAM;
  }

  struct bool_field_ctx ctx = {.field_name = field_name, .value = value};
  return options_update(bool_field_updater, &ctx);
}

struct string_field_ctx {
  const char *field_name;
  const char *value;
};

static void string_field_updater(options_t *opts, void *context) {
  struct string_field_ctx *ctx = (struct string_field_ctx *)context;
  if (strcmp(ctx->field_name, "address") == 0)
    SAFE_STRNCPY(opts->address, ctx->value, sizeof(opts->address));
  else if (strcmp(ctx->field_name, "address6") == 0)
    SAFE_STRNCPY(opts->address6, ctx->value, sizeof(opts->address6));
  else if (strcmp(ctx->field_name, "port") == 0)
    SAFE_STRNCPY(opts->port, ctx->value, sizeof(opts->port));
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
  if (strcmp(field_name, "address") != 0 && strcmp(field_name, "address6") != 0 && strcmp(field_name, "port") != 0 &&
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

  struct string_field_ctx ctx = {.field_name = field_name, .value = value};
  return options_update(string_field_updater, &ctx);
}

struct double_field_ctx {
  const char *field_name;
  double value;
};

static void double_field_updater(options_t *opts, void *context) {
  struct double_field_ctx *ctx = (struct double_field_ctx *)context;
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

  struct double_field_ctx ctx = {.field_name = internal_name, .value = value};
  return options_update(double_field_updater, &ctx);
}
