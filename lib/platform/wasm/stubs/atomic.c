/**
 * @file lib/platform/wasm/stubs/atomic.c
 * @brief Atomic debug stubs for WASM
 */

#include <stdbool.h>

// Stub for debug_atomic_is_initialized - always return true in WASM
bool debug_atomic_is_initialized(void) {
  return true;
}

// Stubs for debug_sync functions
bool debug_sync_is_initialized(void) {
  return true;
}

void debug_sync_mutex_lock(const char *file, int line) {
  (void)file;
  (void)line;
  // No-op for WASM
}

void debug_sync_mutex_unlock(const char *file, int line) {
  (void)file;
  (void)line;
  // No-op for WASM
}

bool debug_sync_mutex_trylock(const char *file, int line) {
  (void)file;
  (void)line;
  // Always succeed in WASM
  return true;
}

void debug_sync_print_state(void) {
  // No-op for WASM
}

// Stubs for named registry
typedef struct { int dummy; } named_registry_t;

named_registry_t g_debug_entries = { 0 };

void named_register(const char *name, void *ptr) {
  (void)name;
  (void)ptr;
  // No-op for WASM
}

void named_register_fd(int fd, const char *name) {
  (void)fd;
  (void)name;
  // No-op for WASM
}
