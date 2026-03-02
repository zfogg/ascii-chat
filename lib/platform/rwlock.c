/**
 * @file lib/platform/rwlock.c
 * @brief Platform-agnostic read-write lock hooks and utilities
 * @ingroup platform
 *
 * This file provides hook functions that can be called from platform-specific
 * rwlock implementations to record events and debug information.
 * All hooks are debug-only and disabled in release builds.
 */

#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/log/log.h>
#include <stdio.h>

#ifndef NDEBUG

/**
 * @brief Hook called when a read lock is successfully acquired
 * @param rwlock Pointer to the rwlock that was read-locked
 *
 * Called by platform-specific rwlock_rdlock_impl() after the read lock is acquired.
 * Records timing, increments current reader count and total rdlock count.
 *
 * @ingroup platform
 */
void rwlock_on_rdlock(rwlock_t *rwlock) {
  if (!rwlock) return;
  rwlock->last_rdlock_time_ns = time_get_ns();
  atomic_fetch_add_u64(&rwlock->read_lock_count, 1);
  rwlock->rdlock_count++;
}

/**
 * @brief Hook called when a write lock is successfully acquired
 * @param rwlock Pointer to the rwlock that was write-locked
 *
 * Called by platform-specific rwlock_wrlock_impl() after the write lock is acquired.
 * Records timing, held-by thread, and increments total wrlock count.
 *
 * @ingroup platform
 */
void rwlock_on_wrlock(rwlock_t *rwlock) {
  if (!rwlock) return;
  rwlock->last_wrlock_time_ns = time_get_ns();
  asciichat_thread_t current_thread = (asciichat_thread_t)asciichat_thread_current_id();
  rwlock->write_held_by_key = asciichat_thread_to_key(current_thread);
  rwlock->wrlock_count++;
}

/**
 * @brief Hook called when an rwlock is unlocked (read or write)
 * @param rwlock Pointer to the rwlock that was unlocked
 *
 * Called by platform-specific rwlock_rdunlock_impl() or rwlock_wrunlock_impl()
 * before releasing the lock. Decrements appropriate reader/writer count and increments total unlock count.
 *
 * @ingroup platform
 */
void rwlock_on_unlock(rwlock_t *rwlock) {
  if (!rwlock) return;
  rwlock->last_unlock_time_ns = time_get_ns();
  rwlock->unlock_count++;
  asciichat_thread_t current_thread = (asciichat_thread_t)asciichat_thread_current_id();
  uintptr_t current_key = asciichat_thread_to_key(current_thread);
  if (rwlock->write_held_by_key == current_key) {
    rwlock->write_held_by_key = 0;
  } else if (atomic_load_u64(&rwlock->read_lock_count) > 0) {
    atomic_fetch_sub_u64(&rwlock->read_lock_count, 1);
  }
}

/**
 * @brief Format rwlock timing and state info into buffer
 * @param rwlock Pointer to the rwlock
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written
 *
 * Formats timing (rdlock/wrlock/unlock), held-by info, and operation counts.
 * Called by rwlock_log_state() and --sync-state display code.
 *
 * @ingroup platform
 */
int rwlock_format_state(const rwlock_t *rwlock, char *buffer, size_t size) {
  if (!rwlock || !buffer || size == 0) return 0;

  int offset = 0;
  uint64_t now_ns = time_get_ns();

  char rdlock_str[64] = "";
  char wrlock_str[64] = "";
  char unlock_str[64] = "";
  char held_str[64] = "";
  char count_str[256] = "";

  if (rwlock->last_rdlock_time_ns > 0 && rwlock->last_rdlock_time_ns <= now_ns) {
    char elapsed_str[64];
    time_pretty(now_ns - rwlock->last_rdlock_time_ns, -1, elapsed_str, sizeof(elapsed_str));
    snprintf(rdlock_str, sizeof(rdlock_str), "rdlock=%s", elapsed_str);
  }

  if (rwlock->last_wrlock_time_ns > 0 && rwlock->last_wrlock_time_ns <= now_ns) {
    char elapsed_str[64];
    time_pretty(now_ns - rwlock->last_wrlock_time_ns, -1, elapsed_str, sizeof(elapsed_str));
    snprintf(wrlock_str, sizeof(wrlock_str), "wrlock=%s", elapsed_str);
  }

  if (rwlock->last_unlock_time_ns > 0 && rwlock->last_unlock_time_ns <= now_ns) {
    char elapsed_str[64];
    time_pretty(now_ns - rwlock->last_unlock_time_ns, -1, elapsed_str, sizeof(elapsed_str));
    snprintf(unlock_str, sizeof(unlock_str), "unlock=%s", elapsed_str);
  }

  uint64_t read_count = atomic_load_u64(&rwlock->read_lock_count);
  if (rwlock->write_held_by_key != 0) {
    snprintf(held_str, sizeof(held_str), "[WRITE_LOCKED_BY=thread.%lu]", (unsigned long)rwlock->write_held_by_key);
  } else if (read_count > 0) {
    snprintf(held_str, sizeof(held_str), "[READ_LOCKED=%lu]", read_count);
  }

  if (rwlock->rdlock_count > 0 || rwlock->wrlock_count > 0 || rwlock->unlock_count > 0) {
    snprintf(count_str, sizeof(count_str), "[ops: rdlock=%lu wrlock=%lu unlock=%lu]",
             rwlock->rdlock_count, rwlock->wrlock_count, rwlock->unlock_count);
  }

  offset += snprintf(buffer + offset, size - offset, "%s %s %s %s %s",
                     rdlock_str, wrlock_str, unlock_str, held_str, count_str);
  return offset;
}

/**
 * @brief Log the state of a read-write lock
 * @param rwlock Pointer to the rwlock
 * @param file Source file of the caller
 * @param line Source line of the caller
 * @param func Source function of the caller
 *
 * Formats and logs detailed state information about the rwlock.
 *
 * @ingroup platform
 */
void rwlock_log_state(const rwlock_t *rwlock, const char *file, int line, const char *func) {
  if (!rwlock) return;
  char buf[512];
  rwlock_format_state(rwlock, buf, sizeof(buf));
  log_msg(LOG_DEBUG, file, line, func, "rwlock/state %p: %s", (const void *)rwlock, buf);
}

#endif  // !NDEBUG
