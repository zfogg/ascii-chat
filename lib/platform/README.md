# Platform Abstraction Layer

## Overview

The platform abstraction layer provides a unified, cross-platform API that enables ASCII-Chat to run seamlessly on Windows, Linux, and macOS. This layer abstracts platform-specific functionality into a common interface, allowing the main application code to remain platform-independent.

## Architecture

### File Structure

```
lib/platform/
├── README.md           # This file
├── abstraction.h       # Main abstraction header with all API definitions
├── abstraction.c       # Common implementation (currently minimal)
├── init.h              # Platform initialization helpers and static init wrappers
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

#### 1. **abstraction.h** - Main API Header
- Central header file that defines all platform abstraction interfaces
- Provides compile-time platform detection (`PLATFORM_WINDOWS` vs `PLATFORM_POSIX`)
- Defines all data structures and function prototypes
- Includes platform-specific headers based on detected OS

#### 2. **init.h** - Initialization Helpers
- Static initialization wrappers for global synchronization primitives
- Lazy initialization for Windows (which lacks PTHREAD_MUTEX_INITIALIZER equivalents)
- Platform initialization/cleanup functions
- Thread-safe initialization using atomic operations

#### 3. **posix/** - POSIX Implementation
- Implements abstraction layer for Linux and macOS across multiple component files
- **thread.c**: Wraps pthreads for threading
- **socket.c**: BSD sockets for networking
- **terminal.c**: Terminal I/O using termios
- **mutex.c, rwlock.c, cond.c**: Synchronization primitives
- **system.c**: System functions (sleep, process info, environment)

#### 4. **windows/** - Windows Implementation
- Implements abstraction layer for Windows across multiple component files
- **thread.c**: Wraps Windows Threading API
- **mutex.c**: Critical Sections for mutexes
- **rwlock.c**: SRW Locks for read-write locks
- **cond.c**: Condition Variables
- **socket.c**: Winsock2 for networking
- **terminal.c**: Console API for terminal operations
- **system.c**: Windows system functions

## API Categories

### Threading (`asciithread_t`, `mutex_t`, `rwlock_t`, `cond_t`)

**Thread Management:**
```c
int ascii_thread_create(asciithread_t *thread, void *(*func)(void *), void *arg);
int ascii_thread_join(asciithread_t *thread, void **retval);
void ascii_thread_exit(void *retval);
thread_id_t ascii_thread_self(void);
int ascii_thread_equal(thread_id_t t1, thread_id_t t2);
uint64_t ascii_thread_current_id(void);
```

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

### Networking (`socket_t`)

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
int socket_set_nonblocking(socket_t sock, bool nonblocking);
int socket_set_reuseaddr(socket_t sock, bool reuse);
int socket_set_nodelay(socket_t sock, bool nodelay);
int socket_is_valid(socket_t sock);
```

### Terminal I/O

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
```

### System Functions

**Process & Time:**
```c
void platform_sleep_ms(unsigned int ms);
void platform_sleep_us(unsigned int us);
int platform_get_pid(void);
const char *platform_get_username(void);
signal_handler_t platform_signal(int sig, signal_handler_t handler);
```

**Environment Variables:**
```c
const char *platform_getenv(const char *name);
int platform_setenv(const char *name, const char *value);
```

## Platform-Specific Features

### Windows Specifics

- **Winsock Initialization**: Required before any socket operations
- **ANSI Escape Sequences**: Must be explicitly enabled on Windows 10+
- **SRW Locks**: Used for read-write locks (lighter than CRITICAL_SECTIONs)
- **Console API**: Used for terminal size and cursor control
- **Thread IDs**: Uses DWORD (32-bit) thread IDs

### POSIX Specifics

- **pthreads**: Full pthread implementation for threading
- **BSD Sockets**: Standard socket API
- **termios**: Terminal control for raw mode and echo
- **Signal Handling**: Full signal support with SIGPIPE ignored

## Usage Examples

### Basic Thread Creation
```c
#include "platform/abstraction.h"

