#pragma once

/**
 * @file rwlock.h
 * @brief Cross-platform read-write lock interface for ASCII-Chat
 *
 * This header provides a unified read-write lock interface that abstracts platform-specific
 * implementations (Windows SRW Locks vs POSIX pthread read-write locks).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>

#ifdef _WIN32
#include "windows_compat.h"
typedef SRWLOCK rwlock_t;
#else
#include <pthread.h>
typedef pthread_rwlock_t rwlock_t;
#endif

// Forward declarations for lock debugging
// Note: Full lock_debug.h cannot be included here due to circular dependencies
// Files that use both rwlock and lock_debug must include lock_debug.h separately
#ifndef NDEBUG
bool lock_debug_is_initialized(void);
int debug_rwlock_rdlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);
int debug_rwlock_wrlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);
int debug_rwlock_rdunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);
int debug_rwlock_wrunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);
#endif

// ============================================================================
// Read-Write Lock Functions
// ============================================================================

int rwlock_init(rwlock_t *lock);
int rwlock_destroy(rwlock_t *lock);
int rwlock_init_impl(rwlock_t *lock);
int rwlock_destroy_impl(rwlock_t *lock);
int rwlock_rdlock_impl(rwlock_t *lock);
int rwlock_wrlock_impl(rwlock_t *lock);
int rwlock_rdunlock_impl(rwlock_t *lock);
int rwlock_wrunlock_impl(rwlock_t *lock);

// Debug-enabled macros that capture caller context
// These macros ensure __FILE__, __LINE__, and __func__ resolve to the caller's location
// rather than the platform implementation file location
//
// In Release builds (NDEBUG defined), these call the implementation directly for zero overhead.
// In Debug builds, they conditionally use lock_debug if initialized.
#ifdef NDEBUG
// Release mode: Direct calls to implementation (no lock debugging overhead)
#define rwlock_rdlock(lock) rwlock_rdlock_impl(lock)
#define rwlock_wrlock(lock) rwlock_wrlock_impl(lock)
#define rwlock_rdunlock(lock) rwlock_rdunlock_impl(lock)
#define rwlock_wrunlock(lock) rwlock_wrunlock_impl(lock)
#else
// Debug mode: Use lock debugging when initialized
#define rwlock_rdlock(lock)                                                                                            \
  (lock_debug_is_initialized() ? debug_rwlock_rdlock(lock, __FILE__, __LINE__, __func__) : rwlock_rdlock_impl(lock))

#define rwlock_wrlock(lock)                                                                                            \
  (lock_debug_is_initialized() ? debug_rwlock_wrlock(lock, __FILE__, __LINE__, __func__) : rwlock_wrlock_impl(lock))

#define rwlock_rdunlock(lock)                                                                                          \
  (lock_debug_is_initialized() ? debug_rwlock_rdunlock(lock, __FILE__, __LINE__, __func__) : rwlock_rdunlock_impl(lock))

#define rwlock_wrunlock(lock)                                                                                          \
  (lock_debug_is_initialized() ? debug_rwlock_wrunlock(lock, __FILE__, __LINE__, __func__) : rwlock_wrunlock_impl(lock))
#endif
