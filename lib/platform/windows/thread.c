/**
 * @file thread.c
 * @brief Windows thread implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides Windows Threading API wrappers for the platform abstraction layer,
 * enabling cross-platform thread management using a unified API.
 */

#ifdef _WIN32

#include "../abstraction.h"
#include "../../common.h"
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
    printf("[THREAD_WRAPPER] ERROR: NULL wrapper\n");
    fflush(stdout);
    return 1;
  }

  printf("[THREAD_WRAPPER] wrapper=%p, func=%p, arg=%p, thread_id=%lu\n", wrapper, wrapper->posix_func, wrapper->arg,
         GetCurrentThreadId());
  fflush(stdout);

  // Check if function pointer is valid
  if (!wrapper->posix_func) {
    printf("[THREAD_WRAPPER] ERROR: NULL function pointer!\n");
    fflush(stdout);
    free(wrapper);
    return 1;
  }

  printf("[THREAD_WRAPPER] About to call POSIX function...\n");
  fflush(stdout);

  void *result = NULL;
  __try {
    result = wrapper->posix_func(wrapper->arg);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    printf("[THREAD_WRAPPER] EXCEPTION caught! Code: 0x%lX\n", (unsigned long)GetExceptionCode());
    fflush(stdout);
    // Use raw free to match raw malloc used in allocation
#ifdef DEBUG_MEMORY
#undef free
    free(wrapper);
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#else
    free(wrapper);
#endif
    return 1;
  }

  printf("[THREAD_WRAPPER] Function returned: %p\n", result);
  fflush(stdout);

  // Use raw free to match raw malloc used in allocation
#ifdef DEBUG_MEMORY
#undef free
  free(wrapper);
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#else
  free(wrapper);
#endif
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
  printf("ENTER ascii_thread_create: thread=%p, func=%p, arg=%p\n", thread, func, arg);
  fflush(stdout);

#ifdef DEBUG_THREADS
  OutputDebugStringA("DEBUG: ascii_thread_create() called\n");
#endif

  printf("DEBUG: About to malloc wrapper (size=%zu)\n", sizeof(thread_wrapper_t));
  fflush(stdout);

  // CRITICAL: Use real malloc, not debug_malloc to avoid deadlock during thread creation
#ifdef DEBUG_MEMORY
#undef malloc
  thread_wrapper_t *wrapper = (thread_wrapper_t *)malloc(sizeof(thread_wrapper_t));
#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#else
  thread_wrapper_t *wrapper;
  SAFE_MALLOC(wrapper, sizeof(thread_wrapper_t), thread_wrapper_t *);
#endif

  printf("DEBUG: malloc returned wrapper=%p\n", wrapper);
  fflush(stdout);
  if (!wrapper) {
#ifdef DEBUG_THREADS
    OutputDebugStringA("DEBUG: malloc failed for thread wrapper\n");
#endif
    return -1;
  }

  wrapper->posix_func = func;
  wrapper->arg = arg;

#ifdef DEBUG_THREADS
  OutputDebugStringA("DEBUG: About to call CreateThread\n");
#endif

  DWORD thread_id;

  printf("[CREATE_THREAD] Before CreateThread: wrapper=%p, func=%p, arg=%p\n", wrapper, wrapper->posix_func,
         wrapper->arg);
  fflush(stdout);

  (*thread) = CreateThread(NULL, 0, windows_thread_wrapper, wrapper, 0, &thread_id);

  printf("[CREATE_THREAD] After CreateThread: handle=%p, thread_id=%lu\n", *thread, thread_id);
  fflush(stdout);

  if (*thread == NULL) {
    DWORD error = GetLastError();
    printf("[CREATE_THREAD] FAILED, error=%lu\n", error);
    fflush(stdout);
#ifdef DEBUG_THREADS
    char debug_msg[256];
    SAFE_SNPRINTF(debug_msg, 256, "DEBUG: CreateThread failed, error=%lu\n", error);
    OutputDebugStringA(debug_msg);
#endif
    free(wrapper);
    return -1;
  }

  printf("[CREATE_THREAD] SUCCESS: handle=%p, thread_id=%lu\n", *thread, thread_id);
  fflush(stdout);

#ifdef DEBUG_THREADS
  char debug_msg[256];
  SAFE_SNPRINTF(debug_msg, 256, "DEBUG: CreateThread succeeded, handle=%p, thread_id=%lu\n", *thread, thread_id);
  OutputDebugStringA(debug_msg);
#endif

  printf("DEBUG: ascii_thread_create about to return 0\n");
  fflush(stdout);

  printf("DEBUG: Actually returning from ascii_thread_create now\n");
  fflush(stdout);

  // IMPORTANT: Add a memory barrier to ensure all writes complete before returning
  MemoryBarrier();

  return 0;
}

/**
 * @brief Wait for a thread to complete and retrieve its return value
 * @param thread Pointer to thread to join
 * @param retval Pointer to store thread return value (can be NULL)
 * @return 0 on success, -1 on failure
 */
int ascii_thread_join(asciithread_t *thread, void **retval) {
  if (!thread || (*thread) == NULL || (*thread) == INVALID_HANDLE_VALUE) {
    return -1;
  }

  DWORD result = WaitForSingleObject((*thread), INFINITE);

  if (result == WAIT_OBJECT_0) {
    if (retval) {
      DWORD exit_code;
      GetExitCodeThread((*thread), &exit_code);
      *retval = (void *)(uintptr_t)exit_code;
    }
    CloseHandle((*thread));
    *thread = NULL; // Clear the handle to prevent reuse
    return 0;
  }
  return -1;
}

/**
 * @brief Join a thread with timeout
 * @param thread Thread handle to join
 * @param retval Optional return value from thread
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on timeout/error
 */
int ascii_thread_join_timeout(asciithread_t *thread, void **retval, uint32_t timeout_ms) {
  DWORD result = WaitForSingleObject((*thread), timeout_ms);

  if (result == WAIT_OBJECT_0) {
    if (retval) {
      DWORD exit_code;
      GetExitCodeThread((*thread), &exit_code);
      *retval = (void *)(uintptr_t)exit_code;
    }
    CloseHandle((*thread));
    return 0;
  } else if (result == WAIT_TIMEOUT) {
    return -2; // Special return code for timeout
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
