#pragma once

/**
 * @file rwlock.h
 * @brief Cross-platform read-write lock interface for ASCII-Chat
 *
 * This header provides a unified read-write lock interface that abstracts platform-specific
 * implementations (Windows SRW Locks vs POSIX pthread read-write locks).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
typedef SRWLOCK rwlock_t;
#else
#include <pthread.h>
typedef pthread_rwlock_t rwlock_t;
#endif

// ============================================================================
// Read-Write Lock Functions
// ============================================================================

int rwlock_init(rwlock_t *lock);
int rwlock_destroy(rwlock_t *lock);
int rwlock_rdlock(rwlock_t *lock);
int rwlock_wrlock(rwlock_t *lock);
int rwlock_unlock(rwlock_t *lock);
