/**
 * @file lib/platform/mutex.c
 * @brief Platform-agnostic mutex hooks and utilities
 * @ingroup platform
 *
 * This file provides hook functions that can be called from platform-specific
 * mutex implementations to record events and debug information.
 */

#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>

/**
 * @brief Hook called when a mutex is successfully locked
 * @param mutex Pointer to the mutex that was locked
 *
 * Called by platform-specific mutex_lock_impl() after the lock is acquired.
 * Can be extended in the future to add more diagnostics or callbacks.
 *
 * @ingroup platform
 */
void mutex_on_lock(mutex_t *mutex) {
  if (mutex) {
    mutex->last_lock_time_ns = time_get_ns();
    mutex->currently_held_by_tid = (uint64_t)asciichat_thread_current_id();
  }
}

/**
 * @brief Hook called when a mutex is unlocked
 * @param mutex Pointer to the mutex that was unlocked
 *
 * Called by platform-specific mutex_unlock_impl() before releasing the lock.
 * Can be extended in the future to add more diagnostics or callbacks.
 *
 * @ingroup platform
 */
void mutex_on_unlock(mutex_t *mutex) {
  if (mutex) {
    mutex->last_unlock_time_ns = time_get_ns();
    mutex->currently_held_by_tid = 0;
  }
}
