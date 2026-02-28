/**
 * @file thread_pool.c
 * @brief 🧵 Generic thread pool implementation for managing worker threads
 */

#include <ascii-chat/thread_pool.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/debug/named.h>
#include <string.h>

#ifndef _WIN32
#include <signal.h>
#include <pthread.h>
#endif

/**
 * @brief Worker thread function for work queue mode
 *
 * Waits for work to be added to the queue, executes it, and loops.
 * Exits when pool->shutdown_requested is set.
 */
static void *thread_pool_worker_thread(void *arg) {
  thread_pool_t *pool = (thread_pool_t *)arg;
  if (!pool) {
    return NULL;
  }

  log_debug("[ThreadPool] Worker thread started for pool '%s'", pool->name);

  while (true) {
    thread_pool_work_entry_t *work = NULL;

    // Wait for work or shutdown signal
    log_debug("[ThreadPool] WORKER: About to lock pending_tasks for %s", pool->name);
    mutex_lock(&pool->work_queue_mutex);
    log_debug("[ThreadPool] WORKER: Locked pending_tasks for %s, checking for work", pool->name);

    // Loop until we get work or shutdown is requested
    while (!pool->work_queue && !pool->shutdown_requested) {
      cond_wait(&pool->work_available, &pool->work_queue_mutex);
    }

    // Check if we're shutting down
    if (pool->shutdown_requested && !pool->work_queue) {
      mutex_unlock(&pool->work_queue_mutex);
      log_debug("[ThreadPool] Worker thread exiting (shutdown requested)");
      break;
    }

    // Dequeue work
    if (pool->work_queue) {
      work = pool->work_queue;
      pool->work_queue = work->next;
    }

    mutex_unlock(&pool->work_queue_mutex);

    // Execute work (outside the lock)
    if (work) {
      if (work->work_func) {
        work->work_func(work->work_arg);
      }
      SAFE_FREE(work);
    }
  }

  return NULL;
}

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
  pool->is_work_queue_mode = false;
  pool->num_workers = 0;

  // Initialize mutex
  if (mutex_init(&pool->threads_mutex, "thread_pool") != 0) {
    SAFE_FREE(pool);
    SET_ERRNO(ERROR_THREAD, "Failed to initialize thread pool mutex");
    return NULL;
  }

  NAMED_REGISTER_THREAD_POOL(pool, pool->name);

  log_debug("Thread pool '%s' created (long-lived thread mode)", pool->name);
  return pool;
}

