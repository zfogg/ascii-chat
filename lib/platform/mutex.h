#pragma once

/**
 * @file mutex.h
 * @brief Cross-platform mutex interface for ASCII-Chat
 *
 * This header provides a unified mutex interface that abstracts platform-specific
 * implementations (Windows Critical Sections vs POSIX pthread mutexes).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION mutex_t;
#else
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#endif

// ============================================================================
// Mutex Functions
// ============================================================================

int mutex_init(mutex_t *mutex);
int mutex_destroy(mutex_t *mutex);
int mutex_lock(mutex_t *mutex);
int mutex_trylock(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);
