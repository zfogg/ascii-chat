/**
 * @file platform/windows/rwlock.c
 * @ingroup platform
 * @brief 🔓 Windows SRW Lock implementation for multi-reader/single-writer synchronization
 */

#ifdef _WIN32

#include <ascii-chat/platform/api.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/windows_compat.h>
#include <ascii-chat/debug/named.h>

/**
 * @brief Initialize a read-write lock with a name
 * @param lock Pointer to read-write lock structure to initialize
 * @param name Human-readable name for debugging
 * @return 0 on success, error code on failure
 */
int rwlock_init_impl(rwlock_t *lock) {
  InitializeSRWLock(&lock->impl);
  return 0;
}

int rwlock_init(rwlock_t *lock, const char *name) {
  InitializeSRWLock(&lock->impl);
  lock->name = NAMED_REGISTER(lock, name, "rwlock");
  return 0;
}

/**
 * @brief Destroy a read-write lock and free its resources
 * @param lock Pointer to read-write lock to destroy
 * @return 0 on success, error code on failure
 * @note SRWLocks don't need explicit destruction on Windows
 */
int rwlock_destroy_impl(rwlock_t *lock) {
  // SRWLocks don't need explicit destruction
  (void)lock; // Suppress unused parameter warning
  return 0;
}

int rwlock_destroy(rwlock_t *lock) {
  NAMED_UNREGISTER(lock);
  return rwlock_destroy_impl(lock);
}

/**
 * @brief Acquire a read lock (shared access) - implementation function
 * @param lock Pointer to read-write lock to acquire for reading
 * @return 0 on success, error code on failure
 */
int rwlock_rdlock_impl(rwlock_t *lock) {
  AcquireSRWLockShared(&lock->impl);
  rwlock_on_rdlock(lock);
  return 0;
}

/**
 * @brief Acquire a write lock (exclusive access) - implementation function
 * @param lock Pointer to read-write lock to acquire for writing
 * @return 0 on success, error code on failure
 */
int rwlock_wrlock_impl(rwlock_t *lock) {
  AcquireSRWLockExclusive(&lock->impl);
  rwlock_on_wrlock(lock);
  return 0;
}

/**
 * @brief Release a read lock (explicit read unlock) - implementation function
 * @param lock Pointer to read-write lock to release from read mode
 * @return 0 on success, error code on failure
 */
int rwlock_rdunlock_impl(rwlock_t *lock) {
  rwlock_on_unlock(lock);
  ReleaseSRWLockShared(&lock->impl);
  return 0;
}

/**
 * @brief Release a write lock (explicit write unlock) - implementation function
 * @param lock Pointer to read-write lock to release from write mode
 * @return 0 on success, error code on failure
 */
int rwlock_wrunlock_impl(rwlock_t *lock) {
  rwlock_on_unlock(lock);
  ReleaseSRWLockExclusive(&lock->impl);
  return 0;
}

#endif // _WIN32
