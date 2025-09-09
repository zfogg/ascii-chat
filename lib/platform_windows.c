#ifdef _WIN32

#include "platform.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <signal.h>

// Thread implementation
int ascii_thread_create(asciithread_t *thread, void *(*func)(void *), void *arg) {
  thread->handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, &thread->id);
  return (thread->handle != NULL) ? 0 : -1;
}

int ascii_thread_join(asciithread_t *thread, void **retval) {
  if (WaitForSingleObject(thread->handle, INFINITE) == WAIT_OBJECT_0) {
    if (retval) {
      DWORD exit_code;
      GetExitCodeThread(thread->handle, &exit_code);
      *retval = (void *)(uintptr_t)exit_code;
    }
    CloseHandle(thread->handle);
    return 0;
  }
  return -1;
}

void ascii_thread_exit(void *retval) {
  ExitThread((DWORD)(uintptr_t)retval);
}

int ascii_thread_detach(asciithread_t *thread) {
  CloseHandle(thread->handle);
  return 0;
}

thread_id_t ascii_thread_self(void) {
  thread_id_t id;
  id.id = GetCurrentThreadId();
  return id;
}

int ascii_thread_equal(thread_id_t t1, thread_id_t t2) {
  return t1.id == t2.id;
}

// Mutex implementation
int mutex_init(mutex_t *mutex) {
  InitializeCriticalSection(&mutex->cs);
  return 0;
}

int mutex_destroy(mutex_t *mutex) {
  DeleteCriticalSection(&mutex->cs);
  return 0;
}

int mutex_lock(mutex_t *mutex) {
  EnterCriticalSection(&mutex->cs);
  return 0;
}

int mutex_trylock(mutex_t *mutex) {
  return TryEnterCriticalSection(&mutex->cs) ? 0 : EBUSY;
}

int mutex_unlock(mutex_t *mutex) {
  LeaveCriticalSection(&mutex->cs);
  return 0;
}

// Read-write lock implementation
int rwlock_init(rwlock_t *lock) {
  InitializeSRWLock(&lock->lock);
  return 0;
}

int rwlock_destroy(rwlock_t *lock) {
  // SRWLocks don't need explicit destruction
  (void)lock; // Suppress unused parameter warning
  return 0;
}

int rwlock_rdlock(rwlock_t *lock) {
  AcquireSRWLockShared(&lock->lock);
  return 0;
}

int rwlock_wrlock(rwlock_t *lock) {
  AcquireSRWLockExclusive(&lock->lock);
  return 0;
}

int rwlock_unlock(rwlock_t *lock) {
  // Try to release as writer first, then as reader
  // This is a limitation of SRWLock - we don't track lock type
  ReleaseSRWLockExclusive(&lock->lock);
  return 0;
}

int rwlock_rdunlock(rwlock_t *lock) {
  ReleaseSRWLockShared(&lock->lock);
  return 0;
}

int rwlock_wrunlock(rwlock_t *lock) {
  ReleaseSRWLockExclusive(&lock->lock);
  return 0;
}

// Condition variable implementation
int cond_init(cond_t *cond) {
  InitializeConditionVariable(&cond->cv);
  return 0;
}

int cond_destroy(cond_t *cond) {
  // Condition variables don't need explicit destruction on Windows
  (void)cond; // Suppress unused parameter warning
  return 0;
}

int cond_wait(cond_t *cond, mutex_t *mutex) {
  return SleepConditionVariableCS(&cond->cv, &mutex->cs, INFINITE) ? 0 : -1;
}

int cond_timedwait(cond_t *cond, mutex_t *mutex, int timeout_ms) {
  return SleepConditionVariableCS(&cond->cv, &mutex->cs, timeout_ms) ? 0 : -1;
}

int cond_signal(cond_t *cond) {
  WakeConditionVariable(&cond->cv);
  return 0;
}

int cond_broadcast(cond_t *cond) {
  WakeAllConditionVariable(&cond->cv);
  return 0;
}

// Terminal I/O implementation
int terminal_get_size(terminal_size_t *size) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }

  if (GetConsoleScreenBufferInfo(h, &csbi)) {
    size->cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    size->rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 0;
  }

  return -1;
}

const char *get_tty_path(void) {
  return "CON";
}

