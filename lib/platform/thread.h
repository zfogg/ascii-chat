#pragma once

/**
 * @file thread.h
 * @brief Cross-platform thread interface for ASCII-Chat
 *
 * This header provides a unified thread interface that abstracts platform-specific
 * implementations (Windows threads vs POSIX pthreads).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE asciithread_t;
typedef DWORD thread_id_t;
#else
#include <pthread.h>
typedef pthread_t asciithread_t;
typedef pthread_t thread_id_t;
#endif

// ============================================================================
// Thread Functions
// ============================================================================

int ascii_thread_create(asciithread_t *thread, void *(*func)(void *), void *arg);
int ascii_thread_join(asciithread_t *thread, void **retval);
int ascii_thread_join_timeout(asciithread_t *thread, void **retval, uint32_t timeout_ms);
void ascii_thread_exit(void *retval);
thread_id_t ascii_thread_self(void);
int ascii_thread_equal(thread_id_t t1, thread_id_t t2);
uint64_t ascii_thread_current_id(void);
bool ascii_thread_is_initialized(asciithread_t *thread);
