#ifndef PLATFORM_H
#define PLATFORM_H

// ============================================================================
// Platform Detection
// ============================================================================

#ifdef _WIN32
#define PLATFORM_WINDOWS 1
#define PLATFORM_POSIX 0
#else
#define PLATFORM_WINDOWS 0
#define PLATFORM_POSIX 1
#endif

// ============================================================================
// Standard Headers
// ============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

// ============================================================================
// Platform-Specific Headers
// ============================================================================

#if PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <io.h>
#include <direct.h>

// Missing types for MSVC compatibility
typedef long long ssize_t;

// POSIX function name compatibility
#define unlink _unlink
#define isatty _isatty
#define open _open
#define close _close
#define read _read
#define write _write

// Disable MSVC warnings for secure functions
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// MSVC doesn't support restrict keyword - use __restrict
#ifndef restrict
#define restrict __restrict
#endif

// MSVC doesn't support __attribute__((packed)) - use #pragma pack instead
#define PACKED_STRUCT_BEGIN __pragma(pack(push, 1))
#define PACKED_STRUCT_END __pragma(pack(pop))
#define PACKED_ATTR
#define ALIGNED_ATTR(x) __declspec(align(x))

// MSVC doesn't support GCC attributes - only define if not already defined
#ifndef __attribute__
#define __attribute__(x)
#endif

// Missing errno values
#ifndef EBUSY
#define EBUSY 16
#endif
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>

// GCC/Clang packed struct support
#define PACKED_STRUCT_BEGIN
#define PACKED_STRUCT_END
#define PACKED_ATTR __attribute__((packed))
#define ALIGNED_ATTR(x) __attribute__((aligned(x)))
#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#endif

// ============================================================================
// Threading Abstraction
// ============================================================================

// Use different name to avoid conflict with macOS system headers
typedef struct {
#if PLATFORM_WINDOWS
  HANDLE handle;
  DWORD id;
#else
  pthread_t thread;
#endif
} asciithread_t;

// Provide compatibility typedef if not conflicting
#ifndef __APPLE__
typedef asciithread_t thread_t;
#endif

typedef struct {
#if PLATFORM_WINDOWS
  CRITICAL_SECTION cs;
#else
  pthread_mutex_t mutex;
#endif
} mutex_t;

typedef struct {
#if PLATFORM_WINDOWS
  SRWLOCK lock;
#else
  pthread_rwlock_t lock;
#endif
} rwlock_t;

typedef struct {
#if PLATFORM_WINDOWS
  CONDITION_VARIABLE cv;
#else
  pthread_cond_t cond;
#endif
} cond_t;

// Thread ID type
typedef struct {
#if PLATFORM_WINDOWS
  DWORD id;
#else
  pthread_t thread;
#endif
} thread_id_t;

// Thread functions
int ascii_thread_create(asciithread_t *thread, void *(*func)(void *), void *arg);
int ascii_thread_join(asciithread_t *thread, void **retval);
void ascii_thread_exit(void *retval);
thread_id_t ascii_thread_self(void);
int ascii_thread_equal(thread_id_t t1, thread_id_t t2);
uint64_t ascii_thread_current_id(void);

// Helper to check if thread is initialized
static inline bool ascii_thread_is_initialized(asciithread_t *thread) {
#if PLATFORM_WINDOWS
  return thread->handle != NULL;
#else
  return thread->thread != 0;
#endif
}

// Mutex functions
int mutex_init(mutex_t *mutex);
int mutex_destroy(mutex_t *mutex);
int mutex_lock(mutex_t *mutex);
int mutex_trylock(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);

// RW lock functions
int rwlock_init(rwlock_t *lock);
int rwlock_destroy(rwlock_t *lock);
int rwlock_rdlock(rwlock_t *lock);
int rwlock_wrlock(rwlock_t *lock);
int rwlock_unlock(rwlock_t *lock);

// Condition variable functions
int cond_init(cond_t *cond);
int cond_destroy(cond_t *cond);
int cond_wait(cond_t *cond, mutex_t *mutex);
int cond_timedwait(cond_t *cond, mutex_t *mutex, int timeout_ms);
int cond_signal(cond_t *cond);
int cond_broadcast(cond_t *cond);

// ============================================================================
// Socket Abstraction
// ============================================================================

#if PLATFORM_WINDOWS
typedef SOCKET socket_t;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#define SOCKET_ERROR_VALUE SOCKET_ERROR
#define GET_SOCKET_ERROR() WSAGetLastError()
#define WOULD_BLOCK_ERROR WSAEWOULDBLOCK
#define IN_PROGRESS_ERROR WSAEINPROGRESS
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH
#else
typedef int socket_t;
#define INVALID_SOCKET_VALUE -1
#define SOCKET_ERROR_VALUE -1
#define GET_SOCKET_ERROR() errno
#define WOULD_BLOCK_ERROR EWOULDBLOCK
#define IN_PROGRESS_ERROR EINPROGRESS
#define closesocket close
#ifndef __APPLE__
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#endif
#endif

// Define ssize_t for Windows
#if PLATFORM_WINDOWS
typedef SSIZE_T ssize_t;
#endif