void* worker_thread(void* arg) {
    printf("Worker thread running\n");
    return NULL;
}

int main() {
    // Initialize platform (required on Windows)
    platform_init();
    
    asciithread_t thread;
    if (ascii_thread_create(&thread, worker_thread, NULL) == 0) {
        ascii_thread_join(&thread, NULL);
    }
    
    // Cleanup (required on Windows)
    platform_cleanup();
    return 0;
}
```

### Socket Server
```c
#include "platform/abstraction.h"

int main() {
    // Initialize sockets (required on Windows)
    socket_init();
    
    socket_t server = socket_create(AF_INET, SOCK_STREAM, 0);
    if (server != INVALID_SOCKET_VALUE) {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8080);
        addr.sin_addr.s_addr = INADDR_ANY;
        
        socket_bind(server, (struct sockaddr*)&addr, sizeof(addr));
        socket_listen(server, 5);
        
        // Accept connections...
        
        socket_close(server);
    }
    
    // Cleanup sockets (required on Windows)
    socket_cleanup();
    return 0;
}
```

### Static Mutex Initialization
```c
#include "platform/init.h"

// Global mutex with static initialization
static_mutex_t g_mutex = STATIC_MUTEX_INIT;

void critical_function() {
    static_mutex_lock(&g_mutex);
    // Critical section
    static_mutex_unlock(&g_mutex);
}
```

## Build Integration

### Makefile
```makefile
# Platform abstraction sources - select based on OS
PLATFORM_COMMON_SOURCES := $(LIB_DIR)/platform/abstraction.c

ifeq ($(OS),Windows_NT)
    PLATFORM_SOURCES := $(PLATFORM_COMMON_SOURCES) $(wildcard $(LIB_DIR)/platform/windows/*.c)
else
    PLATFORM_SOURCES := $(PLATFORM_COMMON_SOURCES) $(wildcard $(LIB_DIR)/platform/posix/*.c)
endif

# Files use explicit includes like "platform/abstraction.h"
# No additional include path needed
```

### CMake
```cmake
# Platform abstraction sources
set(PLATFORM_SOURCES
    lib/platform/abstraction.c
)

# Add platform-specific sources
if(WIN32)
    file(GLOB PLATFORM_WINDOWS_SOURCES "lib/platform/windows/*.c")
    list(APPEND PLATFORM_SOURCES ${PLATFORM_WINDOWS_SOURCES})
else()
    file(GLOB PLATFORM_POSIX_SOURCES "lib/platform/posix/*.c")
    list(APPEND PLATFORM_SOURCES ${PLATFORM_POSIX_SOURCES})
endif()

# Files use explicit includes like "platform/abstraction.h"
# No additional include path needed
```

## Testing

The platform abstraction layer is tested through:

1. **Unit Tests**: Tests for individual functions (mutexes, threads, sockets)
2. **Integration Tests**: Multi-threaded scenarios, client-server communication
3. **Cross-Platform CI**: Automated testing on Windows, Linux, and macOS

## Known Limitations

### Windows
- SRW Locks don't track lock type (read vs write) for unlock operations
- Sleep precision limited to milliseconds (no true microsecond sleep)
- Some POSIX signals not available

### macOS
- System header conflicts require asciithread_t naming (not thread_t)
- Some Linux-specific features may not be available

## Future Enhancements

- [ ] Add spinlock support for high-performance scenarios
- [ ] Implement thread pool abstraction
- [ ] Add memory-mapped file abstraction
- [ ] Support for asynchronous I/O operations
- [ ] Add performance profiling hooks
- [ ] Implement platform-specific optimizations

## Contributing

When adding new platform abstractions:

1. Define the interface in `abstraction.h`
2. Implement POSIX version in appropriate `posix/*.c` file
3. Implement Windows version in appropriate `windows/*.c` file
4. Add documentation to this README
5. Write tests for both platforms
6. Update build files (Makefile and CMakeLists.txt)

## License

This platform abstraction layer is part of ASCII-Chat and follows the same license as the main project.