/**
 * @file thread.c
 * @brief Windows thread implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides Windows Threading API wrappers for the platform abstraction layer,
 * enabling cross-platform thread management using a unified API.
 */

#ifdef _WIN32

#include "../abstraction.h"
#include <windows.h>
#include <process.h>
#include <stdint.h>

// Thread wrapper structure to bridge POSIX and Windows thread APIs
typedef struct {
  void *(*posix_func)(void *);
  void *arg;
} thread_wrapper_t;

// Windows thread wrapper function that calls POSIX-style function
static DWORD WINAPI windows_thread_wrapper(LPVOID param) {
  thread_wrapper_t *wrapper = (thread_wrapper_t *)param;
  
  if (!wrapper) {
    return 1;
  }
  
  void *result = wrapper->posix_func(wrapper->arg);
  free(wrapper);
  return (DWORD)(uintptr_t)result;
}
/**
 * @brief Create a new thread
 * @param thread Pointer to thread structure to initialize
 * @param func Thread function to execute
 * @param arg Argument to pass to thread function
 * @return 0 on success, -1 on failure
 */
int ascii_thread_create(asciithread_t *thread, void *(*func)(void *), void *arg) {
  thread_wrapper_t *wrapper = malloc(sizeof(thread_wrapper_t));
  if (!wrapper) {
    return -1;
  }
  
  wrapper->posix_func = func;
  wrapper->arg = arg;
  
  DWORD thread_id;
  (*thread) = CreateThread(NULL, 0, windows_thread_wrapper, wrapper, 0, &thread_id);
  
  if (*thread == NULL) {
    free(wrapper);
    return -1;
  }
  
  return 0;
}

/**
 * @brief Wait for a thread to complete and retrieve its return value
 * @param thread Pointer to thread to join
 * @param retval Pointer to store thread return value (can be NULL)
 * @return 0 on success, -1 on failure
 */
int ascii_thread_join(asciithread_t *thread, void **retval) {
  DWORD result = WaitForSingleObject((*thread), INFINITE);
  
  if (result == WAIT_OBJECT_0) {
    if (retval) {
      DWORD exit_code;
      GetExitCodeThread((*thread), &exit_code);
      *retval = (void *)(uintptr_t)exit_code;
    }
    CloseHandle((*thread));
    return 0;
  }
  return -1;
}

/**
 * @brief Exit the current thread with a return value
 * @param retval Return value for the thread
 */
void ascii_thread_exit(void *retval) {
  ExitThread((DWORD)(uintptr_t)retval);
}

/**
 * @brief Detach a thread, allowing it to run independently
 * @param thread Pointer to thread to detach
 * @return 0 on success, -1 on failure
 */
int ascii_thread_detach(asciithread_t *thread) {
  CloseHandle((*thread));
  return 0;
}

/**
 * @brief Get the current thread's ID
 * @return Thread ID structure for current thread
 */
thread_id_t ascii_thread_self(void) {
  thread_id_t id;
  id = GetCurrentThreadId();
  return id;
}

/**
 * @brief Compare two thread IDs for equality
 * @param t1 First thread ID
 * @param t2 Second thread ID
 * @return Non-zero if equal, 0 if different
 */
int ascii_thread_equal(thread_id_t t1, thread_id_t t2) {
  return t1 == t2;
}

/**
 * @brief Get current thread ID as a 64-bit integer
 * @return Current thread ID as uint64_t
 */
uint64_t ascii_thread_current_id(void) {
  return (uint64_t)GetCurrentThreadId();
}

bool ascii_thread_is_initialized(asciithread_t *thread) {
  if (!thread)
    return false;
  // On Windows, check if thread handle is valid (not NULL and not INVALID_HANDLE_VALUE)
  return (*thread != NULL && *thread != INVALID_HANDLE_VALUE);
}

#endif // _WIN32