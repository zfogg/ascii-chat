/**
 * @file lib/platform/rwlock.c
 * @brief Platform-agnostic read-write lock hooks and utilities
 * @ingroup platform
 *
 * This file provides hook functions that can be called from platform-specific
 * rwlock implementations to record events and debug information.
 */

#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/util/time.h>

/**
 * @brief Hook called when a read lock is successfully acquired
 * @param rwlock Pointer to the rwlock that was read-locked
 *
 * Called by platform-specific rwlock_rdlock_impl() after the read lock is acquired.
 * Can be extended in the future to add more diagnostics or callbacks.
 *
 * @ingroup platform
 */
void rwlock_on_rdlock(rwlock_t *rwlock) {
  if (rwlock) {
    rwlock->last_rdlock_time_ns = time_get_ns();
  }
}

/**
 * @brief Hook called when a write lock is successfully acquired
 * @param rwlock Pointer to the rwlock that was write-locked
 *
 * Called by platform-specific rwlock_wrlock_impl() after the write lock is acquired.
 * Can be extended in the future to add more diagnostics or callbacks.
 *
 * @ingroup platform
 */
void rwlock_on_wrlock(rwlock_t *rwlock) {
  if (rwlock) {
    rwlock->last_wrlock_time_ns = time_get_ns();
  }
}

/**
 * @brief Hook called when an rwlock is unlocked (read or write)
 * @param rwlock Pointer to the rwlock that was unlocked
 *
 * Called by platform-specific rwlock_rdunlock_impl() or rwlock_wrunlock_impl()
 * before releasing the lock.
 * Can be extended in the future to add more diagnostics or callbacks.
 *
 * @ingroup platform
 */
void rwlock_on_unlock(rwlock_t *rwlock) {
  if (rwlock) {
    rwlock->last_unlock_time_ns = time_get_ns();
  }
}
