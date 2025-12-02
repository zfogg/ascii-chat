uu# ascii-chat Development Guide for Claude

## Repository Information

- **Repository Owner**: zfogg (GitHub username)
- **Repository**: ascii-chat

## Essential First Steps

- **ALWAYS** read and understand the files in this repo first to understand ascii-chat. As Claude you mostly need the files in `src/`, `lib/`, `docs/`
- Use ctest for running tests: `cmake --build build --target tests && ctest --test-dir build --output-on-failure --parallel 0`
- **Use memory macros** from common.h rather than regular malloc/free (see Memory Management section below)
- Use `lldb` for debugging
- On Windows: use PowerShell build script `./build.ps1` which kills any processes, rebuilds, and copies the dlls and executable to bin/.
- **IMPORTANT: This project uses Clang only, not MSVC or GCC** - `clang` is the compiler we support
- Do NOT use `git add .`. Add files individually and make per-file commits or add hunks and commit them.
- Use AddressSanitizer (ASan) and memory reports from common.c for memory debugging: `cmake --preset debug -B build && cmake --build build`
- Use log\_\*() from logging.c and common.h for logging instead of printf()
- **Use asciichat_error_t instead of int for return types**
- **Use asciichat_errno for error handling instead of system errno**
- When debugging and testing, make a test_whatever.sh and use that so you don't bother the developer by requesting to run commands over and over
- Use `cmake --preset` by  default. Use `cmake --preset default` as much as possible so you don't get confused and try to use UNIX Makefiles or Visual Studio 2017, when the build dir is supposed to be configured for Ninja files. This causes scripting errorrs which painfully slow down Claude's development progress and confuses him. Stick with Ninja.
- **AVOID deleting the build directory** - Prefer reconfiguring (`cmake --preset default`) and rebuilding (`cmake --build build`) without `rm -rf build`. Only delete specific files if needed, not the entire build directory. Deleting rebuilds everything from scratch including PCH, defer tool, and dependencies which is slow. Only delete the build dir when absolutely necessary (e.g., switching build types, corrupted cache).

## Memory Management Macros (CRITICAL)

**IMPORTANT**: ascii-chat uses custom memory macros for debugging and leak tracking. **ALWAYS** use these instead of standard C memory functions.

### Safe Memory Allocation Macros

All memory allocation macros are defined in `lib/common.h` and provide automatic leak tracking when `DEBUG_MEMORY` is enabled.

**SAFE_MALLOC, SAFE_CALLOC, SAFE_REALLOC** - Allocate memory with leak tracking:

```c
// ✅ CORRECT - Two arguments: size and cast type
uint8_t *buffer = SAFE_MALLOC(1024, uint8_t *);
client_t *client = SAFE_MALLOC(sizeof(client_t), client_t *);

// ❌ WRONG - Missing size argument
uint8_t *buffer = SAFE_MALLOC(uint8_t *);  // COMPILE ERROR!

// ❌ WRONG - Using old malloc
uint8_t *buffer = malloc(1024);  // No leak tracking!
```

### Debug Memory Tracking

When `CMAKE_BUILD_TYPE=Debug`, all SAFE\_\* allocations are tracked:

- Memory leaks are reported on program exit
- Each allocation includes file and line number
- Use `DEBUG_MEMORY` define for verbose allocation logs

```bash
# Enable memory debugging
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run with memory leak detection
./build/bin/ascii-chat server
# On exit, memory leaks are reported with source locations
```

## Logging and Error Handling (CRITICAL)

### Logging Macros

**Use log\_\*\_every() for rate-limited logging in high-frequency code:**

```c
// ✅ CORRECT - Rate-limited logging for video/audio threads
log_debug_every(1000000, "Processing frame %d", frame_count);  // Max once per second
log_info_every(5000000, "Client connected: %s", client_ip);   // Max once per 5 seconds

// ❌ WRONG - Spammy logging in tight loops
for (int i = 0; i < 1000; i++) {
    log_debug("Processing item %d", i);  // Will flood logs!
}
```

**Standard logging macros:**

