#pragma once

/**
 * @file cond.h
 * @brief Cross-platform condition variable interface for ASCII-Chat
 *
 * This header provides a unified condition variable interface that abstracts platform-specific
 * implementations (Windows Condition Variables vs POSIX pthread condition variables).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>
#include "mutex.h"

#ifdef _WIN32
#include "windows_compat.h"
typedef CONDITION_VARIABLE cond_t;
#else
#include <pthread.h>
typedef pthread_cond_t cond_t;
#endif

// ============================================================================
// Condition Variable Functions
// ============================================================================

int cond_init(cond_t *cond);
int cond_destroy(cond_t *cond);
int cond_wait(cond_t *cond, mutex_t *mutex);
int cond_timedwait(cond_t *cond, mutex_t *mutex, int timeout_ms);
int cond_signal(cond_t *cond);
int cond_broadcast(cond_t *cond);
