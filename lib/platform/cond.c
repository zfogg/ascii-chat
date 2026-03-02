/**
 * @file lib/platform/cond.c
 * @brief Platform-agnostic condition variable hooks and utilities
 * @ingroup platform
 *
 * This file provides hook functions that can be called from platform-specific
 * condition variable implementations to record events and debug information.
 * All hooks are debug-only and disabled in release builds.
 */

#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/log/log.h>
#include <stdio.h>

#ifndef NDEBUG

/**
 * @brief Hook called when a thread waits on a condition variable
 * @param cond Pointer to the condition variable being waited on
 * @param mutex Pointer to the associated mutex
 * @param file Source file of the wait callsite (for deadlock detection)
 * @param line Source line of the wait callsite (for deadlock detection)
 * @param func Source function of the wait callsite (for deadlock detection)
 *
 * Called by platform-specific cond_wait() or cond_timedwait() before blocking.
 * Records timing, callsite information, and increments wait count.
 *
 * @ingroup platform
 */
void cond_on_wait(cond_t *cond, mutex_t *mutex, const char *file, int line, const char *func) {
  if (!cond) return;
  cond->last_wait_time_ns = time_get_ns();
  cond->last_wait_mutex = mutex;
  cond->last_wait_file = file;
  cond->last_wait_line = line;
  cond->last_wait_func = func;
  asciichat_thread_t current_thread = (asciichat_thread_t)asciichat_thread_current_id();
  cond->last_waiting_key = asciichat_thread_to_key(current_thread);
  atomic_fetch_add_u64(&cond->waiting_count, 1);
  cond->wait_count++;
}

/**
 * @brief Hook called when a condition variable is signaled
 * @param cond Pointer to the condition variable being signaled
 *
 * Called by platform-specific cond_signal() after waking one thread.
 * Records timing and decrements waiting count.
 *
 * @ingroup platform
 */
void cond_on_signal(cond_t *cond) {
  if (!cond) return;
  cond->last_signal_time_ns = time_get_ns();
  cond->signal_count++;
  if (atomic_load_u64(&cond->waiting_count) > 0) {
    atomic_fetch_sub_u64(&cond->waiting_count, 1);
  }
}

/**
 * @brief Hook called when a condition variable is broadcast
 * @param cond Pointer to the condition variable being broadcast
 *
 * Called by platform-specific cond_broadcast() after waking all threads.
 * Records timing and broadcasts count.
 *
 * @ingroup platform
 */
void cond_on_broadcast(cond_t *cond) {
  if (!cond) return;
  cond->last_broadcast_time_ns = time_get_ns();
  cond->broadcast_count++;
  atomic_store_u64(&cond->waiting_count, 0);
}

/**
 * @brief Format condition variable timing and state info into buffer
 * @param cond Pointer to the condition variable
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written
 *
 * Formats timing (wait/signal/broadcast), waiting threads, and operation counts.
 * Called by cond_log_state() and --sync-state display code.
 *
 * @ingroup platform
 */
int cond_format_state(const cond_t *cond, char *buffer, size_t size) {
  if (!cond || !buffer || size == 0) return 0;

  int offset = 0;
  uint64_t now_ns = time_get_ns();

  char wait_str[64] = "";
  char signal_str[64] = "";
  char broadcast_str[64] = "";
  char waiting_str[64] = "";
  char count_str[256] = "";

  if (cond->last_wait_time_ns > 0 && cond->last_wait_time_ns <= now_ns) {
    char elapsed_str[64];
    time_pretty(now_ns - cond->last_wait_time_ns, -1, elapsed_str, sizeof(elapsed_str));
    snprintf(wait_str, sizeof(wait_str), "wait=%s", elapsed_str);
  }

  if (cond->last_signal_time_ns > 0 && cond->last_signal_time_ns <= now_ns) {
    char elapsed_str[64];
    time_pretty(now_ns - cond->last_signal_time_ns, -1, elapsed_str, sizeof(elapsed_str));
    snprintf(signal_str, sizeof(signal_str), "signal=%s", elapsed_str);
  }

  if (cond->last_broadcast_time_ns > 0 && cond->last_broadcast_time_ns <= now_ns) {
    char elapsed_str[64];
    time_pretty(now_ns - cond->last_broadcast_time_ns, -1, elapsed_str, sizeof(elapsed_str));
    snprintf(broadcast_str, sizeof(broadcast_str), "broadcast=%s", elapsed_str);
  }

  uint64_t waiting_count = atomic_load_u64(&cond->waiting_count);
  if (waiting_count > 0) {
    snprintf(waiting_str, sizeof(waiting_str), "[WAITING=%lu]", waiting_count);
  }

  if (cond->wait_count > 0 || cond->signal_count > 0 || cond->broadcast_count > 0) {
    snprintf(count_str, sizeof(count_str), "[ops: wait=%lu signal=%lu broadcast=%lu]",
             cond->wait_count, cond->signal_count, cond->broadcast_count);
  }

  offset += snprintf(buffer + offset, size - offset, "%s %s %s %s %s",
                     wait_str, signal_str, broadcast_str, waiting_str, count_str);
  return offset;
}

/**
 * @brief Log the state of a condition variable
 * @param cond Pointer to the condition variable
 * @param file Source file of the caller
 * @param line Source line of the caller
 * @param func Source function of the caller
 *
 * Formats and logs detailed state information about the condition variable.
 *
 * @ingroup platform
 */
void cond_log_state(const cond_t *cond, const char *file, int line, const char *func) {
  if (!cond) return;
  char buf[512];
  cond_format_state(cond, buf, sizeof(buf));
  log_msg(LOG_DEBUG, file, line, func, "cond/state %p: %s", (const void *)cond, buf);
}

#endif  // !NDEBUG