thread_pool_t *thread_pool_create_with_workers(const char *pool_name, size_t num_workers) {
  if (num_workers == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "num_workers must be > 0");
    return NULL;
  }

  thread_pool_t *pool = SAFE_MALLOC(sizeof(thread_pool_t), thread_pool_t *);
  if (!pool) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate thread pool");
    return NULL;
  }

  memset(pool, 0, sizeof(*pool));

  // Copy pool name
  if (pool_name) {
    SAFE_STRNCPY(pool->name, pool_name, sizeof(pool->name));
  } else {
    SAFE_STRNCPY(pool->name, "unnamed", sizeof(pool->name));
  }

  // Mark as work queue mode
  pool->is_work_queue_mode = true;
  pool->num_workers = num_workers;
  pool->work_queue = NULL;
  pool->shutdown_requested = false;

  // Initialize mutexes
  if (mutex_init(&pool->threads_mutex, "worker_list") != 0) {
    SAFE_FREE(pool);
    SET_ERRNO(ERROR_THREAD, "Failed to initialize threads_mutex");
    return NULL;
  }

  if (mutex_init(&pool->work_queue_mutex, "pending_tasks") != 0) {
    mutex_destroy(&pool->threads_mutex);
    SAFE_FREE(pool);
    SET_ERRNO(ERROR_THREAD, "Failed to initialize work_queue_mutex");
    return NULL;
  }

  // Initialize condition variable
  if (cond_init(&pool->work_available, "task_available") != 0) {
    mutex_destroy(&pool->work_queue_mutex);
    mutex_destroy(&pool->threads_mutex);
    SAFE_FREE(pool);
    SET_ERRNO(ERROR_THREAD, "Failed to initialize work_available condition");
    return NULL;
  }

  // Create worker threads
  for (size_t i = 0; i < num_workers; i++) {
    thread_pool_entry_t *entry = SAFE_MALLOC(sizeof(thread_pool_entry_t), thread_pool_entry_t *);
    if (!entry) {
      log_error("Failed to allocate worker entry %zu", i);
      thread_pool_destroy(pool);
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate worker entry");
      return NULL;
    }

    memset(entry, 0, sizeof(*entry));
    entry->stop_id = 0;
    entry->thread_func = thread_pool_worker_thread;
    entry->thread_arg = pool;
    SAFE_SNPRINTF(entry->name, sizeof(entry->name), "%s_worker_%zu", pool->name, i);

    // Create worker thread
    if (asciichat_thread_create(&entry->thread, entry->name, thread_pool_worker_thread, pool) != 0) {
      log_error("Failed to create worker thread %zu", i);
      SAFE_FREE(entry);
      thread_pool_destroy(pool);
      SET_ERRNO(ERROR_THREAD, "Failed to create worker thread %zu", i);
      return NULL;
    }

    // Add to thread list
    mutex_lock(&pool->threads_mutex);
    entry->next = pool->threads;
    pool->threads = entry;
    pool->thread_count++;
    mutex_unlock(&pool->threads_mutex);

    log_debug("[ThreadPool] Created worker thread %zu for pool '%s'", i, pool->name);
  }

  NAMED_REGISTER_THREAD_POOL(pool, pool->name);

  log_info("[ThreadPool] Created work queue pool '%s' with %zu worker threads", pool->name, num_workers);
  return pool;
}

void thread_pool_destroy(thread_pool_t *pool) {
  if (!pool) {
    return;
  }

  log_debug("Destroying thread pool '%s' (thread_count=%zu, work_queue_mode=%d)", pool->name, pool->thread_count,
            pool->is_work_queue_mode);

  // Stop all threads first (if not already stopped)
  if (pool->thread_count > 0) {
    log_debug("Thread pool '%s' has %zu threads, stopping them first", pool->name, pool->thread_count);
    thread_pool_stop_all(pool);
  }

  // Clean up work queue (in case there's pending work)
  if (pool->is_work_queue_mode) {
    mutex_lock(&pool->work_queue_mutex);
    thread_pool_work_entry_t *entry = pool->work_queue;
    while (entry) {
      thread_pool_work_entry_t *next = entry->next;
      SAFE_FREE(entry);
      entry = next;
    }
    pool->work_queue = NULL;
    mutex_unlock(&pool->work_queue_mutex);

    // Destroy condition variable and work queue mutex
    cond_destroy(&pool->work_available);
    mutex_destroy(&pool->work_queue_mutex);
  }

  // Destroy threads mutex
  mutex_destroy(&pool->threads_mutex);

  NAMED_UNREGISTER(pool);

  // Free pool
  SAFE_FREE(pool);

  log_debug("Thread pool destroyed");
}

asciichat_error_t thread_pool_queue_work(const char *name, thread_pool_t *pool, void *(*work_func)(void *),
                                         void *work_arg) {
  if (!name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "work name is required");
  }

  if (!pool) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "pool is NULL");
  }

  if (!pool->is_work_queue_mode) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Pool not in work queue mode. Use thread_pool_create_with_workers()");
  }

  if (!work_func) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "work_func is NULL");
  }

  // Allocate work entry
  thread_pool_work_entry_t *entry = SAFE_MALLOC(sizeof(thread_pool_work_entry_t), thread_pool_work_entry_t *);
  if (!entry) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate work entry");
  }

  entry->work_func = work_func;
  entry->work_arg = work_arg;
  entry->next = NULL;

  // Register work with debug naming system
  NAMED_REGISTER_THREADPOOL_WORK(entry, name);

  // Add to work queue (at the end)
  mutex_lock(&pool->work_queue_mutex);

  // Find the last entry in the queue
  if (!pool->work_queue) {
    pool->work_queue = entry;
  } else {
    thread_pool_work_entry_t *last = pool->work_queue;
    while (last->next) {
      last = last->next;
    }
    last->next = entry;
  }

  // Signal workers that work is available (WHILE HOLDING MUTEX to prevent lost wakeup race)
  cond_signal(&pool->work_available);

  mutex_unlock(&pool->work_queue_mutex);

  log_dev("Queued work to pool '%s' (workers=%zu)", pool->name, pool->num_workers);
  return ASCIICHAT_OK;
}

