#pragma once

/**
 * @file mutex.h
 * @brief Cross-platform mutex interface for ASCII-Chat
 *
 * This header provides a unified mutex interface that abstracts platform-specific
 * implementations (Windows Critical Sections vs POSIX pthread mutexes).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>

#ifdef _WIN32
#include "windows_compat.h"
typedef CRITICAL_SECTION mutex_t;
#else
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#endif

// ============================================================================
// Mutex Functions
// ============================================================================

int mutex_init(mutex_t *mutex);
int mutex_destroy(mutex_t *mutex);
int mutex_lock_impl(mutex_t *mutex);
int mutex_trylock(mutex_t *mutex);
int mutex_unlock_impl(mutex_t *mutex);

// Debug-enabled macros that capture caller context
// These macros ensure __FILE__, __LINE__, and __func__ resolve to the caller's location
// rather than the platform implementation file location
//
// In Release builds (NDEBUG defined), these call the implementation directly for zero overhead.
// In Debug builds, they conditionally use lock_debug if initialized.
#ifdef NDEBUG
// Release mode: Direct calls to implementation (no lock debugging overhead)
#define mutex_lock(mutex) mutex_lock_impl(mutex)
#define mutex_unlock(mutex) mutex_unlock_impl(mutex)
#else
// Debug mode: Use lock debugging when initialized
#define mutex_lock(mutex)                                                                                              \
  (lock_debug_is_initialized() ? debug_mutex_lock(mutex, __FILE__, __LINE__, __func__) : mutex_lock_impl(mutex))

#define mutex_unlock(mutex)                                                                                            \
  (lock_debug_is_initialized() ? debug_mutex_unlock(mutex, __FILE__, __LINE__, __func__) : mutex_unlock_impl(mutex))
#endif