```c
log_debug("Debug message: %s", data);
log_info("Info message: %d clients connected", count);
log_warn("Warning: %s", warning_msg);
log_error("Error occurred: %s", error_msg);
log_fatal("Fatal error: %s", fatal_msg);
```

### Error Handling with asciichat_error_t

**ALWAYS use asciichat_error_t instead of int for return types:**

```c
// ✅ CORRECT - Use asciichat_error_t
asciichat_error_t process_client(client_t *client) {
    if (!client) {
        SET_ERRNO(ERROR_INVALID_PARAM, "Client is NULL");
        return ERROR_INVALID_PARAM;
    }
    // ... processing ...
    return ASCIICHAT_OK;
}

// ❌ WRONG - Using int return type
int process_client(client_t *client) {
    if (!client) {
        return -1;  // No context, no logging!
    }
    return 0;
}
```

### Error Context and Logging

**Use asciichat_errno for comprehensive error tracking:**

```c
// In library code (lib/):
if (bind(sockfd, &addr, sizeof(addr)) < 0) {
    SET_ERRNO_SYS(ERROR_NETWORK_BIND, "Cannot bind to port %d", port);
    return ERROR_NETWORK_BIND;
}

// In application code (src/):
asciichat_error_t result = process_data();
if (result != ASCIICHAT_OK) {
    asciichat_error_context_t err_ctx;
    if (HAS_ERRNO(&err_ctx)) {
        log_error("Operation failed: %s", err_ctx.context_message);
        PRINT_ERRNO_CONTEXT(&err_ctx);  // Debug builds only
    }
    return result;
}
```

### Use SET_ERRNO() Instead of Old Error Patterns

