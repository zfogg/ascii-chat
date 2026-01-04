/**
 * @file thread_pool.c
 * @brief 🧵 Generic thread pool implementation for managing worker threads
 */

#include "thread_pool.h"
#include "common.h"
#include "log/logging.h"
#include "platform/thread.h"
#include <string.h>

thread_pool_t *thread_pool_create(const char *pool_name) {
  thread_pool_t *pool = SAFE_MALLOC(sizeof(thread_pool_t), thread_pool_t *);
  if (!pool) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate thread pool");
    return NULL;
  }

  memset(pool, 0, sizeof(*pool));

  // Copy pool name (truncate if necessary)
  if (pool_name) {
    SAFE_STRNCPY(pool->name, pool_name, sizeof(pool->name));
  } else {
    SAFE_STRNCPY(pool->name, "unnamed", sizeof(pool->name));
  }

  // Initialize linked list
  pool->threads = NULL;
  pool->thread_count = 0;

  // Initialize mutex
  if (mutex_init(&pool->threads_mutex) != 0) {
    SAFE_FREE(pool);
    SET_ERRNO(ERROR_THREAD, "Failed to initialize thread pool mutex");
    return NULL;
  }

  log_debug("Thread pool '%s' created", pool->name);
  return pool;
}

void thread_pool_destroy(thread_pool_t *pool) {
  if (!pool) {
    return;
  }

  log_debug("Destroying thread pool '%s' (thread_count=%zu)", pool->name, pool->thread_count);

  // Stop all threads first (if not already stopped)
  if (pool->thread_count > 0) {
    log_debug("Thread pool '%s' has %zu threads, stopping them first", pool->name, pool->thread_count);
    thread_pool_stop_all(pool);
  }

  // Destroy mutex
  mutex_destroy(&pool->threads_mutex);

  // Free pool
  SAFE_FREE(pool);

  log_debug("Thread pool destroyed");
}

asciichat_error_t thread_pool_spawn(thread_pool_t *pool, void *(*thread_func)(void *), void *thread_arg, int stop_id,
                                    const char *thread_name) {
  if (!pool) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "pool is NULL");
  }

  if (!thread_func) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "thread_func is NULL");
  }

  // Allocate thread entry
  thread_pool_entry_t *entry = SAFE_MALLOC(sizeof(thread_pool_entry_t), thread_pool_entry_t *);
  if (!entry) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate thread pool entry");
  }

  memset(entry, 0, sizeof(*entry));
  entry->stop_id = stop_id;
  entry->thread_func = thread_func;
  entry->thread_arg = thread_arg;
  entry->next = NULL;

  // Copy thread name (truncate if necessary)
  if (thread_name) {
    SAFE_STRNCPY(entry->name, thread_name, sizeof(entry->name));
  } else {
    // Auto-generate name
    SAFE_SNPRINTF(entry->name, sizeof(entry->name), "%s_worker_%d", pool->name, stop_id);
  }

  // Create thread
  if (asciichat_thread_create(&entry->thread, thread_func, thread_arg) != 0) {
    SAFE_FREE(entry);
    return SET_ERRNO(ERROR_THREAD, "Failed to create thread '%s' in pool '%s'", entry->name, pool->name);
  }

  // Add to thread list (sorted by stop_id)
  mutex_lock(&pool->threads_mutex);

  if (!pool->threads || pool->threads->stop_id > stop_id) {
    // Insert at head
    entry->next = pool->threads;
    pool->threads = entry;
  } else {
    // Find insertion point (maintain sorted order by stop_id)
    thread_pool_entry_t *prev = pool->threads;
    while (prev->next && prev->next->stop_id <= stop_id) {
      prev = prev->next;
    }
    entry->next = prev->next;
    prev->next = entry;
  }

  pool->thread_count++;
  mutex_unlock(&pool->threads_mutex);

  log_debug("Spawned thread '%s' (stop_id=%d) in pool '%s' (total_threads=%zu)", entry->name, stop_id, pool->name,
            pool->thread_count);

  return ASCIICHAT_OK;
}

asciichat_error_t thread_pool_stop_all(thread_pool_t *pool) {
  if (!pool) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "pool is NULL");
  }

  mutex_lock(&pool->threads_mutex);

  if (pool->thread_count == 0) {
    mutex_unlock(&pool->threads_mutex);
    log_debug("Thread pool '%s' has no threads to stop", pool->name);
    return ASCIICHAT_OK;
  }

  log_debug("Stopping %zu threads in pool '%s' in stop_id order", pool->thread_count, pool->name);

  // Threads are already sorted by stop_id (ascending), so just iterate and join
  thread_pool_entry_t *entry = pool->threads;
  while (entry) {
    log_debug("Joining thread '%s' (stop_id=%d) in pool '%s'", entry->name, entry->stop_id, pool->name);

    // Join thread (wait for it to exit)
    if (asciichat_thread_join(&entry->thread, NULL) != 0) {
      log_warn("Failed to join thread '%s' in pool '%s'", entry->name, pool->name);
    }

    thread_pool_entry_t *next = entry->next;
    SAFE_FREE(entry);
    entry = next;
  }

  // Clear list
  pool->threads = NULL;
  pool->thread_count = 0;

  mutex_unlock(&pool->threads_mutex);

  log_debug("All threads stopped in pool '%s'", pool->name);
  return ASCIICHAT_OK;
}

size_t thread_pool_get_count(const thread_pool_t *pool) {
  if (!pool) {
    return 0;
  }

  // Note: We're not locking here for performance
  // thread_count is a simple size_t which should be atomic on most platforms
  // If strict thread safety is required, use mutex_lock/unlock
  return pool->thread_count;
}

bool thread_pool_has_threads(const thread_pool_t *pool) {
  return thread_pool_get_count(pool) > 0;
}
