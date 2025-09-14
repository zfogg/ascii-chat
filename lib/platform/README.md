# Platform Abstraction Layer

## Overview

The platform abstraction layer provides a unified, cross-platform API that enables ASCII-Chat to run seamlessly on Windows, Linux, and macOS. This layer abstracts platform-specific functionality into a common interface, allowing the main application code to remain platform-independent.

## Architecture

### File Structure

```
lib/platform/
├── README.md           # This file
├── abstraction.h       # Main abstraction header - includes all component headers
├── abstraction.c       # Common implementation (minimal)
├── init.h              # Platform initialization and static synchronization helpers
├── internal.h          # Internal helpers for implementation files
├── thread.h            # Thread management interface
├── mutex.h             # Mutex interface
├── rwlock.h            # Read-write lock interface
├── cond.h              # Condition variable interface
├── socket.h            # Socket interface
├── terminal.h          # Terminal I/O interface
├── system.h            # System functions interface
├── string.h            # String manipulation interface
├── file.h              # File I/O interface
├── posix/              # POSIX implementation (Linux/macOS)
│   ├── thread.c        # POSIX pthread implementation
│   ├── mutex.c         # POSIX mutex implementation
│   ├── rwlock.c        # POSIX read-write lock implementation
│   ├── cond.c          # POSIX condition variable implementation
│   ├── terminal.c      # POSIX terminal I/O implementation
│   ├── system.c        # POSIX system functions implementation
│   └── socket.c        # POSIX socket implementation
└── windows/            # Windows implementation
    ├── thread.c        # Windows thread implementation
    ├── mutex.c         # Windows mutex (Critical Section) implementation
    ├── rwlock.c        # Windows read-write lock (SRW Lock) implementation
    ├── cond.c          # Windows condition variable implementation
    ├── terminal.c      # Windows terminal (Console API) implementation
    ├── system.c        # Windows system functions implementation
    └── socket.c        # Windows socket (Winsock2) implementation
```

### Key Components

#### Core Headers (Modular Design)

1. **abstraction.h** - Main include file that brings in all platform headers
2. **thread.h** - Threading primitives (threads, thread IDs)
3. **mutex.h** - Mutual exclusion locks
4. **rwlock.h** - Read-write locks for concurrent access
5. **cond.h** - Condition variables for thread synchronization
6. **socket.h** - Network socket operations
7. **terminal.h** - Terminal I/O and control
8. **system.h** - System functions (process, environment, signals, TTY)
9. **string.h** - Safe string manipulation
10. **file.h** - File I/O operations
11. **init.h** - Platform initialization and static initialization helpers
12. **internal.h** - Internal implementation helpers

#### Platform Implementations

**POSIX Implementation (posix/)**
- Implements abstraction layer for Linux and macOS
- Uses standard POSIX APIs (pthread, BSD sockets, termios)
- Full signal support including SIGWINCH and SIGTERM

**Windows Implementation (windows/)**
- Implements abstraction layer for Windows
- Uses Windows APIs (Critical Sections, SRW Locks, Winsock2)
- Limited signal support (SIGWINCH/SIGTERM defined as no-ops)

## API Categories

### Threading (`thread.h`)

**Thread Management:**
```c
int ascii_thread_create(asciithread_t *thread, void *(*func)(void *), void *arg);
int ascii_thread_join(asciithread_t *thread, void **retval);
void ascii_thread_exit(void *retval);
thread_id_t ascii_thread_self(void);
int ascii_thread_equal(thread_id_t t1, thread_id_t t2);
uint64_t ascii_thread_current_id(void);
bool ascii_thread_is_initialized(asciithread_t *thread);
```

### Synchronization (`mutex.h`, `rwlock.h`, `cond.h`)

**Mutexes:**
```c
int mutex_init(mutex_t *mutex);
int mutex_destroy(mutex_t *mutex);
int mutex_lock(mutex_t *mutex);
int mutex_trylock(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);
```

**Read-Write Locks:**
```c
int rwlock_init(rwlock_t *lock);
int rwlock_destroy(rwlock_t *lock);
int rwlock_rdlock(rwlock_t *lock);
int rwlock_wrlock(rwlock_t *lock);
int rwlock_unlock(rwlock_t *lock);
int rwlock_rdunlock(rwlock_t *lock);  // Explicit read unlock
int rwlock_wrunlock(rwlock_t *lock);  // Explicit write unlock
```