// Poll/select support
#if PLATFORM_WINDOWS
// Windows SDK 10.0.17763.0+ has pollfd in winsock2.h
// Check if it's already defined
#ifdef _WINSOCK2API_
// winsock2.h already included, it has pollfd
#else
// Define our own pollfd if winsock2.h not included
#ifndef _POLLFD_DEFINED
#define _POLLFD_DEFINED
struct pollfd {
  SOCKET fd;
  short events;
  short revents;
};
#endif
#endif

// nfds_t might not be defined
#ifndef _NFDS_T_DEFINED
#define _NFDS_T_DEFINED
typedef unsigned long nfds_t;
#endif

// Poll event flags might not be defined
#ifndef POLLIN
#define POLLIN 0x0001
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020
#endif
#else
#include <poll.h>
#endif

// Socket functions
int socket_init(void);
void socket_cleanup(void);
socket_t socket_create(int domain, int type, int protocol);
int socket_close(socket_t sock);
int socket_bind(socket_t sock, const struct sockaddr *addr, socklen_t addrlen);
int socket_listen(socket_t sock, int backlog);
socket_t socket_accept(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);
int socket_connect(socket_t sock, const struct sockaddr *addr, socklen_t addrlen);
ssize_t socket_send(socket_t sock, const void *buf, size_t len, int flags);
ssize_t socket_recv(socket_t sock, void *buf, size_t len, int flags);
ssize_t socket_sendto(socket_t sock, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                      socklen_t addrlen);
ssize_t socket_recvfrom(socket_t sock, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int socket_setsockopt(socket_t sock, int level, int optname, const void *optval, socklen_t optlen);
int socket_getsockopt(socket_t sock, int level, int optname, void *optval, socklen_t *optlen);
int socket_shutdown(socket_t sock, int how);
int socket_getpeername(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);
int socket_getsockname(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);
int socket_set_blocking(socket_t sock);
int socket_set_nonblocking(socket_t sock, bool nonblocking);
int socket_set_reuseaddr(socket_t sock, bool reuse);
int socket_set_nodelay(socket_t sock, bool nodelay);
int socket_set_keepalive(socket_t sock, bool keepalive);
int socket_get_error(socket_t sock);
int socket_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int socket_get_fd(socket_t sock);
int socket_is_valid(socket_t sock);

// ============================================================================
// Process/System Functions
// ============================================================================

// Sleep functions
void platform_sleep_ms(unsigned int ms);
void platform_sleep_us(unsigned int us);

// Process functions
int platform_get_pid(void);
const char *platform_get_username(void);

// Signal handling
typedef void (*signal_handler_t)(int);
signal_handler_t platform_signal(int sig, signal_handler_t handler);

// ============================================================================
// Terminal I/O
// ============================================================================

typedef struct {
  int rows;
  int cols;
} terminal_size_t;

// Terminal functions
#undef write // Temporarily undefine to avoid conflicts with function declarations
int terminal_get_size(terminal_size_t *size);
int terminal_set_raw_mode(bool enable);
int terminal_set_echo(bool enable);
bool terminal_supports_color(void);
bool terminal_supports_unicode(void);
int terminal_clear_screen(void);
int terminal_move_cursor(int row, int col);
void terminal_enable_ansi(void);
#if PLATFORM_WINDOWS
#define write _write // Re-define after declarations on Windows only
#endif

// File/TTY functions
int platform_isatty(int fd);
const char *platform_get_tty_path(void);
int platform_open_tty(const char *mode);

// ============================================================================
// Environment Variables
// ============================================================================

const char *platform_getenv(const char *name);
int platform_setenv(const char *name, const char *value);

// ============================================================================
// File I/O
// ============================================================================

#if PLATFORM_WINDOWS
#define PATH_SEPARATOR "\\"
#define PATH_SEPARATOR_CHAR '\\'
// Redefine POSIX names to Windows equivalents
#define open _open
#define close _close
#define read _read
#define write _write
#define lseek _lseek
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_RDWR _O_RDWR
#define O_CREAT _O_CREAT
#define O_TRUNC _O_TRUNC
#define O_APPEND _O_APPEND
#else
#define PATH_SEPARATOR "/"
#define PATH_SEPARATOR_CHAR '/'
#endif

// ============================================================================
// Time Functions
// ============================================================================

#if PLATFORM_WINDOWS
// Windows SDK 10.0.17763.0+ defines timespec in time.h
// Only define if using older SDK
#if !defined(_TIMESPEC_DEFINED)
// Check if we're using an older Windows SDK that doesn't have timespec
#if !defined(_MSC_VER) || (_MSC_VER < 1920)
#define _TIMESPEC_DEFINED
#define HAVE_STRUCT_TIMESPEC
struct timespec {
  time_t tv_sec;
  long tv_nsec;
};
#endif
#endif

// Clock IDs for clock_gettime
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

// Function declarations
int clock_gettime(int clk_id, struct timespec *tp);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
int usleep(unsigned int usec);
#endif

// ============================================================================
// Memory Functions
// ============================================================================

#if PLATFORM_WINDOWS
void *aligned_alloc(size_t alignment, size_t size);
#define aligned_free(ptr) _aligned_free(ptr)
#else
#define aligned_free(ptr) free(ptr)
#endif

// ============================================================================
// Utility Macros
// ============================================================================

// String comparison
#if PLATFORM_WINDOWS
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

// Directory creation
#if PLATFORM_WINDOWS
#define mkdir(path, mode) _mkdir(path)
#endif

// Standard file descriptors
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#endif // PLATFORM_H