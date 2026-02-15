# ascii-chat Development Guide for Claude

## Repository Information

- **Repository Owner**: zfogg (GitHub username)
- **Repository**: ascii-chat
- **Description**: Terminal-based video chat with ASCII art rendering, end-to-end encryption, and multi-client support

## Quick Start

### Building

```bash
# Configure with Ninja (default preset)
cmake --preset default -B build

# Build
cmake --build build

# Run tests
cmake --build build --target tests
ctest --test-dir build --output-on-failure --parallel 0
```

**IMPORTANT**:
- This project uses Clang only, not MSVC or GCC
- Use `cmake --preset default` - use Ninja and don't switch to Makefiles or Visual Studio generators
- **Avoid deleting build/** - just re-run `cmake --preset default` and rebuild

### Running

```bash
# Server mode (listens on port 27224)
./build/bin/ascii-chat server

# Client mode (connects to localhost:27224)
./build/bin/ascii-chat client

# Mirror mode (local webcam preview, no networking)
./build/bin/ascii-chat mirror

# Discovery service (ACDS)
./build/bin/acds --port 27225
```

Get full options with: `./build/bin/ascii-chat <mode> --help`

## Binary Modes

ascii-chat binary has four primary modes:

1. **server** - Video broadcast server, handles multiple clients, mixes audio, generates ASCII grid
2. **client** - Connects to server, sends webcam frames, receives mixed ASCII output
3. **mirror** - Local mode for testing webcam/files/ulrs/encoding/format without networking or crypto
4. **discovery-service** - Session signalling and discovery webrtc server
5. **discovery** - The mode that connects via session string via a discovery-service instance. ascii-chat binary runs as
   this mode by default when you don't pass a mode as a first positional argument.

## Debugging Flags

### Snapshot Mode (Single Frame Capture)

```bash
# Capture exactly one frame and exit
./build/bin/ascii-chat client --snapshot --snapshot-delay 0
# Same thing
./build/bin/ascii-chat client --snapshot -S -D 0

# Capture for N seconds then exit (useful for testing), printing every frame to stdout
./build/bin/ascii-chat mirror --snapshot --snapshot-delay 5

# Common pattern: quick test without continuous capture
./build/bin/ascii-chat mirror --snapshot --snapshot-delay 0 --volume 0
```

### Log Filtering with --grep

Searches logs and log headers. Supports two formats:
- **Format 1**: `/pattern/flags` - Regex with flags (case-insensitive, context lines, etc.)
- **Format 2**: `pattern` - Plain regex without slashes or flags

```bash
# Plain regex format (no slashes, no flags)
./build/bin/ascii-chat server --grep "DEBUG"
./build/bin/ascii-chat server --grep "handshake|crypto"
./build/bin/ascii-chat client --grep "ERROR|WARN"

# Slash format with flags
./build/bin/ascii-chat server --grep "/handshake/i"        # Case-insensitive
./build/bin/ascii-chat client --grep "/ERROR/C2"           # 2 lines of context
./build/bin/ascii-chat client --grep "/(YO)/F"             # Fixed string (literal)

# Flags: i(case-insensitive), F(fixed string), C/A/B(context), g(global highlight), I(invert)
./build/bin/ascii-chat client --grep "/test/igC2"
./build/bin/ascii-chat client --grep "/test/igB1A3"

# Pass --grep multiple times to OR-logic multiple patterns (can mix formats)
./build/bin/ascii-chat client --grep "/test1/igB1A3" --grep "test2"

# Combine with log levels
./build/bin/ascii-chat --log-level debug server --grep "crypto"
```

### Other Debug Options

```bash
# Log to file instead of stdout+stderr
./build/bin/ascii-chat --log-file /tmp/server.log server

# Increase verbosity
./build/bin/ascii-chat -VV server  # -V, -VV, -VVV

# Set log level explicitly
./build/bin/ascii-chat --log-level dev server
```

## Error Handling with asciichat_errno_t

**ALWAYS use asciichat_error_t for return types, not int:**

```c
// ✅ CORRECT
asciichat_error_t process_data(void) {
    if (some_error) {
        return SET_ERRNO(ERROR_INVALID_STATE, "Failed to process: %s", reason);
    }
    return ASCIICHAT_OK;
}

// ❌ WRONG
int process_data(void) {
    if (some_error) {
        log_error("Failed");
        return -1;  // No context, no error tracking!
    }
    return 0;
}
```

**Error checking:**

```c
asciichat_error_t result = do_something();
if (result != ASCIICHAT_OK) {
    asciichat_error_context_t ctx;
    if (HAS_ERRNO(&ctx)) {
        log_error("Operation failed: %s", ctx.context_message);
    }
    return result;
}
```

## Memory Management

**Use memory tracking macros, not raw malloc/free:**

```c
// ✅ CORRECT
uint8_t *buffer = SAFE_MALLOC(1024, uint8_t *);
client_t *client = SAFE_CALLOC(1, sizeof(client_t), client_t *);
SAFE_FREE(buffer);

// ❌ WRONG
uint8_t *buffer = malloc(1024);  // No leak tracking!
free(buffer);
```

Memory reports show at program exit in debug builds. Red numbers indicate leaks.

## Platform Abstraction Layer (lib/platform)

Cross-platform support via abstraction:

- **lib/platform/posix/** - Linux/macOS implementations
- **lib/platform/windows/** - Windows implementations
- **lib/platform/abstraction.h** - Platform-independent API

Key abstractions:
- Threads: `asciichat_thread_t`, `asciichat_thread_create()`
- Mutexes: `mutex_t`, `mutex_init/lock/unlock()`
- Sockets: `socket_t`, `socket_close()`, `INVALID_SOCKET_VALUE`
- Safe wrappers: `SAFE_GETENV()`, `SAFE_STRERROR()`, `platform_open()`

Always use abstraction layer functions, never direct POSIX/Windows APIs.

## Session Management (lib/session)

The session layer handles:
- **display.c** - Terminal capability detection, ASCII rendering setup
- **splash.c** - Intro splash screen with rainbow animation and log capture
- **help_screen.c** - Interactive help UI
- **server_status.c** - Server status screen with live log feed
- **discovery_status.c** - Discovery UI
- **session_log_buffer.c** - Shared log buffer for session screens

### Session Screens (Status + Splash)

Both status screen and splash screen use the same "fixed header + scrolling logs" pattern:
- Fixed header at top (status info or animated ASCII art)
- Logs captured in `session_log_buffer` (100-entry circular buffer)
- Logs displayed below header, calculated to fill exactly `term_rows - header_lines - 1`
- Screen never scrolls past bottom (prevents terminal flashing)

**Implementation:**
- `session_log_buffer_init()` - Initialize log capture (called by splash/status init)
- `session_log_buffer_append()` - Called from `lib/log/logging.c` to capture messages
- `session_log_buffer_get_recent()` - Retrieve N most recent entries for display
- `session_log_buffer_clear()` - Clear initialization logs before screen starts

**Testing:**
```bash
# Manual test script
./tests/manual/test_session_screens.sh

# Server status screen
./build/bin/ascii-chat server --status-screen

# Client splash screen (shows during connection)
./build/bin/ascii-chat client
```

## Media Resolution and Streaming

ascii-chat supports media from files and URLs using a smart resolution system:

### Supported Media Sources

**Direct Streams & Files (FFmpeg-handled):**
- Local files: `.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`, `.flv`, `.gif`, `.jpg`, `.png`, etc.
- Streaming protocols: `http://`, `https://`, `rtsp://`, `rtmp://`, HLS (`.m3u8`), DASH
- Any format or protocol supported by FFmpeg

**Complex Streaming Sites (yt-dlp-resolved):**
- 1000+ supported sites including YouTube, TikTok, Twitch, Reddit, Instagram, Twitter, etc.
- Requires `yt-dlp` installed: `pip install yt-dlp` or `brew install yt-dlp`
- yt-dlp extracts the actual playable stream URL, which is then handled by FFmpeg

### Smart Resolution Strategy

The media source resolver (`lib/media/source.c`) uses this strategy:

1. **Direct detection**: Check if URL has a direct file extension (`.mp4`, `.mkv`, `.m3u8`, etc.) or protocol (`rtsp://`, `rtmp://`) → use FFmpeg directly (fastest)
2. **yt-dlp extraction**: If not a direct stream, try to extract stream URL using yt-dlp (for YouTube, TikTok, etc.)
3. **Fallback**: If yt-dlp fails, try FFmpeg as a last resort (may fail for truly unsupported sites)
4. **Error reporting**: Log failures at each step with the `--grep` system

### Usage Examples

```bash
# Direct file (FFmpeg handles it)
./build/bin/ascii-chat mirror --file /path/to/video.mp4

# Direct HTTP stream (FFmpeg handles it)
./build/bin/ascii-chat mirror --url "https://example.com/stream.m3u8"

# YouTube video (yt-dlp extracts stream, FFmpeg plays it)
./build/bin/ascii-chat mirror --url "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

# TikTok video (yt-dlp extracts stream)
./build/bin/ascii-chat mirror --url "https://www.tiktok.com/@user/video/123456"

# Custom yt-dlp options
./build/bin/ascii-chat mirror --url "https://www.youtube.com/..." --yt-dlp-options "--no-warnings --restrict-filenames"

# Server streaming media to clients
./build/bin/ascii-chat server --file video.mp4
./build/bin/ascii-chat server --url "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
```

### Caching

Stream URLs extracted by yt-dlp are cached for 30 seconds to avoid repeated subprocess calls during initialization (FPS detection, audio probing, format detection). Cache keys include both URL and yt-dlp options.

### Debugging Media Resolution

```bash
# Watch media resolution process
./build/bin/ascii-chat --log-level debug mirror --file video.mp4 --grep "Media source|resolve|stream"

# Watch yt-dlp execution
./build/bin/ascii-chat --log-level debug mirror --url "https://www.youtube.com/watch?v=..." --grep "yt-dlp"

# Check FFmpeg fallback
./build/bin/ascii-chat --log-level debug mirror --url "https://example.com/unknown-format" --grep "ffmpeg|stream"
```

### Implementation Details

- **lib/media/yt_dlp.c/h** - yt-dlp subprocess integration with caching
- **lib/media/source.c** - Smart routing logic and media source creation
- **include/ascii-chat/options/options.h** - `yt_dlp_options` STRING option

## Cryptographic Protocol

### End-to-End Encryption

ascii-chat uses libsodium for E2E encryption by default:
- **X25519** - Key exchange
- **XSalsa20-Poly1305** - AEAD cipher
- **Ed25519** - SSH key signatures

### Authentication Modes

```bash
# 1. Ephemeral DH (default) - encrypted but no identity verification
./build/bin/ascii-chat server
./build/bin/ascii-chat client

# 2. Password authentication
./build/bin/ascii-chat server --password "secret"
./build/bin/ascii-chat client --password "secret"

# 3. SSH key authentication (TOFU with known_hosts)
./build/bin/ascii-chat server --key ~/.ssh/id_ed25519
./build/bin/ascii-chat client --server-key ~/.ssh/server_id_ed25519.pub

# 4. Fetch keys from GitHub
./build/bin/ascii-chat client --server-key github:zfogg
```

### Debugging Crypto Handshake

```bash
# Watch handshake process
./build/bin/ascii-chat --log-level debug server --grep "handshake"
./build/bin/ascii-chat --log-level debug client --grep "handshake|crypto"

# Check known_hosts verification
./build/bin/ascii-chat --log-level debug client --grep "known_hosts"
```

See `docs/crypto.md` for full protocol specification.

## ACDS (ascii-chat Discovery Service)

The Discovery Service enables automatic session discovery and connection:

### Running ACDS

```bash
# Start discovery server
./build/bin/ascii-chat discovery-service --port 27225 --database ./acds.db

# Register with ACDS (server mode)
./build/bin/ascii-chat server --discovery-service --discovery-service-port 27225

# Find sessions via ACDS (client mode)
./build/bin/ascii-chat client --discovery-service --discovery-service-port 27225
```

### ACDS Features

- Session registration with unique string identifiers (e.g., "blue-mountain-tiger")
- IP privacy (requires explicit `--discovery-expose-ip` confirmation)
- SQLite database for session persistence
- Automatic cleanup of stale sessions

### Debugging ACDS

```bash
# Server side
./build/bin/ascii-chat --log-level dev server --acds --grep "/acds|discovery/ig"

# Client side
./build/bin/ascii-chat --log-level debug client --acds --grep "/acds|discovery/ig"

# ACDS server itself
./build/bin/acds --log-level debug --grep "/session|register/ig"
```

## Network Protocol Debugging

### Packet Structure

Every packet has:
- **Magic**: ASCIICCHAT spelled in hexadecimal as best we could (validation)
- **Type**: packet_type_t enum
- **Length**: payload size
- **CRC32**: payload checksum
- **Client ID**: sender identification

### Common Packet Issues

**magic constant in data**: Stream desynchronization, missing synchronization

```bash
# Debug packet flow
./build/bin/ascii-chat --log-level debug server --grep "/packet|recv|send/ig"
```

**CRC mismatches**: Data corruption or incomplete reads

```bash
# Watch for CRC errors
./build/bin/ascii-chat server --grep "/CRC|checksum/ig"
```

**Unknown packet type**: Client/server version mismatch

```bash
# Check packet types being sent/received
./build/bin/ascii-chat --log-level debug client --grep "/packet_type/ig"
```

## Project Structure

```
ascii-chat/
├── src/                    # Mode entry points
│   ├── main.c              # Unified binary (mode routing)
│   ├── server/             # Server mode
│   ├── client/             # Client mode
│   ├── mirror/             # Mirror mode
│   └── discovery-service/  # Discovery service mode / ACDS
│   └── discovery/          # Discovery mode for connecting to sessions via acds
├── lib/                    # Core libraries
│   ├── platform/           # Cross-platform abstractions
│   ├── session/            # Session management (display, splash, status)
│   ├── crypto/             # Encryption & authentication
│   ├── network/            # Protocol & packet handling
│   ├── video/              # Webcam, ASCII conversion, SIMD
│   ├── audio/              # PortAudio capture/playback
│   ├── discovery/          # ACDS implementation
│   ├── log/                # Logging system
│   ├── options/            # CLI parsing
│   └── debug/              # Memory tracking
├── tests/                  # Criterion test suite
├── docs/                   # Documentation
└── build/                  # CMake build directory
```

## Testing

```bash
# Run all tests
cmake --build build --target tests
ctest --test-dir build --output-on-failure --parallel 0

# Run specific test category
ctest --test-dir build --label-regex "^unit$" --output-on-failure

# Run single test
ctest --test-dir build -R "crypto_handshake" --output-on-failure
```

**Note**: On macOS, use Docker for Criterion tests:
```bash
docker-compose -f ./tests/docker-compose.yml run --rm ascii-chat-tests bash -c 'build_docker/bin/test_unit_crypto_handshake'
```

## Development Best Practices

### Memory Management Rules

1. Always use `SAFE_MALLOC/CALLOC/FREE` macros
2. Set pointers to NULL after freeing
3. Framebuffers own their data - don't double-free
4. Buffer pool allocations are automatically tracked

### Logging Best Practices

**Important: Never use fprintf() or printf() for debug output**

Always use the logging system (`log_debug()`, `log_info()`, etc.) instead of raw `fprintf()` or `printf()` calls:

```c
// ❌ WRONG - bypasses logging system, causes UI artifacts
fprintf(stderr, "[DEBUG] Processing packet\n");

// ✅ CORRECT - goes through logging system
log_debug("Processing packet");
```

**Why:** Raw fprintf/printf calls:
- Bypass `--grep` filtering (can't isolate specific logs)
- Don't write to log file (can't debug post-mortem)
- Interrupt UI rendering (splash screen flashing, scrolling artifacts)
- Ignore `--quiet` and log level settings

**Additional best practices:**

1. Use `log_*_every()` in high-frequency code (video/audio loops)
2. Use `--grep` to filter noise during debugging
3. Set appropriate log levels: dev < debug < info < warn < error < fatal
4. Log to file with `--log-file` when output would interfere with UI

### Comment Style

**Never use emphatic comment prefixes like "CRITICAL:", "WARNING:", "IMPORTANT:", etc.**

All comments in code are important by nature. Write them in a professional, matter-of-fact tone.

**Acceptable prefixes:**
- ✅ `TODO:` - for planned future work
- ✅ `FIXME:` - for known issues that need fixing
- ❌ `CRITICAL:` - everything in production code is critical
- ❌ `WARNING:` - if it's dangerous, explain why, don't just warn
- ❌ `IMPORTANT:` - all comments should be important
- ❌ `HACK:` - if it's a hack, refactor it or explain the constraints

```c
// ❌ WRONG - emphatic style that doesn't add value
// CRITICAL: This must be called before the mutex is destroyed!
// WARNING: Do not modify this without updating the corresponding code!
// IMPORTANT: Buffer must be freed after use!

// ✅ CORRECT - professional, clear, direct
// This must be called before the mutex is destroyed.
// Do not modify this without updating the corresponding code.
// Buffer must be freed after use.

// ✅ CORRECT - actionable markers for future work
// TODO: Add support for IPv6 addresses
// FIXME: Race condition when multiple threads access this simultaneously
```

**Why:**
- Emphatic prefixes like CRITICAL/WARNING/IMPORTANT make code look unprofessional
- All comments in production code should be equally important and carefully considered
- The comment content itself should convey importance through clear explanation
- If something is truly critical, explain why and what happens if violated

```c
// ❌ WRONG
// CRITICAL: Writer must NOT modify read_index!

// ✅ CORRECT
// Writer must not modify read_index (race condition with reader).
```

### Error Handling Best Practices

1. Always use `asciichat_error_t` return types
2. Use `SET_ERRNO()` for errors with context
3. Use `SET_ERRNO_SYS()` for system call errors
4. Check return values and propagate errors up

### Git Workflow

```bash
# Never use git add . or git add -A
git add file1.c file2.c file3.h

# Commit with Co-Authored-By for Claude changes
git commit -m "fix: description
# Don't leave an "claude by anthropic" reference in the commit message
```

## Common Issues

### Build Issues

**"Unknown mode or invalid argument"**: Check command line - mode must come after binary-level options

```bash
# ✅ CORRECT
./build/bin/ascii-chat --log-level info server --port 8080

# ❌ WRONG
./build/bin/ascii-chat server --log-level info --port 8080
```

**SIGTTOU when piping**: Fixed - program now checks terminal control before modifying terminal

**Compilation database for defer tool**: Build system automatically handles this

### Network Issues

**Connection refused**: Check server is running and firewall allows port 27224

```bash
# Test connectivity
nc -zv localhost 27224
```

**Handshake timeout**: Check crypto keys match

```bash
./build/bin/ascii-chat --log-level debug client --grep "handshake"
```

### Memory Issues

**Leaks at exit**: Check memory report. Red numbers indicate leaks. Common causes:
- Missing `SAFE_FREE()` calls
- Circular references
- Thread cleanup issues

Use AddressSanitizer in debug builds to find issues:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/bin/ascii-chat mirror --snapshot --snapshot-delay 0
```

## Environment Variables

- `ASCII_CHAT_QUESTION_PROMPT_RESPONSE` - **Stack-based auto-answer for prompts** (for automation/testing)
  - Single response: `ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y'`
  - Multiple responses: `ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y;n;password123'`
  - Optional trailing semicolon: `ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y;n;'`
  - Responses are popped from the stack as prompts are encountered
  - After stack is exhausted, program waits for manual input
  - Invalid formats (`;`, `;;`, `;y`) are rejected
- `CLAUDECODE=1` - Optimize output for LLM (set automatically by Claude Code)
- `SSH_AUTH_SOCK` - SSH agent socket for key authentication
- `LOG_LEVEL` - Override log level (DEBUG/INFO/WARN/ERROR/FATAL)
- `ASCII_CHAT_MEMORY_REPORT_BACKTRACE=1` - Show backtraces in memory report

## Resources

- **Crypto Protocol**: `docs/crypto.md`
- **Platform Abstraction**: `lib/platform/README.md`
- **Build System**: `cmake/` directory