**Condition Variables:**
```c
int cond_init(cond_t *cond);
int cond_destroy(cond_t *cond);
int cond_wait(cond_t *cond, mutex_t *mutex);
int cond_timedwait(cond_t *cond, mutex_t *mutex, int timeout_ms);
int cond_signal(cond_t *cond);
int cond_broadcast(cond_t *cond);
```

### Networking (`socket.h`)

**Socket Operations:**
```c
int socket_init(void);                    // Initialize Winsock on Windows
void socket_cleanup(void);                // Cleanup Winsock on Windows
socket_t socket_create(int domain, int type, int protocol);
int socket_close(socket_t sock);
int socket_bind(socket_t sock, const struct sockaddr *addr, socklen_t addrlen);
int socket_listen(socket_t sock, int backlog);
socket_t socket_accept(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);
int socket_connect(socket_t sock, const struct sockaddr *addr, socklen_t addrlen);
ssize_t socket_send(socket_t sock, const void *buf, size_t len, int flags);
ssize_t socket_recv(socket_t sock, void *buf, size_t len, int flags);
int socket_setsockopt(socket_t sock, int level, int optname, const void *optval, socklen_t optlen);
int socket_getsockopt(socket_t sock, int level, int optname, void *optval, socklen_t *optlen);
int socket_set_nonblocking(socket_t sock, bool nonblocking);
int socket_set_reuseaddr(socket_t sock, bool reuse);
int socket_set_nodelay(socket_t sock, bool nodelay);

// Error handling
int socket_get_error(socket_t sock);             // Get error for specific socket
int socket_get_last_error(void);                 // Get last socket operation error
const char *socket_get_error_string(void);       // Get error string
int socket_is_valid(socket_t sock);              // Check if socket is valid
int socket_poll(struct pollfd *fds, nfds_t nfds, int timeout);  // Poll sockets
int socket_get_fd(socket_t sock);                // Get file descriptor from socket
```

### Terminal I/O (`terminal.h`)

**Terminal Operations:**
```c
int terminal_get_size(terminal_size_t *size);
int terminal_set_raw_mode(bool enable);
int terminal_set_echo(bool enable);
bool terminal_supports_color(void);
bool terminal_supports_unicode(void);
int terminal_clear_screen(void);
int terminal_move_cursor(int row, int col);
void terminal_enable_ansi(void);          // Enable ANSI on Windows 10+
int terminal_set_buffering(bool line_buffered);
int terminal_flush(void);
int terminal_hide_cursor(bool hide);
int terminal_get_cursor_position(int *row, int *col);
int terminal_save_cursor(void);
int terminal_restore_cursor(void);
int terminal_set_title(const char *title);
int terminal_ring_bell(void);
int terminal_set_scroll_region(int top, int bottom);
int terminal_reset(void);
```

### System Functions (`system.h`)

**Process & Time:**
```c
void platform_sleep_ms(unsigned int ms);
int platform_get_pid(void);
const char *platform_get_username(void);
signal_handler_t platform_signal(int sig, signal_handler_t handler);
```

**Environment Variables:**
```c
const char *platform_getenv(const char *name);
int platform_setenv(const char *name, const char *value);
```

**TTY Functions:**
```c
int platform_isatty(int fd);
const char *platform_ttyname(int fd);
int platform_fsync(int fd);
```

**Debug/Stack Trace:**
```c
int platform_backtrace(void **buffer, int size);
char **platform_backtrace_symbols(void *const *buffer, int size);
void platform_backtrace_symbols_free(char **strings);

// Crash handling (automatically installed by platform_init)
void platform_install_crash_handler(void);
void platform_print_backtrace(void);
```

### String Operations (`string.h`)

**Safe String Functions:**
```c
int platform_snprintf(char *str, size_t size, const char *format, ...);
int platform_vsnprintf(char *str, size_t size, const char *format, va_list ap);
size_t platform_strlcpy(char *dst, const char *src, size_t size);
size_t platform_strlcat(char *dst, const char *src, size_t size);
int platform_strcasecmp(const char *s1, const char *s2);
int platform_strncasecmp(const char *s1, const char *s2, size_t n);
char *platform_strdup(const char *s);
char *platform_strndup(const char *s, size_t n);
char *platform_strtok_r(char *str, const char *delim, char **saveptr);
```

