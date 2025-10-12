# ASCII-Chat Development Guide for Claude

## Repository Information
- **Repository Owner**: zfogg (GitHub username)
- **Repository**: ascii-chat

## Essential First Steps
- **ALWAYS** read and understand the `README.md` and `CMakeLists.txt` files first
- Use the test runner script `./tests/scripts/run_tests.sh` for running tests
- Format code with `cmake --build build --target format` after you edit it
- Use `SAFE_MALLOC()` macro from common.h rather than regular `malloc()`
- On macOS: use `lldb` for debugging (gdb doesn't work with this project)
- On Windows: use PowerShell build script `./build.ps1` or CMake directly
- Use `clang` instead of `gcc`
- Don't use `git add .`, add all files individually
- Use AddressSanitizer (ASan) and memory reports from common.c for memory debugging: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
- Use log_*() from logging.c and common.h for logging instead of printf()
- When debugging and testing, make a test_whatever.sh and use that so you don't bother the developer by requesting to run commands over and over

## Project Overview
ASCII-Chat is a terminal-based video chat application that converts webcam video to ASCII art in real-time. It supports multiple clients connecting to a single server, with video mixing and audio streaming capabilities.

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
├── bin/                                            # Hard links to compiled binaries for convenience
├── build/                                          # CMake build directory (all platforms)
├── notes/                                          # Development notes and documentation
├── todo/                                           # Experimental code and future features
├── tests/                                          # Comprehensive test suite using Criterion
│   ├── scripts/                                    # Test infrastructure scripts (NEW)
│   │   └── run_tests.sh                            # Unified test runner with parallel execution
│   ├── unit/                                       # Unit tests for individual components
│   │   ├── audio_test.c                            # Audio system and ringbuffer tests
│   │   ├── buffer_pool_test.c                      # Memory buffer pool tests
│   │   ├── packet_queue_test.c                     # Packet queue and node pool tests
│   │   ├── hashtable_test.c                        # Hash table implementation tests
│   │   ├── crypto_test.c                           # Cryptographic functions tests
│   │   ├── common_test.c                           # Common utilities tests
│   │   ├── logging_test.c                          # Logging system tests
│   │   ├── network_test.c                          # Network protocol tests
│   │   ├── options_test.c                          # Command-line options tests
│   │   ├── simd_caches_test.c                      # SIMD cache management tests
│   │   ├── terminal_detect_test.c                  # Terminal capability detection tests
│   │   └── ascii_simd_test.c                       # SIMD optimization tests
│   ├── integration/                                # Multi-component integration tests
│   │   ├── crypto_network_test.c                   # Crypto + network integration
│   │   ├── server_multiclient_test.c               # Multi-client scenarios
│   │   ├── neon_color_renderers_test.c             # NEON color rendering tests
│   │   └── video_pipeline_test.c                   # End-to-end video pipeline
│   ├── performance/                                # Performance and stress tests
│   │   └── benchmark_test.c                        # Performance benchmarking
│   └── fixtures/                                   # Test data and fixtures
├── src/                                            # Main application entry points
│   ├── server.c                                    # Server main - handles multiple clients
│   └── client.c                                    # Client main - captures/displays video
├── lib/                                            # Core library components
│   ├── common.c/h                                  # Shared utilities, macros, memory debugging, constants
│   ├── platform/                                   # Cross-platform abstraction layer
│   │   ├── README.md                               # Platform abstraction documentation
│   │   ├── abstraction.h                           # Main abstraction header with all API definitions
│   │   ├── abstraction.c                           # Common implementation (currently minimal)
│   │   ├── init.h                                  # Platform initialization helpers and static init wrappers
│   │   ├── posix/                                  # POSIX implementation (Linux/macOS)
│   │   │   ├── thread.c                            # POSIX pthread implementation
│   │   │   ├── mutex.c                             # POSIX mutex implementation
│   │   │   ├── rwlock.c                            # POSIX read-write lock implementation
│   │   │   ├── cond.c                              # POSIX condition variable implementation
│   │   │   ├── terminal.c                          # POSIX terminal I/O implementation
│   │   │   ├── system.c                            # POSIX system functions implementation
│   │   │   └── socket.c                            # POSIX socket implementation
│   │   └── windows/                                # Windows implementation
│   │       ├── thread.c                            # Windows thread implementation
│   │       ├── mutex.c                             # Windows mutex (Critical Section) implementation
│   │       ├── rwlock.c                            # Windows read-write lock (SRW Lock) implementation
│   │       ├── cond.c                              # Windows condition variable implementation
│   │       ├── terminal.c                          # Windows terminal (Console API) implementation
│   │       ├── system.c                            # Windows system functions implementation
│   │       └── socket.c                            # Windows socket (Winsock2) implementation
│   ├── crypto/                                     # Cryptographic protocol implementation
│   │   ├── crypto.c/h                              # Core crypto operations (X25519, XSalsa20-Poly1305)
│   │   ├── handshake.c/h                           # Protocol handshake and mutual authentication
│   │   ├── ssh_agent.c/h                           # SSH agent integration
│   │   ├── known_hosts.c/h                         # Known hosts TOFU verification
│   │   ├── pem_utils.c/h                           # PEM file parsing utilities
│   │   ├── gpg.c/h                                 # GPG key support
│   │   ├── http_client.c/h                         # HTTPS client for key fetching (BearSSL)
│   │   └── keys/                                   # Key management implementations
│   │       ├── keys.c/h                            # Key loading and management API
│   │       ├── types.h                             # Key type definitions
│   │       ├── ssh_keys.c/h                        # SSH Ed25519 key parsing
│   │       ├── gpg_keys.c/h                        # GPG key handling
│   │       ├── https_keys.c/h                      # GitHub/GitLab key fetching
│   │       └── validation.c/h                      # Key validation utilities
│   ├── logging.c                                   # Logging system implementation
│   ├── options.c/h                                 # Command-line argument parsing
│   ├── network.c/h                                 # Network protocol and packet handling
│   ├── packet_queue.c/h                            # Thread-safe per-client packet queues
│   ├── buffer_pool.c/h                             # Memory buffer pool system for efficient allocation
│   ├── hashtable.c/h                               # Hash table implementation for client ID lookup
│   ├── ringbuffer.c/h                              # Lock-free ring buffer implementation
│   ├── compression.c/h                             # Frame compression with zlib
│   ├── crc32_hw.c/h                                # Hardware-accelerated CRC32 checksums
│   ├── mixer.c/h                                   # Audio mixing for multiple clients
│   ├── audio.c/h                                   # Audio capture/playback (PortAudio)
│   ├── image2ascii/                                # Image processing and ASCII conversion
│   │   ├── image.c/h                               # Image processing and manipulation
│   │   ├── ascii.c/h                               # ASCII art conversion and grid layout
│   │   └── simd/                                   # Architecture-specific SIMD optimizations
│   │       ├── common.h                            # Shared SIMD interface definitions
│   │       ├── output_buffer.h                     # Output buffer management
│   │       ├── neon.c/h                            # ARM NEON optimizations (16 pixels/cycle)
│   │       ├── sse2.c/h                            # Intel SSE2 optimizations (16 pixels/cycle)
│   │       ├── ssse3.c/h                           # Intel SSSE3 optimizations (32 pixels/cycle)
│   │       ├── avx2.c/h                            # Intel AVX2 optimizations (32 pixels/cycle)
│   │       └── sve.c/h                             # ARM SVE optimizations (scalable vectors)
│   ├── aspect_ratio.c/h                            # Aspect ratio calculations
│   ├── ascii_simd.c/h                              # SIMD dispatch and common cache management
│   ├── ascii_simd_color.c                          # SIMD color ASCII conversion implementation
│   ├── ansi_fast.c/h                               # Optimized ANSI escape sequence generation
│   ├── terminal_detect.c/h                         # Terminal capability detection
│   ├── simd_caches.c/h                             # SIMD cache management
│   ├── palette.c/h                                 # ASCII palette management
│   ├── webcam.c/h                                  # Webcam capture abstraction layer
│   ├── webcam_platform.c/h                         # Platform-specific webcam detection
│   ├── webcam_avfoundation.m                       # macOS webcam implementation (AVFoundation)
│   ├── webcam_v4l2.c                               # Linux webcam implementation (Video4Linux2)
│   ├── webcam_windows.c                            # Windows webcam implementation stub (NEW)
│   └── round.h                                     # Rounding utilities
├── CMakeLists.txt                                  # CMake build configuration (all platforms)
├── Info.plist                                      # macOS application metadata
└── CLAUDE.md                                       # Development guide (this file)
```

## Platform Support and Building

### Windows Support (NEW - September 2025)
ASCII-Chat now has comprehensive Windows support through a platform abstraction layer:

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

# Build with MinGW mode (GCC or Clang)
./build.ps1 -MinGW

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

# Run tests after building (requires Criterion - typically only works with MinGW)
./build.ps1 -Test
```

**Note**: The Criterion test framework is primarily Unix-based and typically only works on Windows when using MinGW/MSYS2 with pkg-config installed. Native Windows builds usually cannot run the tests.

The build script automatically:
- Kills any running ASCII-Chat processes before building
- Detects and uses the best available compiler (Clang > MSVC > GCC)
- Uses Ninja if available for faster builds
- Creates hard links in `bin/` directory for consistency with Unix builds
- Copies runtime DLLs to the correct location
- Links compile_commands.json to repo root for IDE integration

Manual CMake build (if not using build.ps1):
```bash
# Use CMake with Clang (recommended)
cmake -B build -G "Ninja" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Or with Visual Studio
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug

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
cmake --build build
./tests/scripts/run_tests.sh
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

## Testing Framework (UPDATED - Use run_tests.sh!)

### The Test Runner Script
**IMPORTANT**: Always use `./tests/scripts/run_tests.sh` for running tests!

**Note**: The test script automatically builds any tests it needs to run, so you never need to manually build test targets. Just run the test script with your desired options and it handles all compilation automatically based on the build mode you select.

The test runner provides:
- **Automatic test compilation** - builds tests as needed, no manual build required
- **Parallel test execution** with automatic CPU core detection
- **Worker pool architecture** for efficient resource utilization
- **JUnit XML generation** for CI/CD integration
- **Coverage support** with llvm-cov
- **Test filtering** for running specific test cases
- **Detailed logging** with customizable output files
- **Signal handling** for clean Ctrl-C interruption

### Basic Test Usage
```bash
# Run all tests (recommended default)
./tests/scripts/run_tests.sh

# Run specific test category
./tests/scripts/run_tests.sh unit           # All unit tests
./tests/scripts/run_tests.sh integration    # All integration tests
./tests/scripts/run_tests.sh performance    # All performance tests

# Run single test by name
./tests/scripts/run_tests.sh test_unit_buffer_pool
./tests/scripts/run_tests.sh unit buffer_pool  # Alternative syntax

# Run multiple tests
./tests/scripts/run_tests.sh unit buffer_pool packet_queue hashtable

# Filter tests within a binary
# IMPORTANT: Always use "*filter*" format with Criterion (wildcard matching on both sides)
./tests/scripts/run_tests.sh test_unit_buffer_pool -f "*creation*"  # Correct format
./tests/scripts/run_tests.sh test_unit_ascii -f "*convert*"        # Matches all tests with "convert" in name
./tests/scripts/run_tests.sh test_unit_ascii -f "*read_init*"      # Matches ascii_read_init_basic, etc.
```

### Advanced Test Options
```bash
# Different build types
./tests/scripts/run_tests.sh -b release      # Optimized build
./tests/scripts/run_tests.sh -b coverage     # Coverage instrumentation
./tests/scripts/run_tests.sh -b debug        # AddressSanitizer enabled (default)
./tests/scripts/run_tests.sh -b dev          # Debug without sanitizers (faster)

# Generate JUnit XML for CI
./tests/scripts/run_tests.sh -J

# Custom logging
./tests/scripts/run_tests.sh --log-file=/tmp/custom_test.log

# Control parallelism
./tests/scripts/run_tests.sh -j 4            # Use 4 parallel jobs
./tests/scripts/run_tests.sh --no-parallel   # Disable parallel execution

# Verbose output
./tests/scripts/run_tests.sh -v

# Combined options
./tests/scripts/run_tests.sh -b coverage -J -l /tmp/coverage.log unit
```

### Test Categories

#### Unit Tests (`tests/unit/`)
- **Core Infrastructure**: buffer_pool, packet_queue, hashtable, ringbuffer
- **Network & Protocol**: network, packet handling, CRC validation
- **Audio System**: audio capture/playback, mixing, ringbuffer operations
- **SIMD Optimizations**: NEON, SSE2, SSSE3, AVX2 implementations
- **Terminal Detection**: capability detection, color mode support
- **Utilities**: logging, options parsing, common functions

#### Integration Tests (`tests/integration/`)
- **Crypto + Network**: encrypted packet transmission
- **Multi-client Scenarios**: concurrent connections, broadcast messaging
- **Video Pipeline**: end-to-end frame capture and display
- **NEON Color Rendering**: integrated SIMD color processing

#### Performance Tests (`tests/performance/`)
- **Benchmark Suite**: throughput testing, latency measurements
- **Stress Testing**: maximum client handling, memory pressure tests

### Test Infrastructure Details
The test runner uses a sophisticated worker pool architecture:
- Automatically detects CPU cores and allocates resources optimally
- Runs tests in parallel with `CORES/2` concurrent executables
- Each test gets `CORES` jobs for internal parallelism (Criterion feature)
- Proper signal handling for clean Ctrl-C interruption
- File locking for thread-safe JUnit XML generation
- Detailed failure reporting with last 50 lines of output

### Criterion Parameterized Test Requirements

**CRITICAL**: Parameterized tests have special memory requirements!

When using Criterion's parameterized tests, any **pointers** in test parameter structures require Criterion's allocators (`cr_malloc`, `cr_calloc`, `cr_realloc`, `cr_free`). Using regular `malloc`/`free` or string literals causes **undefined behavior** and crashes.

**Best Practice for This Project**: Use fixed-size char arrays to avoid allocation code:

```c
// ✅ RECOMMENDED - Use char arrays (simpler, no allocation needed)
typedef struct {
  char ip[256];              // Fixed-size array
  char description[64];      // Fixed-size array
  int expected_result;
} test_case_t;

static test_case_t cases[] = {
    {"192.0.2.1", "example IP", 1},
    {"invalid", "bad IP", 0}
};

// ⚠️ ALTERNATIVE - Pointers with cr_malloc (more complex, rarely needed)
typedef struct {
  const char *ip;
  const char *description;
  int expected_result;
} test_case_t;

ParameterizedTestParameters(suite, test) {
  static test_case_t *cases = NULL;
  if (!cases) {
    cases = cr_malloc(2 * sizeof(test_case_t));
    cases[0] = (test_case_t){
      .ip = cr_strdup("192.0.2.1"),
      .description = cr_strdup("example IP"),
      .expected_result = 1
    };
    // ...
  }
  return cr_make_param_array(test_case_t, cases, 2);
}

// ❌ WRONG - String literals with pointers will crash!
typedef struct {
  const char *ip;            // Pointer to string literal - CRASHES!
  const char *description;   // Pointer to string literal - CRASHES!
  int expected_result;
} test_case_t;

static test_case_t cases[] = {
    {"192.0.2.1", "example", 1}  // String literals - UNDEFINED BEHAVIOR!
};
```

See [Criterion Parameterized Tests Documentation](https://criterion.readthedocs.io/en/master/parameterized.html) for details.

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

### Windows Command Execution Notes

**IMPORTANT**: When working in the Windows bash environment:
- You are running in a bash shell, NOT PowerShell or CMD
- To run PowerShell commands, use: `powershell -Command "Your-Command"`
- To run PowerShell scripts, use: `./script.ps1` or `powershell ./script.ps1`
- Use double slashes for Windows command flags: `taskkill //F //IM` not `/F /IM`
- For delays, use `sleep` (bash) not `Start-Sleep` (PowerShell)
- Background processes: use `&` at the end of commands

### 1. Environment Variables

**Supported Environment Variables:**

**Cryptography:**
- `$SSH_AUTH_SOCK` - SSH agent socket path for password-free key authentication (Unix only)
- `$ASCII_CHAT_SSH_PASSWORD` - Passphrase for encrypted SSH keys (⚠️ sensitive, prefer ssh-agent)
- `$ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK` - Set to `1` to disable known_hosts verification (⚠️ DANGER: enables MITM attacks)

**Terminal Detection:**
- `$TERM` - Terminal type detection for capability negotiation
- `$LANG`, `$LC_ALL`, `$LC_CTYPE` - Locale settings for UTF-8 support
- `$LINES`, `$COLUMNS` - Terminal dimensions when auto-detection fails
- `$COLORTERM` - Enhanced color capability detection
- `$TTY` - TTY device path detection

**System:**
- `$USER` - User identification for logging
- `$TESTING`, `$CRITERION_TEST` - Enable fast test mode
- `$LOG_LEVEL` - Enable logging at a certain level (DEBUG/0, INFO/1, WARN/2, ERROR/3, FATAL/4)

**IMPORTANT**: The project does NOT support `WEBCAM_DISABLED=1`. A webcam must be available.

### 2. Enable Debug Logging
Add these defines to see detailed logs:
```c
/*#define DEBUG_MEMORY*/     // Memory debugging (enabled by default with CMAKE_BUILD_TYPE=Debug)
#define DEBUG_NETWORK        // Network packet details
#define DEBUG_COMPRESSION    // Compression statistics
#define DEBUG_AUDIO          // Audio packet info
#define DEBUG_THREADS        // Thread lifecycle
#define DEBUG_MIXER          // Audio mixing details
```

### 3. Common Issues and Solutions

**Windows-specific Issues:**
- **Socket initialization**: Always check for `INVALID_SOCKET_VALUE` not -1 or 0
- **File descriptor issues**: Use `socket_close()` not `close()` for sockets
- **Assertion failures**: Check for proper initialization (`sockfd = INVALID_SOCKET_VALUE`)
- **Build errors**: Use CMake for all platforms

**"Unknown packet type" errors:**
- Check packet type enum values in network.h
- Verify receive_packet() in network.c has cases for ALL packet types
- Common issue: Missing validation cases for PACKET_TYPE_ASCII_FRAME

**"DEADBEEF" pattern in packet data:**
- Indicates TCP stream corruption/desynchronization
- Usually caused by concurrent socket writes without synchronization
- Solution: Use per-client packet queues with dedicated send threads

**Memory crashes:**
- Always use SAFE_MALLOC() macro instead of malloc()
- Check framebuffer operations - common source of use-after-free
- Build with AddressSanitizer: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`

### 4. Debugging Tools

```bash
# Monitor network traffic
sudo tcpdump -i lo0 port 8080 -X  # macOS
sudo tcpdump -i lo port 8080 -X    # Linux
netsh trace start capture=yes tracefile=trace.etl provider=Microsoft-Windows-TCPIP # Windows

# Memory debugging
leaks --atExit -- ./build/bin/ascii-chat server     # macOS
valgrind ./build/bin/ascii-chat server               # Linux
# Windows: Use Visual Studio diagnostics or Application Verifier

# Debuggers
lldb ./build/bin/ascii-chat server                   # macOS (NOT gdb!)
gdb ./build/bin/ascii-chat server                    # Linux
windbg ./build/bin/ascii-chat.exe server # Windows

# Process monitoring
lsof -p $(pgrep ascii-chat)          # Unix file descriptors
ps -M $(pgrep ascii-chat)            # macOS threads
ps -eLf | grep ascii-chat            # Linux threads
tasklist //FI "IMAGENAME eq ascii-chat.exe" # Windows processes (NOTE: double slashes from bash!)

# Windows process termination (NOTE: Use double slashes from bash!)
taskkill //F //IM ascii-chat.exe   # Force kill by image name
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

### Overview
ASCII-Chat implements **end-to-end encryption by default** using libsodium with modern cryptographic primitives:

**Algorithms:**
- **X25519** - Elliptic curve Diffie-Hellman key exchange (32-byte keys)
- **XSalsa20-Poly1305** - Authenticated encryption (AEAD cipher)
- **Ed25519** - Digital signatures for SSH key authentication
- **Argon2id** - Memory-hard password hashing (64MB, interactive mode)

**Key Features:**
- ✅ **Forward secrecy** - Ephemeral X25519 keys per connection
- ✅ **SSH agent support** - Password-free authentication (Unix)
- ✅ **Known hosts verification** - SSH-style TOFU (Trust On First Use)
- ✅ **Client whitelisting** - Server-side access control
- ✅ **GitHub/GitLab integration** - Fetch public keys via HTTPS

### Authentication Modes

**1. Default (Ephemeral DH):**
```bash
./ascii-chat-server
./ascii-chat-client
# Privacy: encrypted against eavesdropping
# Security: ❌ vulnerable to MITM (no identity verification)
```

**2. Password Authentication:**
```bash
./ascii-chat-server --password "shared_secret"
./ascii-chat-client --password "shared_secret"
# Binds password HMAC to DH shared_secret for MITM protection
```

**3. SSH Key Authentication:**
```bash
# Server with SSH key (prompts for passphrase or uses ssh-agent)
./ascii-chat-server --key ~/.ssh/id_ed25519

# Client verifies server identity
./ascii-chat-client --server-key ~/.ssh/server_id_ed25519.pub
# OR fetch from GitHub:
./ascii-chat-client --server-key github:zfogg
```

**4. Client Whitelisting:**
```bash
# Server only accepts pre-approved client keys
./ascii-chat-server --client-keys ~/.ascii-chat/authorized_clients.txt
```

**5. Defense in Depth (All features):**
```bash
./ascii-chat-server --key ~/.ssh/id_ed25519 --password "pass" --client-keys allowed.txt
./ascii-chat-client --key ~/.ssh/id_ed25519 --password "pass" --server-key github:zfogg
```

### SSH Agent Integration
**Automatic key detection:** When you provide an encrypted SSH key via `--key`, ASCII-Chat automatically checks if that key is in your SSH agent:
- **If in agent:** Uses agent for signatures, no password prompt
- **If not in agent:** Prompts for password, then auto-adds key to agent for future use
- **Environment:** Requires `$SSH_AUTH_SOCK` (Unix/macOS only, not Windows)

### Known Hosts Verification
**File location:** `~/.ascii-chat/known_hosts`
**Format:** `<IP:port> x25519 <hex-key> [comment]`

First connection to a new server prompts:
```
The authenticity of host '192.168.1.100:27224' can't be established.
Ed25519 key fingerprint is: SHA256:abc123...
Are you sure you want to continue connecting (yes/no)? yes
```

Subsequent connections verify against stored key. Key changes trigger warnings (MITM detection).

**Note:** Keys are bound to **IP addresses**, not hostnames, to prevent DNS hijacking attacks.

### Security Considerations
**Strengths:**
- ✅ Modern crypto primitives (X25519, XSalsa20-Poly1305, Argon2id)
- ✅ Forward secrecy (ephemeral DH keys per session)
- ✅ Authenticated encryption (Poly1305 MAC prevents tampering)
- ✅ Mutual authentication (both client and server prove shared secret)

**Weaknesses:**
- ⚠️ Default mode vulnerable to MITM (use `--server-key` or known_hosts)
- ⚠️ Password quality matters (use strong passwords for `--password`)
- ⚠️ Known hosts TOFU vulnerable on first connection

**For detailed protocol specification, see `docs/crypto.md`**

## Git Workflow

### Working with Issues
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

### Creating Pull Requests
```bash
# Use gh CLI for PR
gh pr create --title "Fix: Issue description" --body "## Summary
- Fixed X by doing Y
- Updated Z to prevent future issues

## Test Plan
- [ ] Server starts without errors
- [ ] Multiple clients can connect
- [ ] Video displays correctly
- [ ] No memory leaks
- [ ] All tests pass: ./tests/scripts/run_tests.sh

Fixes #issue-number

🤖 Generated with [Claude Code](https://claude.ai/code)"
```

## Architecture Notes

### Per-Client Threading Architecture (September 2025)
ASCII-Chat uses a high-performance per-client threading model:
- **Each client gets 2 dedicated threads**: 1 video render (60 FPS) + 1 audio render (172 FPS)
- **Linear performance scaling**: No shared bottlenecks, scales to 9+ clients
- **Thread-safe design**: Proper synchronization eliminates race conditions
- **Real-time performance**: 60 FPS video + 172 FPS audio maintained per client

### Critical Synchronization Rules

#### Lock Ordering Protocol (CRITICAL - Prevents Deadlocks)
**Always acquire locks in this exact order:**
1. **Global RWLock** (`g_client_manager_rwlock`)
2. **Per-Client Mutex** (`client_state_mutex`)
3. **Specialized Mutexes** (`g_stats_mutex`, `g_frame_cache_mutex`, etc.)

#### Per-Client Mutex Protection
```c
// ✅ CORRECT: Mutex-protected client state access
mutex_lock(&client->client_state_mutex);
uint32_t client_id_snapshot = client->client_id;
bool active_snapshot = client->active;
mutex_unlock(&client->client_state_mutex);

// ❌ WRONG: Direct access to client fields
if (client->active && client->has_video) {  // RACE CONDITION!
  process_client(client->client_id);
}
```

### Memory Management Rules
1. Always use SAFE_MALLOC(), SAFE_REALLOC(), SAFE_CALLOC() instead of malloc/realloc/calloc
2. Framebuffer owns its data - don't free it
3. Packet queues copy data if owns_data=true
4. Set pointers to NULL after freeing
5. Use platform abstraction for thread operations

### ASCII Grid Layout
- Dynamic layout: 2 side-by-side, 2x2, 3x2, 3x3 (up to 9 clients)
- Each section shows one client's webcam as ASCII art
- Empty slots remain blank
- Grid coordinates: (0,0) top-left to (cols-1, rows-1)
- Automatic padding and centering based on terminal size

## Manual Testing Checklist

Before committing any changes:
1. [ ] `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build` - rebuild with sanitizers
2. [ ] `cmake --build build --target format` - code is properly formatted
3. [ ] **Run tests**: `./tests/scripts/run_tests.sh`
4. [ ] Start server, connect 2+ clients
5. [ ] Video displays in correct grid layout
6. [ ] No "DEADBEEF" or "Unknown packet type" errors
7. [ ] No CRC checksum mismatches
8. [ ] Audio works (if testing audio changes)
9. [ ] Clients can disconnect/reconnect cleanly
10. [ ] No crashes over 1+ minute runtime
11. [ ] Check with AddressSanitizer if changed memory handling
12. [ ] Verify tests pass with coverage: `./tests/scripts/run_tests.sh -b coverage`

## Development Principles

### No Emergency Cleanup Code
**NEVER** write "emergency cleanup" or "force cleanup" code. This indicates improper resource management.

**Instead, Code Properly From Start:**
- Assume threads and networks have **irregular, unpredictable timing**
- Design resource ownership to be **unambiguous** (single owner per resource)
- Use **deterministic cleanup sequences** that work regardless of timing
- **Wait for threads properly** before cleaning up their resources
- Use **reference counting** for complex resource lifetimes

### Platform Portability
- Always use platform abstraction layer for threads, mutexes, sockets
- Use SAFE_* macros for platform-specific functions
- Test on multiple platforms when possible
- Initialize sockets to INVALID_SOCKET_VALUE, not 0 or -1

## Important Files to Understand

### Core Infrastructure
1. **platform.h/c**: Platform abstraction layer - CRITICAL for Windows support
2. **network.h/c**: Defines ALL packet types and protocol
3. **lib/crypto/**: Cryptographic protocol implementation
   - `crypto.c/h`: Core crypto operations (X25519 DH, XSalsa20-Poly1305 AEAD)
   - `handshake.c/h`: Protocol handshake and mutual authentication
   - `ssh_agent.c/h`: SSH agent integration for password-free auth
   - `known_hosts.c/h`: IP-based TOFU verification
   - `keys/keys.c/h`: Key loading and management API
   - `keys/ssh_keys.c/h`: SSH Ed25519 key parsing
   - `keys/https_keys.c/h`: GitHub/GitLab key fetching
   - `http_client.c/h`: HTTPS client using BearSSL
4. **packet_queue.c**: Thread-safe per-client queue implementation
5. **buffer_pool.c/h**: Memory buffer pool for efficient allocation
6. **hashtable.c/h**: Hash table for client ID lookup

### Main Application
7. **server.c**:
   - `video_broadcast_thread`: Mixes and sends video
   - `audio_mixer_thread`: Mixes and sends audio
   - `client_thread_func`: Handles individual client
8. **client.c**:
   - `handle_ascii_frame_packet`: Processes received video
   - `video_capture_thread_func`: Captures and sends webcam

### Media Processing
9. **lib/image2ascii/ascii.c**: ASCII conversion and grid layout
10. **mixer.c**: Multi-client audio mixing with ducking
11. **ringbuffer.c**: Framebuffer for multi-frame storage
12. **ascii_simd.c/h**: SIMD optimization dispatch
13. **ansi_fast.c**: Optimized ANSI escape sequences

### Testing Infrastructure
14. **tests/scripts/run_tests.sh**: Main test runner - USE THIS!
15. **tests/unit/**: Unit test implementations
16. **CMakeLists.txt**: Cross-platform build configuration

## Recent Updates (September 2025)

### Cryptography Implementation (October 2025)
- End-to-end encryption with libsodium (X25519, XSalsa20-Poly1305, Ed25519)
- SSH key authentication with Ed25519 signatures
- SSH agent integration for password-free authentication
- Known hosts verification with IP-based TOFU
- Client key whitelisting for access control
- Mutual authentication protocol (client + server proof)
- Forward secrecy with ephemeral DH keys

### Windows Platform Support (PR #ea36dbb)
- Comprehensive platform abstraction layer
- Windows socket implementation
- CMake build system
- Platform-safe function wrappers
- Thread-local storage for thread safety

### Testing Infrastructure (PR #76, #81)
- Criterion framework integration
- Parallel test execution with worker pools
- JUnit XML generation for CI/CD
- Coverage support with llvm-cov
- 90+ test files covering all major components

### Performance Optimizations (PR #73)
- Full SIMD vectorization for monochrome mode
- Advanced UTF-8 cache system
- 10.5x string generation speedup
- Precomputed lookup tables

### Feature Additions
- Half-block render mode (PR #71) - 2x vertical resolution
- Customizable ASCII palettes (Issue #61)
- Terminal capability auto-detection (Issue #57)
- Snapshot mode for single frame capture (Issue #45)

## SIMD Optimization Notes

**Current Performance (September 2025):**
- Terminal 203×64: 0.131ms/frame (FG), 0.189ms/frame (BG)
- 10.5x string generation speedup with lookup tables
- 8.4x overall speedup for terminal-sized frames

**Key Success Factors:**
1. Fix algorithmic complexity first (O(n²) → O(n))
2. Optimize string generation (snprintf → lookup tables)
3. Profile end-to-end pipeline, not just hot spots
4. SIMD works best when string generation is already optimized

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
