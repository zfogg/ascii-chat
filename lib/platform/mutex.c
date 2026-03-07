/**
 * @file lib/platform/mutex.c
 * @brief Platform-agnostic mutex hooks and utilities
 * @ingroup platform
 *
 * This file provides hook functions that can be called from platform-specific
 * mutex implementations to record events and debug information.
 * All hooks are debug-only and disabled in release builds.
 */

#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>
#include <stdint.h>
#include <stdio.h>

#ifndef NDEBUG

/**
 * @brief Hook called when a mutex is successfully locked
 * @param mutex Pointer to the mutex that was locked
 *
 * Called by platform-specific mutex_lock_impl() after the lock is acquired.
 * Records timing, held-by thread, and increments lock count.
 *
 * @ingroup platform
 */
void mutex_on_lock(mutex_t *mutex) {
  if (!mutex) return;
  mutex->last_lock_time_ns = time_get_ns();
  asciichat_thread_t current_thread = (asciichat_thread_t)asciichat_thread_current_id();
  mutex->currently_held_by_key = asciichat_thread_to_key(current_thread);
  mutex->lock_count++;
}

/**
 * @brief Hook called when a mutex is unlocked
 * @param mutex Pointer to the mutex that was unlocked
 *
 * Called by platform-specific mutex_unlock_impl() before releasing the lock.
 * Records timing and increments unlock count.
 *
 * @ingroup platform
 */
void mutex_on_unlock(mutex_t *mutex) {
  if (!mutex) return;
  mutex->last_unlock_time_ns = time_get_ns();
  mutex->currently_held_by_key = 0;
  mutex->unlock_count++;
}

/**
 * @brief Hook called when a mutex trylock is attempted
 * @param mutex Pointer to the mutex
 * @param success Whether the trylock succeeded
 *
 * Called by platform-specific mutex_trylock_impl() after the attempt.
 * Records timing and success/failure statistics.
 *
 * @ingroup platform
 */
void mutex_on_trylock(mutex_t *mutex, bool success) {
  if (!mutex) return;
  mutex->trylock_count++;
  if (success) {
    mutex->trylock_success_count++;
    mutex->last_lock_time_ns = time_get_ns();
    asciichat_thread_t current_thread = (asciichat_thread_t)asciichat_thread_current_id();
    mutex->currently_held_by_key = asciichat_thread_to_key(current_thread);
  }
}

/**
 * @brief Format mutex timing and state info into buffer
 * @param mutex Pointer to the mutex
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written
 *
 * Formats timing (lock/unlock), held-by info, and operation counts.
 * Called by mutex_log_state() and --sync-state display code.
 *
 * @ingroup platform
 */
int mutex_format_state(const mutex_t *mutex, char *buffer, size_t size) {
  if (!mutex || !buffer || size == 0) return 0;

  int offset = 0;
  uint64_t now_ns = time_get_ns();

  // Format timing information
  char lock_str[64] = "";
  char unlock_str[64] = "";
  char held_str[64] = "";
  char count_str[256] = "";

  if (mutex->last_lock_time_ns > 0 && mutex->last_lock_time_ns <= now_ns) {
    char elapsed_str[64];
    time_pretty(now_ns - mutex->last_lock_time_ns, -1, elapsed_str, sizeof(elapsed_str));
    snprintf(lock_str, sizeof(lock_str), "lock=%s", elapsed_str);
  }

  if (mutex->last_unlock_time_ns > 0 && mutex->last_unlock_time_ns <= now_ns) {
    char elapsed_str[64];
    time_pretty(now_ns - mutex->last_unlock_time_ns, -1, elapsed_str, sizeof(elapsed_str));
    snprintf(unlock_str, sizeof(unlock_str), "unlock=%s", elapsed_str);
  }

  if (mutex->currently_held_by_key != 0) {
    snprintf(held_str, sizeof(held_str), "[LOCKED_BY=thread.%lu]", (unsigned long)mutex->currently_held_by_key);
  }

  if (mutex->lock_count > 0 || mutex->unlock_count > 0 || mutex->trylock_count > 0) {
    snprintf(count_str, sizeof(count_str), "[ops: lock=%llu unlock=%llu trylock=%llu/%llu]",
             (unsigned long long)mutex->lock_count, (unsigned long long)mutex->unlock_count,
             (unsigned long long)mutex->trylock_success_count, (unsigned long long)mutex->trylock_count);
  }

  offset += snprintf(buffer + offset, size - offset, "%s %s %s %s", lock_str, unlock_str, held_str, count_str);
  return offset;
}

/**
 * @brief Log the state of a mutex
 * @param mutex Pointer to the mutex
 * @param file Source file of the caller
 * @param line Source line of the caller
 * @param func Source function of the caller
 *
 * Formats and logs detailed state information about the mutex.
 *
 * @ingroup platform
 */
void mutex_log_state(const mutex_t *mutex, const char *file, int line, const char *func) {
  if (!mutex) return;
  char buf[512];
  mutex_format_state(mutex, buf, sizeof(buf));
  log_msg(LOG_DEBUG, file, line, func, "mutex/state %p: %s", (const void *)mutex, buf);
}

#endif  // !NDEBUG