// Environment variable handling
const char *get_username_env(void) {
  static char username[256];
  const char *user = getenv("USERNAME");
  if (!user) {
    user = getenv("USER");
  }
  if (user) {
    strncpy(username, user, sizeof(username) - 1);
    username[sizeof(username) - 1] = '\0';
    return username;
  }
  return "unknown";
}

// Platform initialization
int platform_init(void) {
  // Set binary mode for stdin/stdout to handle raw data
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);

  // Initialize Winsock will be done in socket_windows.c
  return 0;
}

void platform_cleanup(void) {
  // Cleanup will be done in socket_windows.c for Winsock
}

// Static mutex functions are implemented as inline in platform_init.h

// clock_gettime implementation for Windows
int clock_gettime(int clk_id, struct timespec *tp) {
  LARGE_INTEGER freq, counter;
  (void)clk_id; // Unused parameter

  if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter)) {
    return -1;
  }

  // Convert to seconds and nanoseconds
  tp->tv_sec = counter.QuadPart / freq.QuadPart;
  tp->tv_nsec = ((counter.QuadPart % freq.QuadPart) * 1000000000) / freq.QuadPart;

  return 0;
}

// aligned_alloc implementation for Windows
void *aligned_alloc(size_t alignment, size_t size) {
  return _aligned_malloc(size, alignment);
}

// gmtime_r implementation for Windows (thread-safe gmtime)
struct tm *gmtime_r(const time_t *timep, struct tm *result) {
  errno_t err = gmtime_s(result, timep);
  if (err != 0) {
    return NULL;
  }
  return result;
}

// Additional platform functions
uint64_t ascii_thread_current_id(void) {
  return (uint64_t)GetCurrentThreadId();
}

void platform_sleep_ms(unsigned int ms) {
  Sleep(ms);
}

void platform_sleep_us(unsigned int us) {
  // Windows Sleep only supports milliseconds, so convert
  Sleep((us + 999) / 1000);
}

int usleep(unsigned int usec) {
  // Use the platform function
  platform_sleep_us(usec);
  return 0;
}

int platform_get_pid(void) {
  return (int)GetCurrentProcessId();
}

const char *platform_get_username(void) {
  return get_username_env();
}

signal_handler_t platform_signal(int sig, signal_handler_t handler) {
  return signal(sig, handler);
}

const char *platform_getenv(const char *name) {
  return getenv(name);
}

int platform_setenv(const char *name, const char *value) {
  return _putenv_s(name, value);
}

int platform_isatty(int fd) {
  return _isatty(fd);
}

const char *platform_get_tty_path(void) {
  return get_tty_path();
}

int platform_open_tty(const char *mode) {
  (void)mode; // Unused on Windows
  // On Windows, we use CON for console access
  return _open("CON", _O_RDWR);
}

// Terminal functions
int terminal_set_raw_mode(bool enable) {
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  if (hStdin == INVALID_HANDLE_VALUE)
    return -1;

  DWORD mode;
  if (!GetConsoleMode(hStdin, &mode))
    return -1;

  if (enable) {
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
  } else {
    mode |= (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
  }

  return SetConsoleMode(hStdin, mode) ? 0 : -1;
}

int terminal_set_echo(bool enable) {
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  if (hStdin == INVALID_HANDLE_VALUE)
    return -1;

  DWORD mode;
  if (!GetConsoleMode(hStdin, &mode))
    return -1;

  if (enable) {
    mode |= ENABLE_ECHO_INPUT;
  } else {
    mode &= ~ENABLE_ECHO_INPUT;
  }

  return SetConsoleMode(hStdin, mode) ? 0 : -1;
}

bool terminal_supports_color(void) {
  // Windows 10+ supports ANSI colors
  return true;
}

bool terminal_supports_unicode(void) {
  // Windows supports Unicode through wide character APIs
  return true;
}

int terminal_clear_screen(void) {
  system("cls");
  return 0;
}

int terminal_move_cursor(int row, int col) {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hConsole == INVALID_HANDLE_VALUE)
    return -1;

  COORD coord;
  coord.X = (SHORT)col;
  coord.Y = (SHORT)row;

  return SetConsoleCursorPosition(hConsole, coord) ? 0 : -1;
}

void terminal_enable_ansi(void) {
  // Enable ANSI escape sequences on Windows 10+
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE) {
    DWORD mode;
    if (GetConsoleMode(hOut, &mode)) {
      mode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
      SetConsoleMode(hOut, mode);
    }
  }
}

#endif // _WIN32