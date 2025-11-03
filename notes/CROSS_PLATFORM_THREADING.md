# How Cross-Platform Projects Handle Threading

## Common Approaches

### 1. Abstraction Layer (Most Common)
Projects like **SDL**, **Qt**, **libuv**, **Chromium** create a thin abstraction:

```c
// thread.h - Platform-agnostic interface
typedef struct thread thread_t;
typedef struct mutex mutex_t;

thread_t* thread_create(void* (*func)(void*), void* arg);
void thread_join(thread_t* thread);
mutex_t* mutex_create(void);
void mutex_lock(mutex_t* mutex);
void mutex_unlock(mutex_t* mutex);
```

Then implement per platform:
- `thread_win32.c` - Windows implementation
- `thread_posix.c` - Unix/Linux/macOS implementation

### 2. Conditional Typedef (Simple & Effective)
Projects like **OpenSSL**, **cURL**, **nginx** use conditional compilation:

```c
#ifdef _WIN32
  #include <windows.h>
  typedef HANDLE thread_t;
  typedef CRITICAL_SECTION mutex_t;
  #define thread_create(t, func, arg) ((*(t) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(func), (arg), 0, NULL)) != NULL)
#else
  #include <pthread.h>
  typedef pthread_t thread_t;
  typedef pthread_mutex_t mutex_t;
  #define thread_create(t, func, arg) (pthread_create((t), NULL, (func), (arg)) == 0)
#endif
```

### 3. C11 Threads (Modern Standard)
Since C11, there's standard threading:
```c
#include <threads.h>
thrd_t thread;
mtx_t mutex;
thrd_create(&thread, func, arg);
mtx_lock(&mutex);
```
**Problem**: Windows doesn't fully support C11 threads natively.

### 4. Use Existing Library
- **pthreads-win32**: Full pthread implementation for Windows
- **TinyCThread**: Lightweight C11 threads for all platforms
- **libuv**: Node.js's cross-platform async I/O (includes threading)

## Real-World Examples

### Redis
Uses simple abstraction:
```c
// bio.c
#ifdef _WIN32
    thread_id = CreateThread(NULL, 0, bioProcessBackgroundJobs, arg, 0, NULL);
#else
    pthread_create(&thread, NULL, bioProcessBackgroundJobs, arg);
#endif
```

### SQLite
Minimal abstraction with optional threading:
```c
typedef struct sqlite3_mutex sqlite3_mutex;
// Platform-specific implementations in mutex_w32.c, mutex_unix.c
```

### FFmpeg
Wrapper approach:
```c
// thread.h
typedef struct AVThread AVThread;
int av_thread_create(AVThread **thread, void *(*func)(void*), void *arg);

// Implementations in thread_pthread.c, thread_win32.c
```

### NGINX
Direct platform code with macros:
```c
#if (NGX_THREADS)
  #if (NGX_WIN32)
    typedef HANDLE ngx_tid_t;
  #else
    typedef pthread_t ngx_tid_t;
  #endif
#endif
```

## Recommended Approach for ascii-chat

### Option 1: Simple Macros (Minimal Changes)
```c
// In windows_compat.h
#ifdef WIN32
  #define THREAD_HANDLE HANDLE
  #define MUTEX_TYPE CRITICAL_SECTION
  #define THREAD_CREATE(handle, func, arg) \
    ((handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL)) != NULL)
  #define THREAD_JOIN(handle) WaitForSingleObject(handle, INFINITE)
  #define MUTEX_INIT(m) InitializeCriticalSection(&m)
  #define MUTEX_LOCK(m) EnterCriticalSection(&m)
  #define MUTEX_UNLOCK(m) LeaveCriticalSection(&m)
#else
  #define THREAD_HANDLE pthread_t
  #define MUTEX_TYPE pthread_mutex_t
  #define THREAD_CREATE(handle, func, arg) \
    (pthread_create(&handle, NULL, func, arg) == 0)
  #define THREAD_JOIN(handle) pthread_join(handle, NULL)
  #define MUTEX_INIT(m) pthread_mutex_init(&m, NULL)
  #define MUTEX_LOCK(m) pthread_mutex_lock(&m)
  #define MUTEX_UNLOCK(m) pthread_mutex_unlock(&m)
#endif
```

### Option 2: Typedef with Same Names (No Code Changes)
```c
// In windows_compat.h
#ifdef WIN32
  typedef HANDLE pthread_t;
  typedef CRITICAL_SECTION pthread_mutex_t;

  static inline int pthread_create(pthread_t *thread, void *attr,
                                   void *(*func)(void*), void *arg) {
    *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return (*thread == NULL) ? -1 : 0;
  }

  static inline int pthread_join(pthread_t thread, void **retval) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
  }

  // ... implement all pthread functions used
#endif
```

## Performance Considerations

Windows native threads vs pthreads-win32:
- **CreateThread**: ~5-10% faster thread creation
- **CRITICAL_SECTION**: ~15-20% faster than pthread_mutex on Windows
- **SRWLock**: ~30% faster than pthread_rwlock for read-heavy workloads
- **Condition Variables**: Similar performance

## ascii-chat Specific Needs

The project uses:
- `pthread_create/join` - Basic thread lifecycle
- `pthread_mutex_*` - Mutual exclusion
- `pthread_rwlock_*` - Read-write locks
- `pthread_cond_*` - Condition variables (for audio/video sync)

All of these map cleanly to Windows equivalents.