**❌ WRONG - Old pattern (don't use this):**

```c
// Old way - no context, no proper error tracking
if (some_operation() < 0) {
    log_error("Operation failed");
    return -1;  // No error code, no context!
}
```

**✅ CORRECT - Use SET_ERRNO() macro:**

```c
// New way - proper error context and logging
if (some_operation() < 0) {
    return SET_ERRNO(ERROR_OPERATION_FAILED, "Operation failed: %s", error_details);
}

// For system errors, use SET_ERRNO_SYS:
if (open(file, O_RDONLY) < 0) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open config file: %s", path);
}
```

**Key Benefits of SET_ERRNO():**

- **Automatic logging**: Logs the error with context automatically
- **Error context**: Captures file, line, function, and custom message
- **Consistent error codes**: Uses `asciichat_error_t` enum values
- **System error integration**: `SET_ERRNO_SYS()` captures system errno
- **Debug support**: Includes stack traces in debug builds

**Error checking macros:**

```c
// Check if any error occurred
if (HAS_ERRNO(&err_ctx)) {
    log_error("Error: %s", err_ctx.context_message);
}

// Clear error state
CLEAR_ERRNO();

// Get current error code
asciichat_error_t current_error = GET_ERRNO();
```

## Project Overview

ascii-chat is a terminal-based video chat application that converts webcam video to ASCII art in real-time. It supports multiple clients connecting to a single server, with video mixing and audio streaming capabilities.

**Key Features:**

- Real-time webcam to ASCII conversion
- Multi-client video grid layout (2x2, 3x3, etc)
- Audio streaming with mixing
- **End-to-end encryption** with libsodium (X25519, XSalsa20-Poly1305, Ed25519)
- SSH key authentication with agent support
- Compression support for frames
- **Cross-platform (macOS/Linux/Windows)** ✨ NEW
- Command line program for your terminal
- Half-block render mode for 2x vertical resolution
- Customizable ASCII palettes
- Comprehensive test suite with Criterion framework

## Project Structure (Updated September 2025)

```
ascii-chat/
├── bin/                                # Hard links to compiled binaries for convenience
├── build/                              # CMake build directory (all platforms)
├── notes/                              # Development notes and documentation
├── todo/                               # Experimental code and future features
├── tests/                              # Comprehensive test suite using Criterion
│   ├── scripts/                        # Test infrastructure scripts
│   │   └── run-docker-tests.ps1        # Docker-based test runner for Windows
│   ├── unit/                           # Unit tests for individual components
│   ├── integration/                    # Multi-component integration tests
│   ├── performance/                    # Performance and stress tests
├── src/                                # Main application entry points
│   ├── server.c                        # Server main - handles multiple clients
│   └── client.c                        # Client main - captures/displays video
├── lib/                                # Core library components
│   ├── common.c/h                      # Shared utilities, macros, memory debugging, constants
│   ├── platform/                       # Cross-platform abstraction layer
│   │   ├── README.md                   # Platform abstraction documentation
│   │   ├── abstraction.h               # Main abstraction header with all API definitions
│   │   ├── abstraction.c               # Common implementation (currently minimal)
│   │   ├── init.h                      # Platform initialization helpers and static init wrappers
│   │   ├── posix/                      # POSIX implementation (Linux/macOS)
│   │   │   ├── thread.c                # POSIX pthread implementation
│   │   │   ├── mutex.c                 # POSIX mutex implementation
│   │   │   ├── rwlock.c                # POSIX read-write lock implementation
│   │   │   ├── cond.c                  # POSIX condition variable implementation
│   │   │   ├── terminal.c              # POSIX terminal I/O implementation
│   │   │   ├── system.c                # POSIX system functions implementation
│   │   │   └── socket.c                # POSIX socket implementation
│   │   └── windows/                    # Windows implementation
│   │       ├── thread.c                # Windows thread implementation
│   │       ├── mutex.c                 # Windows mutex (Critical Section) implementation
│   │       ├── rwlock.c                # Windows read-write lock (SRW Lock) implementation
│   │       ├── cond.c                  # Windows condition variable implementation
│   │       ├── terminal.c              # Windows terminal (Console API) implementation
│   │       ├── system.c                # Windows system functions implementation
│   │       └── socket.c                # Windows socket (Winsock2) implementation
│   ├── crypto/                         # Cryptographic protocol implementation
│   │   ├── crypto.c/h                  # Core crypto operations (X25519, XSalsa20-Poly1305)
│   │   ├── handshake.c/h               # Protocol handshake and mutual authentication
│   │   ├── ssh_agent.c/h               # SSH agent integration
│   │   ├── known_hosts.c/h             # Known hosts TOFU verification
│   │   ├── pem_utils.c/h               # PEM file parsing utilities
│   │   ├── gpg.c/h                     # GPG key support
│   │   ├── http_client.c/h             # HTTPS client for key fetching (BearSSL)
│   │   └── keys/                       # Key management implementations
│   │       ├── keys.c/h                # Key loading and management API
│   │       ├── types.h                 # Key type definitions
│   │       ├── ssh_keys.c/h            # SSH Ed25519 key parsing
│   │       ├── gpg_keys.c/h            # GPG key handling
│   │       ├── https_keys.c/h          # GitHub/GitLab key fetching
│   │       └── validation.c/h          # Key validation utilities
│   ├── logging.c                       # Logging system implementation
│   ├── options.c/h                     # Command-line argument parsing
│   ├── network.c/h                     # Network protocol and packet handling
│   ├── packet_queue.c/h                # Thread-safe per-client packet queues
│   ├── buffer_pool.c/h                 # Memory buffer pool system for efficient allocation
│   ├── ringbuffer.c/h                  # Lock-free ring buffer implementation
│   ├── compression.c/h                 # Frame compression with zstd
│   ├── crc32_hw.c/h                    # Hardware-accelerated CRC32 checksums
│   ├── mixer.c/h                       # Audio mixing for multiple clients
│   ├── audio.c/h                       # Audio capture/playback (PortAudio)
│   ├── image2ascii/                    # Image processing and ASCII conversion
│   │   ├── image.c/h                   # Image processing and manipulation
│   │   └── ascii.c/h                   # ASCII art conversion and grid layout
│   ├── aspect_ratio.c/h                # Aspect ratio calculations
│   ├── ansi_fast.c/h                   # Optimized ANSI escape sequence generation
│   ├── terminal_detect.c/h             # Terminal capability detection
│   ├── palette.c/h                     # ASCII palette management
│   ├── webcam.c/h                      # Webcam capture abstraction layer
│   ├── webcam_platform.c/h             # Platform-specific webcam detection
│   ├── webcam_avfoundation.m           # macOS webcam implementation (AVFoundation)
│   ├── webcam_v4l2.c                   # Linux webcam implementation (Video4Linux2)
│   ├── webcam_windows.c                # Windows webcam implementation stub (NEW)
│   └── round.h                         # Rounding utilities
├── CMakeLists.txt                      # CMake build configuration (all platforms)
├── Info.plist                          # macOS application metadata
└── CLAUDE.md                           # Development guide (this file)
```

## Platform Support and Building

### Windows Support (NEW - September 2025)

ascii-chat now has comprehensive Windows support through a platform abstraction layer:

**Windows-specific features:**

- Full platform abstraction layer for threads, mutexes, sockets
- CMake build system for Windows
- Windows socket implementation with Winsock2
- Platform-safe function wrappers (SAFE_GETENV, SAFE_SSCANF, SAFE_STRERROR, platform_open)
- Thread-safe error handling with thread-local storage
- Webcam stub implementation (test pattern mode)

**Building on Windows:**

The preferred method is using the PowerShell build script `build.ps1` which handles all configuration automatically:

```powershell
# Default build with Clang in native Windows mode
./build.ps1

# Clean rebuild
./build.ps1 -Clean

# Release build
./build.ps1 -Config Release

# Custom build directory
./build.ps1 -BuildDir mybuild

# Add compiler flags (e.g., for debugging)
./build.ps1 -CFlags "-DDEBUG_THREADS","-DDEBUG_MEMORY"

# Verbose output
./build.ps1 -Verbose
```

**Note**: The Criterion test framework is primarily Unix-based and typically only works on Windows when using MinGW/MSYS2 with pkg-config installed. Native Windows builds usually cannot run the tests. We don't support MinGW builds for ascii-chat.

The build script automatically:

- Kills any running ascii-chat processes before building
- Cleans build directory if passed -Clean.
- Copies runtime DLLs, binaries, and debug info to the correct location.
- Links compile_commands.json to repo root

Manual CMake build (if not using build.ps1):

```bash
# Use CMake with Clang and Ninja
cmake --preset default -B build
cmake --build build

# Run server and client (unified binary)
./build/bin/ascii-chat.exe server
./build/bin/ascii-chat.exe client
```

### Unix/macOS Building

**Standard Build Commands:**

```bash
# Configure and build (creates build directory if needed)
cmake -B build -DCMAKE_BUILD_TYPE=Debug  # Debug with AddressSanitizer
cmake --build build

# Different build types
cmake -B build -DCMAKE_BUILD_TYPE=Release  # Optimized release build
cmake -B build -DCMAKE_BUILD_TYPE=Dev      # Debug symbols only, no sanitizers

# Clean rebuild
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Format code after changes
cmake --build build --target format

# Build and run tests
cmake --build build --target tests
ctest --test-dir build --output-on-failure --parallel 0
```

### BearSSL Build Optimization (Automatic)

BearSSL is a third-party SSL library that rarely changes. CMake automatically builds it **once** using bearssl's own Makefile and reuses the built library across clean builds:

**First CMake run** (builds bearssl once):

```
-- Building BearSSL library (one-time setup)...
-- BearSSL library built successfully
-- Using BearSSL library: deps/bearssl/build/libbearssl.a
```

**Subsequent builds** (including after `rm -rf build`):

```
-- Using BearSSL library: deps/bearssl/build/libbearssl.a
```

**Build time comparison:**

- First build (includes bearssl): ~30 seconds
- Clean rebuilds (reuses bearssl): ~8 seconds
- **Speedup: 3.75x faster clean builds**

The pre-built library at `deps/bearssl/build/libbearssl.a` persists across `rm -rf build`, so you only rebuild bearssl if you delete it or update the bearssl submodule.

### Essential Commands

```bash
# Start server (listens on port 27224 by default)
./build/bin/ascii-chat server  # Unix/macOS
./build/bin/ascii-chat.exe server  # Windows

# Start client (connects to localhost by default)
./build/bin/ascii-chat client  # Unix/macOS
./build/bin/ascii-chat.exe client  # Windows

# Connect to a specific server
./build/bin/ascii-chat client --address 127.0.0.1 --port 27224

# Run with custom dimensions (by default it uses terminal size)
./build/bin/ascii-chat client --width 80 --height 24

# Color support and audio (audio is always enabled on server, optional for client)
./build/bin/ascii-chat server --color
./build/bin/ascii-chat client --color --audio

# Help and options
./build/bin/ascii-chat server --help
./build/bin/ascii-chat client --help
./build/bin/ascii-chat --help  # Shows mode selection help
```

### Debug Helpers

```bash
# --log-file helps with debugging (better than pipe redirects)
./build/bin/ascii-chat server --log-file /tmp/server-test.log
./build/bin/ascii-chat client --log-file /tmp/client-test.log

# --snapshot mode for testing without continuous capture
./build/bin/ascii-chat client --snapshot                    # Single frame and exit
./build/bin/ascii-chat client --snapshot --snapshot-delay 10 # Capture for 10 seconds then exit
```

## Testing Framework

**IMPORTANT**: Use ctest for running tests!

### Basic Test Usage

```bash
# Build all tests
cmake --build build --target tests

# Run all tests
ctest --test-dir build --output-on-failure --parallel 0

# Run specific test category using labels
ctest --test-dir build --label-regex "^unit$" --output-on-failure
ctest --test-dir build --label-regex "^integration$" --output-on-failure
ctest --test-dir build --label-regex "^performance$" --output-on-failure

# Run single test by name pattern
ctest --test-dir build -R "buffer_pool" --output-on-failure

# List all available tests
ctest --test-dir build -N
```

### Advanced Test Options

```bash
# Verbose output
ctest --test-dir build --output-on-failure --verbose

# For coverage, use cmake with ASCIICHAT_ENABLE_COVERAGE=ON
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_ENABLE_COVERAGE=ON
cmake --build build --target tests
ctest --test-dir build --output-on-failure --parallel 0
```

# ❌ Criterion on macOS for Claude Code is NOT Supported
Criterion tests don't work for Claude Code on macOS for some reason. The executables crash. They log something about forking failures in his environment when he runs them. They error immediately and print nasty messages and then he always tries to use CRITERION_NO_FORK but it never works, poor Claude.. Stop trying to use CRITERION_NO_FORK and instead just use Docker: `docker-compose -f ./tests/docker-compose.yml run --rm ascii-chat-tests bash -c 'build_docker/bin/test_unit_crypto_handshake'`. tests/docker-compose.yml is the CORRECT way to deal with the Criterion issue for Claude and ascii-chat.

## Platform Abstraction Layer (NEW)

### Overview

The platform abstraction layer enables cross-platform support for Windows, Linux, and macOS:

**Key Components:**

- `lib/platform.h` - Main abstraction interface
- `lib/platform_posix.c` - POSIX implementation (Linux/macOS)
- `lib/platform_windows.c` - Windows implementation
- `lib/socket_posix.c` / `lib/socket_windows.c` - Socket implementations

### Thread Abstraction

```c
// Threads use ascii_ prefix to avoid conflicts
ascii_thread_t thread;
ascii_thread_create(&thread, thread_func, arg);
ascii_thread_join(thread, NULL);

// Mutexes
mutex_t mutex;
mutex_init(&mutex);
mutex_lock(&mutex);
mutex_unlock(&mutex);
mutex_destroy(&mutex);

// Read-write locks
rwlock_t rwlock;
rwlock_init(&rwlock);
rwlock_rdlock(&rwlock);
rwlock_wrlock(&rwlock);
rwlock_unlock(&rwlock);
```

### Socket Abstraction

```c
// Socket functions work across platforms
socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
if (sock == INVALID_SOCKET_VALUE) { /* error */ }
socket_close(sock);
```

### Platform-Safe Functions

```c
// Safe environment variable access
char* value = SAFE_GETENV("HOME");

// Safe string scanning
int count = SAFE_SSCANF(input, "%d %d", &x, &y);

// Thread-safe error strings
const char* error = SAFE_STRERROR(errno);

// Safe file operations
int fd = platform_open(filename, O_RDWR | O_CREAT, 0600);

// Safe string copy (already existed)
SAFE_STRNCPY(dest, src, sizeof(dest));
```

## Debugging Techniques

### Environment Variables

- `$SSH_AUTH_SOCK` - SSH agent socket path for password-free key authentication (Unix only)
- `$ASCII_CHAT_SSH_PASSWORD` - Passphrase for encrypted SSH keys (⚠️ sensitive, prefer ssh-agent)
- `$LOG_LEVEL` - Enable logging at a certain level (DEBUG/0, INFO/1, WARN/2, ERROR/3, FATAL/4)
- `$CLAUDECODE` - Automatically set by Claude Code; enables LLM-friendly behavior (see below)

### CLAUDECODE Environment Variable

Claude Code automatically sets `CLAUDECODE=1` in its environment. ascii-chat detects this to optimize behavior for LLM automation:

**Build System:**
- `cmake/utils/Colors.cmake` - Disables ANSI color codes in CMake output

**Debug Builds Only (`#ifndef NDEBUG`):**
- `lib/crypto/known_hosts.c` - Skips interactive host identity prompts
- `lib/crypto/handshake.c` - Skips known_hosts checking (can't do interactive prompts)
- `src/client/crypto.c` - Skips "continue anyway?" security prompts
- `lib/platform/posix/terminal.c` - Forces `--color-mode mono` to reduce output tokens
- `lib/platform/windows/terminal.c` - Forces `--color-mode mono` to reduce output tokens

**Why This Exists:**
- LLMs cannot interact with keyboard prompts, so security prompts block automation
- Color codes waste tokens and add no value for LLM parsing
- Debug-only ensures production builds maintain full security

### Common Issues and Solutions

**"Unknown packet type" errors:**

- Check packet type enum values in network.h
- Verify receive_packet() in network.c has cases for ALL packet types

**"DEADBEEF" pattern in packet data:**

- Indicates TCP stream corruption/desynchronization
- Usually caused by concurrent socket writes without synchronization

**Memory crashes:**

- Always use SAFE_MALLOC() macro instead of malloc()
- Build with AddressSanitizer: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`

### Debugging Tools

```bash
# Memory debugging
leaks --atExit -- ./build/bin/ascii-chat server     # macOS
valgrind ./build/bin/ascii-chat server               # Linux

# Debuggers
lldb ./build/bin/ascii-chat server                   # macOS/Linux
```

## Network Protocol

### Packet Structure

```c
typedef struct {
  uint32_t magic;     // PACKET_MAGIC (0xDEADBEEF) for validation
  uint16_t type;      // packet_type_t enum value
  uint32_t length;    // payload length
  uint32_t crc32;     // payload checksum
  uint32_t client_id; // which client sent this (0 = server)
} __attribute__((packed)) packet_header_t;
```

### Packet Types

```c
typedef enum {
  // Unified frame packets (header + data in single packet)
  PACKET_TYPE_ASCII_FRAME = 1, // Server->Client - Complete ASCII frame
  PACKET_TYPE_IMAGE_FRAME = 2, // Client->Server - Complete RGB image

  // Audio and control
  PACKET_TYPE_AUDIO = 3,               // Audio data
  PACKET_TYPE_CLIENT_CAPABILITIES = 4, // Client reports terminal capabilities
  PACKET_TYPE_PING = 5,                // Keepalive
  PACKET_TYPE_PONG = 6,                // Keepalive response

  // Multi-user protocol extensions
  PACKET_TYPE_CLIENT_JOIN = 7,    // Client announces capability
  PACKET_TYPE_CLIENT_LEAVE = 8,   // Clean disconnect notification
  PACKET_TYPE_STREAM_START = 9,   // Client requests to start sending
  PACKET_TYPE_STREAM_STOP = 10,   // Client stops sending media
  PACKET_TYPE_CLEAR_CONSOLE = 11, // Server tells client to clear
  PACKET_TYPE_SERVER_STATE = 12,  // Server sends current state
  PACKET_TYPE_AUDIO_BATCH = 13    // Batched audio packets
} packet_type_t;
```

## Cryptographic Protocol

ascii-chat implements **end-to-end encryption by default** using libsodium:

**Algorithms:**

- **X25519** - Elliptic curve Diffie-Hellman key exchange
- **XSalsa20-Poly1305** - Authenticated encryption (AEAD cipher)
- **Ed25519** - Digital signatures for SSH key authentication

### Authentication Modes

**1. Default (Ephemeral DH):**

```bash
./ascii-chat server
./ascii-chat client
# Privacy: encrypted against eavesdropping
# Security: ❌ vulnerable to MITM (no identity verification)
```

**2. Password Authentication:**

```bash
./ascii-chat server --password "shared_secret"
./ascii-chat client --password "shared_secret"
```

**3. SSH Key Authentication:**

```bash
# Server with SSH key
./ascii-chat server --key ~/.ssh/id_ed25519

# Client verifies server identity
./ascii-chat client --server-key ~/.ssh/server_id_ed25519.pub
# OR fetch from GitHub:
./ascii-chat client --server-key github:zfogg
```

**For detailed protocol specification, see `docs/crypto.md`**

## Git Workflow

```bash
# Create branch for issue
git checkout master
git pull origin master
git checkout -b claude/issue-XX-$(date +%Y%m%d-%H%M)

# After changes
git add <specific files>  # Don't use git add .
git commit -m "fix: description of fix

Fixes #XX"

# Push to origin
git push origin HEAD
```

## Architecture Notes

### Per-Client Threading Architecture

ascii-chat uses a high-performance per-client threading model:

- **Each client gets 2 dedicated threads**: 1 video render (60 FPS) + 1 audio render (172 FPS)
- **Linear performance scaling**: No shared bottlenecks, scales to 9+ clients
- **Thread-safe design**: Proper synchronization eliminates race conditions

### Critical Synchronization Rules

**Always acquire locks in this exact order:**

1. **Global RWLock** (`g_client_manager_rwlock`)
2. **Per-Client Mutex** (`client_state_mutex`)
3. **Specialized Mutexes** (`g_stats_mutex`, `g_frame_cache_mutex`, etc.)

### Memory Management Rules

1. Always use SAFE_MALLOC(), SAFE_REALLOC(), SAFE_CALLOC() instead of malloc/realloc/calloc
2. Framebuffer owns its data - don't free it
3. Packet queues copy data if owns_data=true
4. Set pointers to NULL after freeing
5. Use platform abstraction for thread operations

## Manual Testing Checklist

Before committing any changes:

1. [ ] `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build` - rebuild with sanitizers
2. [ ] `cmake --build build --target format` - code is properly formatted
3. [ ] **Run tests**: `ctest --test-dir build --output-on-failure --parallel 0`
4. [ ] Start server, connect 2+ clients
5. [ ] Video displays in correct grid layout
6. [ ] No "DEADBEEF" or "Unknown packet type" errors
7. [ ] No CRC checksum mismatches
8. [ ] Audio works (if testing audio changes)
9. [ ] Clients can disconnect/reconnect cleanly
10. [ ] No crashes over 1+ minute runtime

## Development Principles

### No Emergency Cleanup Code

**NEVER** write "emergency cleanup" or "force cleanup" code. This indicates improper resource management.

**Instead, Code Properly From Start:**

- Assume threads and networks have **irregular, unpredictable timing**
- Design resource ownership to be **unambiguous** (single owner per resource)
- Use **deterministic cleanup sequences** that work regardless of timing
- **Wait for threads properly** before cleaning up their resources

### Platform Portability

- Always use platform abstraction layer for threads, mutexes, sockets
- Use SAFE\_\* macros for platform-specific functions
- Test on multiple platforms when possible
- Initialize sockets to INVALID_SOCKET_VALUE, not 0 or -1

## Important Files to Understand

### Core Infrastructure

1. **platform.h/c**: Platform abstraction layer - CRITICAL for Windows support
2. **network.h/c**: Defines ALL packet types and protocol
3. **lib/crypto/**: Cryptographic protocol implementation
4. **packet_queue.c**: Thread-safe per-client queue implementation
5. **buffer_pool.c/h**: Memory buffer pool for efficient allocation
6. **uthash**: Third-party hash table library for client ID and symbol lookups

### Main Application

7. **server.c**: Video broadcast and audio mixing threads
8. **client.c**: Video capture and frame processing

### Media Processing

9. **lib/image2ascii/ascii.c**: ASCII conversion and grid layout
10. **mixer.c**: Multi-client audio mixing with ducking
11. **ringbuffer.c**: Framebuffer for multi-frame storage
12. **ansi_fast.c**: Optimized ANSI escape sequences

### Testing Infrastructure

13. **ctest**: Main test runner - use `ctest --test-dir build --output-on-failure`
14. **tests/unit/**: Unit test implementations
15. **CMakeLists.txt**: Cross-platform build configuration

## Recent Updates (September 2025)

### Query Tool (December 2025)

Debug utility for runtime variable inspection via HTTP queries. Uses external LLDB process for robust debugging without self-patching complexity.

**Key Features:**
- Query variables by `file:line:variable` including struct members
- HTTP API for integration with editors, scripts, and external tools
- Breakpoint mode with `&break` parameter for interactive debugging
- Struct expansion with configurable depth
- Auto-spawn via `QUERY_INIT(port)` macro (compiles out in release)

**Usage:**
```bash
# Build with query tool
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
cmake --build build

# Standalone: attach to running process
./build/bin/ascii-chat server &
./.deps-cache/query-tool/ascii-query-server --attach $(pgrep ascii-chat) --port 9999
curl 'localhost:9999/query?file=src/server/main.c&line=50&name=options'

# Auto-spawn in code (debug builds only)
QUERY_INIT(9999);   // Spawns controller attached to self
// ... app runs, query via HTTP ...
QUERY_SHUTDOWN();
```

**Documentation:** `docs/tooling/query.md`, `docs/tooling/query-api.md`

### Cryptography Implementation (October 2025)

- End-to-end encryption with libsodium (X25519, XSalsa20-Poly1305, Ed25519)
- SSH key authentication with Ed25519 signatures
- SSH agent integration for password-free authentication
- Known hosts verification with IP-based TOFU
- Client key whitelisting for access control

### Windows Platform Support

- Comprehensive platform abstraction layer
- Windows socket implementation
- CMake build system
- Platform-safe function wrappers

### Testing Infrastructure

- Criterion framework integration
- Parallel test execution with worker pools
- JUnit XML generation for CI/CD
- Coverage support with llvm-cov
- 90+ test files covering all major components

### Feature Additions

- Half-block render mode (PR #71) - 2x vertical resolution
- Customizable ASCII palettes (Issue #61)
- Terminal capability auto-detection (Issue #57)
- Snapshot mode for single frame capture (Issue #45)

## Critical C Programming Patterns

### Integer Overflow Prevention

```c
// ❌ WRONG - can overflow 'int' before conversion
const size_t buffer_size = height * width * bytes_per_pixel;

// ✅ CORRECT - cast to larger type BEFORE multiplication
const size_t buffer_size = (size_t)height * (size_t)width * bytes_per_pixel;
```

### Platform-Safe Socket Handling

```c
// ❌ WRONG - assumes Unix behavior
int sockfd = 0;  // 0 is a valid file descriptor!
if (sockfd > 0) { close(sockfd); }

// ✅ CORRECT - platform-independent
socket_t sockfd = INVALID_SOCKET_VALUE;
if (sockfd != INVALID_SOCKET_VALUE) { socket_close(sockfd); }
```

### Thread-Safe Error Handling

```c
// ❌ WRONG - static buffer not thread-safe
static char error_buf[256];
sprintf(error_buf, "Error: %s", strerror(errno));

// ✅ CORRECT - use thread-local storage
const char* error = SAFE_STRERROR(errno);
```

Remember: **Always cast early, use platform abstractions, and test on multiple platforms!**
