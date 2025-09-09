#ifndef WINDOWS_COMPAT_H
#define WINDOWS_COMPAT_H

#ifdef WIN32

// Prevent MinGW from including pthread.h
#define _PTHREAD_H
#define HAVE_STRUCT_TIMESPEC

// Windows headers (order matters)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

// Define missing errno values if needed
#ifndef EBUSY
#define EBUSY 16
#endif

// ============================================================================
// POSIX Compatibility Mappings
// ============================================================================

// Basic functions
#define sleep(x) Sleep((x) * 1000)
#define usleep(x) Sleep((x) / 1000)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define mkdir(x, y) _mkdir(x)
#define getpid _getpid

// File descriptors
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Socket compatibility
#define close(x) closesocket(x)
typedef int socklen_t;
#define MSG_NOSIGNAL 0
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

// Signal handling (minimal stubs)
#define SIGINT 2
#define SIGTERM 15
#define SIGPIPE 13
#define SIGWINCH 28
typedef void (*sighandler_t)(int);

// ============================================================================
// Threading - pthread API using Windows native threads
// ============================================================================

// Thread types
typedef HANDLE pthread_t;
typedef DWORD pthread_attr_t;

// Mutex types
typedef CRITICAL_SECTION pthread_mutex_t;
typedef void *pthread_mutexattr_t;

// RW Lock types
typedef SRWLOCK pthread_rwlock_t;
typedef void *pthread_rwlockattr_t;

// Condition variable types
typedef CONDITION_VARIABLE pthread_cond_t;
typedef void *pthread_condattr_t;

// Thread functions
static inline int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *),
                                 void *arg) {
  (void)attr;
  *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start_routine, arg, 0, NULL);
  return (*thread == NULL) ? -1 : 0;
}

static inline int pthread_join(pthread_t thread, void **retval) {
  (void)retval;
  WaitForSingleObject(thread, INFINITE);
  CloseHandle(thread);
  return 0;
}

static inline void pthread_exit(void *retval) {
  ExitThread((DWORD)(uintptr_t)retval);
}

static inline pthread_t pthread_self(void) {
  return GetCurrentThread();
}

// Mutex functions
static inline int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
  (void)attr;
  InitializeCriticalSection(mutex);
  return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  DeleteCriticalSection(mutex);
  return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *mutex) {
  EnterCriticalSection(mutex);
  return 0;
}

static inline int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  return TryEnterCriticalSection(mutex) ? 0 : EBUSY;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  LeaveCriticalSection(mutex);
  return 0;
}

// RW Lock functions
static inline int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) {
  (void)attr;
  InitializeSRWLock(rwlock);
  return 0;
}

static inline int pthread_rwlock_destroy(pthread_rwlock_t *rwlock) {
  (void)rwlock;
  // SRWLock doesn't need explicit destruction
  return 0;
}

static inline int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
  AcquireSRWLockShared(rwlock);
  return 0;
}

static inline int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
  AcquireSRWLockExclusive(rwlock);
  return 0;
}

static inline int pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
  // Windows doesn't distinguish between read/write unlock
  // Try exclusive first (this is a limitation but works in practice)
  void *state = *(void **)rwlock;
  if (state == (void *)1) {
    ReleaseSRWLockExclusive(rwlock);
  } else {
    ReleaseSRWLockShared(rwlock);
  }
  return 0;
}

// Condition variable functions
static inline int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
  (void)attr;
  InitializeConditionVariable(cond);
  return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t *cond) {
  (void)cond;
  // Condition variables don't need explicit destruction on Windows
  return 0;
}

static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  SleepConditionVariableCS(cond, mutex, INFINITE);
  return 0;
}

static inline int pthread_cond_signal(pthread_cond_t *cond) {
  WakeConditionVariable(cond);
  return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t *cond) {
  WakeAllConditionVariable(cond);
  return 0;
}

// Static initializers
#define PTHREAD_MUTEX_INITIALIZER {(void *)-1, -1, 0, 0, 0, 0}
#define PTHREAD_RWLOCK_INITIALIZER {0}
#define PTHREAD_COND_INITIALIZER {0}

// ============================================================================
// Terminal I/O
// ============================================================================

// Terminal window size
struct winsize {
  unsigned short ws_row;
  unsigned short ws_col;
  unsigned short ws_xpixel;
  unsigned short ws_ypixel;
};

#define TIOCGWINSZ 0x5413

static inline int ioctl(int fd, unsigned long request, void *arg) {
  if (request == TIOCGWINSZ && arg) {
    struct winsize *ws = (struct winsize *)arg;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
      ws->ws_col = csbi.srWindow.Right - csbi.srWindow.Left + 1;
      ws->ws_row = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
      ws->ws_xpixel = 0;
      ws->ws_ypixel = 0;
      return 0;
    }
  }
  return -1;
}

// termios emulation (minimal)
struct termios {
  DWORD input_mode;
  DWORD output_mode;
  unsigned int c_lflag;
};

#define TCSANOW 0
#define ECHO 0x0004
#define ICANON 0x0002

static inline int tcgetattr(int fd, struct termios *termios_p) {
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  if (!GetConsoleMode(h, &termios_p->input_mode))
    return -1;
  h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!GetConsoleMode(h, &termios_p->output_mode))
    return -1;
  // Map Windows modes to termios flags
  termios_p->c_lflag = 0;
  if (termios_p->input_mode & ENABLE_ECHO_INPUT)
    termios_p->c_lflag |= ECHO;
  if (termios_p->input_mode & ENABLE_LINE_INPUT)
    termios_p->c_lflag |= ICANON;
  return 0;
}

static inline int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = termios_p->input_mode;

  // Map termios flags to Windows modes
  if (termios_p->c_lflag & ECHO)
    mode |= ENABLE_ECHO_INPUT;
  else
    mode &= ~ENABLE_ECHO_INPUT;

  if (termios_p->c_lflag & ICANON)
    mode |= ENABLE_LINE_INPUT;
  else
    mode &= ~ENABLE_LINE_INPUT;

  return SetConsoleMode(h, mode) ? 0 : -1;
}

// ============================================================================
// Network Initialization
// ============================================================================

static inline int windows_network_init(void) {
  WSADATA wsaData;
  return WSAStartup(MAKEWORD(2, 2), &wsaData);
}

static inline void windows_network_cleanup(void) {
  WSACleanup();
}

// Enable virtual terminal processing for ANSI codes
static inline void enable_virtual_terminal(void) {
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (GetConsoleMode(hOut, &mode)) {
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
  }
}

#else // !WIN32

// Standard POSIX headers
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <signal.h>

#define windows_network_init() 0
#define windows_network_cleanup() ((void)0)
#define enable_virtual_terminal() ((void)0)

#endif // WIN32

#endif // WINDOWS_COMPAT_H