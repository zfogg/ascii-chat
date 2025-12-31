/**
 * @file options_state.c
 * @brief 🔒 RCU-based thread-safe options state implementation
 */

#include "options/rcu.h"
#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "platform/abstraction.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

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
  if (g_options_initialized) {
    log_warn("Options state already initialized");
    return ASCIICHAT_OK;
  }

  // Initialize write mutex
  if (mutex_init(&g_options_write_mutex) != 0) {
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize options write mutex");
  }

  // Initialize deferred free mutex
  if (mutex_init(&g_deferred_free_mutex) != 0) {
    mutex_destroy(&g_options_write_mutex);
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize deferred free mutex");
  }

  // Allocate initial options struct (will be populated later by options_state_populate_from_globals)
  options_t *initial_opts = SAFE_MALLOC(sizeof(options_t), options_t *);
  if (!initial_opts) {
    mutex_destroy(&g_options_write_mutex);
    mutex_destroy(&g_deferred_free_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate initial options struct");
  }

  // Zero-initialize (all fields start at 0/false/NULL)
  memset(initial_opts, 0, sizeof(*initial_opts));

  // Publish initial struct (release semantics - make all fields visible to readers)
  atomic_store_explicit(&g_options, initial_opts, memory_order_release);

  g_options_initialized = true;
  log_debug("Options state initialized with RCU pattern");

  return ASCIICHAT_OK;
}

asciichat_error_t options_state_set(const options_t *opts) {
  if (!opts) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "opts is NULL");
  }

  if (!g_options_initialized) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Options state not initialized (call options_state_init first)");
  }

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
  if (!g_options_initialized) {
    return;
  }

  // Get current options pointer
  options_t *current = atomic_load_explicit(&g_options, memory_order_acquire);

  // Clear the atomic pointer (prevent further reads)
  atomic_store_explicit(&g_options, NULL, memory_order_release);

  // Free current struct
  SAFE_FREE(current);

  // Free all deferred structs
  deferred_free_all();

  // Destroy mutexes
  mutex_destroy(&g_options_write_mutex);
  mutex_destroy(&g_deferred_free_mutex);

  g_options_initialized = false;
  log_debug("Options state shutdown complete");
}

const options_t *options_get(void) {
  // Lock-free read with acquire semantics
  // Guarantees we see all writes made before the pointer was published
  options_t *current = atomic_load_explicit(&g_options, memory_order_acquire);

  // Should never be NULL after initialization
  if (!current) {
    log_fatal("Options not initialized! Call options_state_init() first");
    abort();
  }

  return current;
}

asciichat_error_t options_update(void (*updater)(options_t *, void *), void *context) {
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
// Convenience Setters
// ============================================================================

// Helper struct for passing multiple params to updater
struct dimensions_update_ctx {
  unsigned short int width;
  unsigned short int height;
};

static void dimensions_updater(options_t *opts, void *context) {
  struct dimensions_update_ctx *ctx = (struct dimensions_update_ctx *)context;
  opts->width = ctx->width;
  opts->height = ctx->height;
}

asciichat_error_t options_set_dimensions(unsigned short int width, unsigned short int height) {
  struct dimensions_update_ctx ctx = {.width = width, .height = height};
  return options_update(dimensions_updater, &ctx);
}

static void color_mode_updater(options_t *opts, void *context) {
  terminal_color_mode_t *mode = (terminal_color_mode_t *)context;
  opts->color_mode = *mode;
}

asciichat_error_t options_set_color_mode(terminal_color_mode_t mode) {
  return options_update(color_mode_updater, &mode);
}

static void render_mode_updater(options_t *opts, void *context) {
  render_mode_t *mode = (render_mode_t *)context;
  opts->render_mode = *mode;
}

asciichat_error_t options_set_render_mode(render_mode_t mode) {
  return options_update(render_mode_updater, &mode);
}

static void log_level_updater(options_t *opts, void *context) {
  log_level_t *level = (log_level_t *)context;
  opts->log_level = *level;
}

asciichat_error_t options_set_log_level(log_level_t level) {
  return options_update(log_level_updater, &level);
}
