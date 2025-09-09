#ifndef WINDOWS_NATIVE_H
#define WINDOWS_NATIVE_H

#ifdef NATIVE_WINDOWS

// Windows headers (order matters)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>
#include <conio.h>

// POSIX compatibility
#define sleep(x) Sleep((x) * 1000)
#define usleep(x) Sleep((x) / 1000)
#define close(x) closesocket(x)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define mkdir(x, y) _mkdir(x)
#define open _open
#define read _read
#define write _write

// File descriptors
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Socket options
#define MSG_NOSIGNAL 0
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

// Signals (Windows doesn't have all Unix signals)
#define SIGPIPE 13
#define SIGWINCH 28

// Native Windows threading
typedef struct {
  HANDLE handle;
  unsigned int id;
} win_thread_t;

typedef CRITICAL_SECTION win_mutex_t;
typedef CONDITION_VARIABLE win_cond_t;
typedef SRWLOCK win_rwlock_t;

// Thread functions
static inline int win_thread_create(win_thread_t *thread, void *(*func)(void *), void *arg) {
  thread->handle = (HANDLE)_beginthreadex(NULL, 0, (unsigned(__stdcall *)(void *))func, arg, 0, &thread->id);
  return thread->handle ? 0 : -1;
}

static inline int win_thread_join(win_thread_t *thread) {
  WaitForSingleObject(thread->handle, INFINITE);
  CloseHandle(thread->handle);
  return 0;
}

// Mutex functions
static inline void win_mutex_init(win_mutex_t *mutex) {
  InitializeCriticalSection(mutex);
}

static inline void win_mutex_destroy(win_mutex_t *mutex) {
  DeleteCriticalSection(mutex);
}

static inline void win_mutex_lock(win_mutex_t *mutex) {
  EnterCriticalSection(mutex);
}

static inline void win_mutex_unlock(win_mutex_t *mutex) {
  LeaveCriticalSection(mutex);
}

// Read-write lock functions
static inline void win_rwlock_init(win_rwlock_t *lock) {
  InitializeSRWLock(lock);
}

static inline void win_rwlock_destroy(win_rwlock_t *lock) {
  // No cleanup needed for SRWLOCK
}

static inline void win_rwlock_rdlock(win_rwlock_t *lock) {
  AcquireSRWLockShared(lock);
}

static inline void win_rwlock_wrlock(win_rwlock_t *lock) {
  AcquireSRWLockExclusive(lock);
}

static inline void win_rwlock_unlock(win_rwlock_t *lock) {
  // Try to release exclusive first, then shared
  ReleaseSRWLockExclusive(lock);
}

// Terminal functions
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
      return 0;
    }
  }
  return -1;
}

// Terminal settings (stub)
struct termios {
  DWORD mode;
};

#define TCSANOW 0
#define ECHO 0x0004
#define ICANON 0x0002

static inline int tcgetattr(int fd, struct termios *termios) {
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  return GetConsoleMode(h, &termios->mode) ? 0 : -1;
}

static inline int tcsetattr(int fd, int opt, struct termios *termios) {
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  return SetConsoleMode(h, termios->mode) ? 0 : -1;
}

// Network initialization
static inline int windows_network_init(void) {
  WSADATA wsaData;
  return WSAStartup(MAKEWORD(2, 2), &wsaData);
}

static inline void windows_network_cleanup(void) {
  WSACleanup();
}

// High-performance console output
static inline void enable_virtual_terminal(void) {
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  GetConsoleMode(hOut, &mode);
  mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(hOut, mode);
}

// Map pthread to Windows native (for easier porting)
#ifdef USE_NATIVE_THREADS
#define pthread_t win_thread_t
#define pthread_mutex_t win_mutex_t
#define pthread_cond_t win_cond_t
#define pthread_rwlock_t win_rwlock_t

#define pthread_create(t, a, f, arg) win_thread_create(t, f, arg)
#define pthread_join(t, r) win_thread_join(&t)
#define pthread_mutex_init(m, a) win_mutex_init(m)
#define pthread_mutex_destroy(m) win_mutex_destroy(m)
#define pthread_mutex_lock(m) win_mutex_lock(m)
#define pthread_mutex_unlock(m) win_mutex_unlock(m)
#define pthread_rwlock_init(l, a) win_rwlock_init(l)
#define pthread_rwlock_destroy(l) win_rwlock_destroy(l)
#define pthread_rwlock_rdlock(l) win_rwlock_rdlock(l)
#define pthread_rwlock_wrlock(l) win_rwlock_wrlock(l)
#define pthread_rwlock_unlock(l) win_rwlock_unlock(l)

#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_COND_INITIALIZER {0}
#endif

#else // !NATIVE_WINDOWS

// Use standard POSIX headers
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>

#define windows_network_init() 0
#define windows_network_cleanup() ((void)0)
#define enable_virtual_terminal() ((void)0)

#endif // NATIVE_WINDOWS

#endif // WINDOWS_NATIVE_H