### File I/O (`file.h`)

**File Operations:**
```c
int platform_open(const char *pathname, int flags, ...);
ssize_t platform_read(int fd, void *buf, size_t count);
ssize_t platform_write(int fd, const void *buf, size_t count);
int platform_close(int fd);
```

## Platform-Specific Features

### Windows Specifics

- **Winsock Initialization**: Required before any socket operations (handled by `platform_init()`)
- **ANSI Escape Sequences**: Automatically enabled on Windows 10+ via `terminal_enable_ansi()`
- **SRW Locks**: Lightweight read-write locks (better than CRITICAL_SECTIONs)
- **Console API**: Full terminal control including colors and cursor positioning
- **Signal Limitations**: SIGWINCH and SIGTERM defined but non-functional
- **Backtrace**: Returns stub implementation (no stack traces)

### POSIX Specifics

- **pthreads**: Full pthread implementation for all threading primitives
- **BSD Sockets**: Standard socket API, no initialization needed
- **termios**: Complete terminal control for raw mode, echo, etc.
- **Signal Handling**: Full support for SIGWINCH (terminal resize) and SIGTERM
- **Backtrace**: Full stack trace support via execinfo.h

## Static Initialization (`init.h`)

The platform layer provides static initialization helpers for global synchronization primitives:

```c
#include "platform/init.h"

// Global mutex with static initialization
static_mutex_t g_mutex = STATIC_MUTEX_INIT;

void critical_function() {
    static_mutex_lock(&g_mutex);
    // Critical section
    static_mutex_unlock(&g_mutex);
}

// Global read-write lock
static_rwlock_t g_rwlock = STATIC_RWLOCK_INIT;

void reader_function() {
    static_rwlock_rdlock(&g_rwlock);
    // Read data
    static_rwlock_unlock(&g_rwlock);
}

// Global condition variable
static_cond_t g_cond = STATIC_COND_INIT;
static_mutex_t g_cond_mutex = STATIC_MUTEX_INIT;

void wait_for_signal() {
    static_mutex_lock(&g_cond_mutex);
    static_cond_wait(&g_cond, &g_cond_mutex);
    static_mutex_unlock(&g_cond_mutex);
}
```

## Usage Examples

### Complete Application Example
```c
#include "platform/abstraction.h"

void* worker_thread(void* arg) {
    printf("Worker thread %d running\n", *(int*)arg);
    platform_sleep_ms(1000);
    return NULL;
}

int main() {
    // Initialize platform (required for Windows sockets)
    platform_init();

    // Create multiple threads
    asciithread_t threads[4];
    int thread_ids[4];

    for (int i = 0; i < 4; i++) {
        thread_ids[i] = i;
        ascii_thread_create(&threads[i], worker_thread, &thread_ids[i]);
    }

    // Wait for all threads
    for (int i = 0; i < 4; i++) {
        ascii_thread_join(&threads[i], NULL);
    }

    // TTY operations
    if (platform_isatty(STDOUT_FILENO)) {
        printf("Running in terminal: %s\n", platform_ttyname(STDOUT_FILENO));
    }

    // Terminal control
    terminal_size_t size;
    if (terminal_get_size(&size) == 0) {
        printf("Terminal size: %dx%d\n", size.cols, size.rows);
    }

    // Cleanup (required for Windows)
    platform_cleanup();
    return 0;
}
```

## Build Integration

The platform abstraction is integrated into the build system:

### Makefile (Unix/macOS)
```makefile
# Platform files are automatically selected based on OS
# POSIX files for Unix/macOS, Windows files for Windows
```

### CMake (Windows)
```cmake
# Platform files are automatically included based on WIN32 detection
```

## Migration from Direct Platform Calls

When migrating code to use the platform abstraction:

1. **Replace direct includes:**
   - `#include <pthread.h>` → `#include "platform/abstraction.h"`
   - `#include <unistd.h>` → Already included via abstraction
   - `#include <winsock2.h>` → Use platform/socket.h

