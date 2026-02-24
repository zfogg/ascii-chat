/**
 * @file lib/platform/cond.c
 * @brief Platform-agnostic condition variable hooks and utilities
 * @ingroup platform
 *
 * This file provides hook functions that can be called from platform-specific
 * condition variable implementations to record events and debug information.
 */

#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>
#include <stdatomic.h>

/**
 * @brief Hook called when a thread waits on a condition variable
 * @param cond Pointer to the condition variable being waited on
 *
 * Called by platform-specific cond_wait() or cond_timedwait() before blocking.
 * Can be extended in the future to add more diagnostics or callbacks.
 *
 * @ingroup platform
 */
void cond_on_wait(cond_t *cond) {
  if (cond) {
    cond->last_wait_time_ns = time_get_ns();
    asciichat_thread_t current_thread = (asciichat_thread_t)asciichat_thread_current_id();
    cond->last_waiting_key = asciichat_thread_to_key(current_thread);
    atomic_fetch_add((volatile _Atomic(uint64_t) *)&cond->waiting_count, 1);
  }
}

/**
 * @brief Hook called when a condition variable is signaled
 * @param cond Pointer to the condition variable being signaled
 *
 * Called by platform-specific cond_signal() after waking one thread.
 * Can be extended in the future to add more diagnostics or callbacks.
 *
 * @ingroup platform
 */
void cond_on_signal(cond_t *cond) {
  if (cond) {
    cond->last_signal_time_ns = time_get_ns();
    if (cond->waiting_count > 0) {
      atomic_fetch_sub((volatile _Atomic(uint64_t) *)&cond->waiting_count, 1);
    }
  }
}

/**
 * @brief Hook called when a condition variable is broadcast
 * @param cond Pointer to the condition variable being broadcast
 *
 * Called by platform-specific cond_broadcast() after waking all threads.
 * Can be extended in the future to add more diagnostics or callbacks.
 *
 * @ingroup platform
 */
void cond_on_broadcast(cond_t *cond) {
  if (cond) {
    cond->last_broadcast_time_ns = time_get_ns();
    atomic_store((volatile _Atomic(uint64_t) *)&cond->waiting_count, 0);
  }
}