asciichat_error_t thread_pool_spawn(thread_pool_t *pool, void *(*thread_func)(void *), void *thread_arg, int stop_id,
                                    const char *thread_name) {
  if (!pool) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "pool is NULL");
  }

  if (pool->is_work_queue_mode) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot spawn threads in work queue mode. Use thread_pool_queue_work()");
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
  if (asciichat_thread_create(&entry->thread, entry->name, thread_func, thread_arg) != 0) {
    // Save name before freeing entry
    char thread_name_copy[64];
    SAFE_STRNCPY(thread_name_copy, entry->name, sizeof(thread_name_copy));
    SAFE_FREE(entry);
    return SET_ERRNO(ERROR_THREAD, "Failed to create thread '%s' in pool '%s'", thread_name_copy, pool->name);
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

  // For work queue mode, signal workers to shutdown (BEFORE releasing the mutex to prevent new work)
  if (pool->is_work_queue_mode) {
    mutex_lock(&pool->work_queue_mutex);
    pool->shutdown_requested = true;
    cond_broadcast(&pool->work_available);
    mutex_unlock(&pool->work_queue_mutex);
  }

  // Save thread list and clear it (WHILE holding mutex to prevent worker state changes)
  thread_pool_entry_t *threads_to_join = pool->threads;
  pool->threads = NULL;
  pool->thread_count = 0;

  // Release mutex BEFORE joining threads to prevent deadlock if workers need to acquire it
  mutex_unlock(&pool->threads_mutex);

  // Now join threads (WITHOUT holding the mutex to prevent deadlock)
  thread_pool_entry_t *entry = threads_to_join;
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

asciichat_error_t thread_pool_interrupt_all(thread_pool_t *pool, int sig) {
  if (!pool) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "pool is NULL");
  }

#ifndef _WIN32
  // Only available on POSIX systems (Unix/Linux/macOS)
  mutex_lock(&pool->threads_mutex);

  if (pool->thread_count == 0) {
    mutex_unlock(&pool->threads_mutex);
    log_debug("Thread pool '%s' has no threads to interrupt", pool->name);
    return ASCIICHAT_OK;
  }

  log_debug("Sending signal %d to %zu threads in pool '%s'", sig, pool->thread_count, pool->name);

  // Iterate through all threads and send signal
  thread_pool_entry_t *entry = pool->threads;
  int sent_count = 0;
  int failed_count = 0;

  while (entry) {
    // pthread_kill returns 0 on success, non-zero error code on failure
    if (pthread_kill(entry->thread, sig) != 0) {
      log_warn("Failed to send signal %d to thread '%s' in pool '%s'", sig, entry->name, pool->name);
      failed_count++;
    } else {
      sent_count++;
    }
    entry = entry->next;
  }

  mutex_unlock(&pool->threads_mutex);

  log_debug("Sent signal %d to %d/%zu threads in pool '%s' (%d failures)", sig, sent_count, pool->thread_count,
            pool->name, failed_count);

  if (failed_count > 0 && sent_count == 0) {
    return SET_ERRNO(ERROR_THREAD, "Failed to send signal to any threads in pool '%s'", pool->name);
  }

  return ASCIICHAT_OK;
#else
  // Windows: no pthread_kill equivalent needed - socket_shutdown() is sufficient
  (void)sig; // Unused on Windows
  log_debug("thread_pool_interrupt_all: no-op on Windows (socket shutdown is sufficient)");
  return ASCIICHAT_OK;
#endif
}