2. **Replace function calls:**
   - `pthread_create()` → `ascii_thread_create()`
   - `isatty()` → `platform_isatty()`
   - `ttyname()` → `platform_ttyname()`
   - `fsync()` → `platform_fsync()`
   - `backtrace()` → `platform_backtrace()`
   - `signal()` → `platform_signal()` (thread-safe on all platforms)

3. **Replace signal handling:**
   ```c
   // Old:
   #ifndef _WIN32
   signal(SIGWINCH, handler);
   #endif

   // New:
   platform_signal(SIGWINCH, handler);  // Thread-safe, cross-platform
   ```

## Testing

The platform abstraction layer is tested through:

1. **Unit Tests**: Individual function tests in `tests/unit/`
2. **Integration Tests**: Multi-threaded scenarios in `tests/integration/`
3. **Cross-Platform CI**: Automated testing on Windows, Linux, and macOS

## Crash Handling

The platform abstraction layer automatically installs crash handlers that capture backtraces when the program crashes:

### Automatic Installation
Crash handlers are automatically installed when `platform_init()` is called, which happens early in the application startup.

### Supported Crash Types

**POSIX (Linux/macOS):**
- `SIGSEGV` - Segmentation fault (null pointer dereference, buffer overflow)
- `SIGABRT` - Abort signal (assertion failures, `abort()` calls)
- `SIGFPE` - Floating point exception (divide by zero)
- `SIGILL` - Illegal instruction
- `SIGBUS` - Bus error

**Windows:**
- `EXCEPTION_ACCESS_VIOLATION` - Access violation (equivalent to SIGSEGV)
- `EXCEPTION_ARRAY_BOUNDS_EXCEEDED` - Array bounds exceeded
- `EXCEPTION_DATATYPE_MISALIGNMENT` - Data type misalignment
- `EXCEPTION_FLT_DIVIDE_BY_ZERO` - Floating point divide by zero
- `EXCEPTION_FLT_INVALID_OPERATION` - Floating point invalid operation
- `EXCEPTION_ILLEGAL_INSTRUCTION` - Illegal instruction
- `EXCEPTION_INT_DIVIDE_BY_ZERO` - Integer divide by zero
- `EXCEPTION_STACK_OVERFLOW` - Stack overflow
- C runtime signals: `SIGABRT`, `SIGFPE`, `SIGILL`

### Output Format
When a crash occurs, the handler outputs:
```
*** CRASH DETECTED ***
Signal: SIGSEGV (Segmentation fault)

=== BACKTRACE ===
 0: main
 1: some_function
 2: another_function
 3: ???
================
```

### Thread Safety
The crash handlers work across **all threads** in the application:

- **Windows**: Uses `SetUnhandledExceptionFilter()` which is process-wide and catches exceptions from all threads
- **POSIX**: Uses `sigaction()` with `SA_SIGINFO` flag for thread-safe signal handling across all threads
- **Backtrace**: Captures the call stack of the thread that crashed

### Benefits
- **Automatic**: No code changes needed - works out of the box
- **Cross-platform**: Same interface on Windows, Linux, and macOS
- **Thread-safe**: Works across all threads in the application
- **Symbol resolution**: Shows function names when debug symbols are available
- **Non-intrusive**: Only activates on crashes, no performance impact during normal operation

## Known Limitations

### Windows
- SRW Locks don't distinguish between read/write unlock operations
- No microsecond sleep precision (minimum ~15ms resolution)
- SIGWINCH and SIGTERM are defined but non-functional
- Full backtrace implementation using StackWalk64 API

### macOS
- System header conflicts require `asciithread_t` naming (not `thread_t`)
- Older GNU Make version (3.81) may have issues with pattern rules

## Contributing

When adding new platform abstractions:

1. Define the interface in the appropriate header file
2. Add Doxygen documentation including @file, @brief, @param, @return
3. Implement POSIX version in `posix/*.c`
4. Implement Windows version in `windows/*.c`
5. Update this README with the new functionality
6. Write tests for both platforms
7. Ensure all platform-specific code is isolated in platform files

## Author

Platform abstraction layer developed by Zachary Fogg <me@zfo.gg>
September 2025
