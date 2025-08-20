# ASCII-Chat Development Guide for Claude

## Essential First Steps
- **ALWAYS** read and understand the `README.md` and `Makefile` files first
- Format code with `make format` after you edit it.
- Use `SAFE_MALLOC()` macro from common.h rather than regular `malloc()`
- On macOS: use `lldb` for debugging (gdb doesn't work with this project)
- Use `clang` instead of `gcc`.
- Don't use `git add .`, add all files individually.
- Use AdrressSanitizer (ASan) and memory reports from common.c for memory
debugging. `make clean && make sanitize`.
- Use log_*() from logging.c and common.h for logging instead of printf().
- When debugging and testing, make a test_whatever.sh and use that so you don't
bother the developer by requesting to run commands over and over.

## Project Overview
ASCII-Chat is a terminal-based video chat application that converts webcam
video to ASCII art in real-time. It supports multiple clients connecting to a
single server, with video mixing and audio streaming capabilities.

**Key Features:**
- Real-time webcam to ASCII conversion
- Multi-client video grid layout (2x2, 3x3, etc)
- Audio streaming with mixing
- Compression support for frames
- Cross-platform (macOS/Linux)
- Command line program for your terminal
- Audio support
- Supports multiple clients like Google Hangouts or Zoom

## Project Structure

```
ascii-chat/
├── bin/                        # Compiled binaries (server, client)
├── build/                      # Object files (.o)
├── notes/                      # Text about the project, things I want to learn or do or remember.
├── todo/                       # Example code that I want to include eventually, usually from you and ChatGPT.
├── common.c/h                  # Code that all files tend to use. Macros, logging, memory debugging, protocol definitions, errors, constants.
├── src/server.c                # Server main - handles multiple clients
├── src/client.c                # Client main - captures/displays video
├── lib/network.c/h             # Network protocol and packet handling
├── lib/packet_queue.c/h        # Per-client packet queue system
├── lib/compression.c/h         # Frame compression with zlib
├── lib/mixer.c/h               # Audio mixing for multiple clients
├── lib/ascii.c/h               # ASCII art conversion and grid layout
├── lib/webcam.c/h              # Webcam capture abstraction
├── lib/webcam_avfoundation.m   # macOS webcam implementation (Objective-C file)
├── lib/webcam_v4l2.c           # Linux webcam implementation (v4l2 API)
├── lib/audio.c/h               # Audio capture/playback (PortAudio)
├── lib/framebuffer.c/h         # Multi-producer frame buffering
├── lib/ringbuffer.c/h          # Lock-free ring buffer
├── lib/ascii_simd*.c/h         # SIMD implementions of IMAGE to ASCII conversions. Black-and-whiteh and color versions.
├── lib/ansifast.c/h            # Build optimized ANSI strings fast for SIMD image to ascii conversion.
└── Makefile                    # Build configuration
```

## Building and Running

### Essential Commands
```bash
# Clean build (always do this when debugging issues)
make clean && make debug

# Format code after changes
make format

# Debug build with symbols
make clean && make debug

# Build with AddressSanitizer (ASan reports, memory debugging)
make clean && make sanitize
# Compiles with -fsanitize=address and -g -O0 flags.

# Start server (listens on port 8080)
./bin/server

# Start client (connects to localhost by default)
./bin/client

# Connect to a server
./bin/client --address 127.0.0.1 --port 80008

# Run with custom dimensions (by default it uses terminal size or $COLUMNS x $LINES)
./bin/client --width 80 --height 24

# Color and audio support
./bin/server --color --audio
./bin/client --color --audio

# If you need to know how to use the binaries
./bin/server --help
# or check options.c/h
```

```sh
# --log-file will help you debug! (you often pipe like >/tmp/file.txt 2>&1 and it doesn't always work, so you get frustrated. so i made you this command line argument flag). also we have --snapshot + --snapshot-delay to use now... use them to debug the server and client. it will just print out ascii until snapshot delay and then it ends leaving you with a snapshot of the ascii and program terminated.
./bin/server --log-file $TMPDIR/server-claude-test12.log
./bin/client --log-file $TMPDIR/client-claude-test42.log

```

```sh
# --snapshot will also really help you debug!
./bin/client --snapshot
# view the printed ascii frame as long as you like
./bin/client --snapshot --snapshot-delay 10
# it will just print out ascii until --snapshot-delay which ends leaving you
# with a snapshot of the ascii and program terminated.
```

### Testing Video Display
```bash
# Test if server is outputting ASCII frames
timeout 5 ./bin/client > /tmp/server_output.txt 2>&1 &
# Then check if /tmp/server_output.txt contains ASCII art

# Test client video capture
./bin/client 2>&1 | grep -E "Sending frame|Received.*frame"
# Sometimes the developer commentds out logs. You can uncomment them or make
# new logs. Delete them when you're done.

# Test with multiple clients in background
./bin/server &
SERVER_PID=$!
sleep 1
./bin/client > /tmp/client1.txt 2>&1 &
./bin/client > /tmp/client2.txt 2>&1 &
sleep 10
kill $SERVER_PID
```

## Debugging Techniques

### 1. Enable Debug Logging
Add these defines to see detailed logs:
```c
#define NETWORK_DEBUG      // Network packet details
#define COMPRESSION_DEBUG  // Compression statistics
#define AUDIO_DEBUG       // Audio packet info
#define DEBUG_THREADS     // Thread lifecycle
#define MIXER_DEBUG       // Audio mixing details
// When you make new logs, try to put them under those categories. Make a new
// category if you need to and it will help the developer and you work and debug.
```

### 2. Common Issues and Solutions

**"Unknown packet type" errors:**
- Check packet type enum values in network.h
- Verify receive_packet() in network.c has cases for ALL packet types in the validation switch
- Common issue: Missing validation cases for PACKET_TYPE_ASCII_FRAME and PACKET_TYPE_IMAGE_FRAME

**"DEADBEEF" pattern in packet data:**
- Indicates TCP stream corruption/desynchronization
- Usually caused by concurrent socket writes without synchronization
- Solution: Use per-client packet queues with dedicated send threads

**Video not displaying:**
- Check if server is sending PACKET_TYPE_ASCII_FRAME (type 1)
- Verify client handle_ascii_frame_packet() is being called
- Check for CRC checksum mismatches in logs
- Use `log_debug()` to trace packet flow

**Memory crashes:**
- Always use SAFE_MALLOC() macro instead of malloc()
- Check framebuffer operations - common source of use-after-free
- Don't write frames back to buffer after reading them
- Build with AddressSanitizer to catch issues

**"Image size exceeds maximum" with huge dimensions:**
- Usually indicates memory corruption or use-after-free
- Check if you're freeing data that framebuffer still references
- Verify you're not modifying frame data after passing to framebuffer

**Connection drops:**
- Check socket timeouts (SEND_TIMEOUT, RECV_TIMEOUT in network.h)
- Monitor packet queue overflow (drops oldest packets)
- Verify keepalive is working (KEEPALIVE_IDLE settings)

**Socket bind errors ("Operation not supported on socket"):**
- Try using a different port: `./bin/server --port 9000`
- Port 8080 may be blocked or in use by another service
- Common alternative ports: 9000, 8081, 3000

### 3. Debugging Tools

Use the following tools frequently to debug and work and help the developer.

```bash
# Monitor network traffic
sudo tcpdump -i lo0 port 8080 -X  # macOS
sudo tcpdump -i lo port 8080 -X    # Linux

# Check for memory leaks (macOS)
leaks --atExit -- timeout 5 ./bin/server
leaks --atExit -- timeout 3 ./bin/client

# Use lldb for debugging (NOT gdb on macOS)
lldb ./bin/server
(lldb) run
(lldb) bt  # backtrace when it crashes
(lldb) frame select 0  # examine specific frame
(lldb) p variable_name  # print variable

# Monitor file descriptors
lsof -p $(pgrep server)

# Check thread activity
ps -M $(pgrep server)  # macOS
ps -eLf | grep server  # Linux

# Watch server output in real-time
./bin/server 2>&1 | tee server.log | grep -E "ERROR|WARN|Client"
```

## Network Protocol

### Packet Structure
```c
packet_header_t {
  uint32_t magic;     // 0xDEADBEEF for validation
  uint16_t type;      // packet_type_t enum value
  uint32_t length;    // payload length
  uint32_t sequence;  // for ordering/duplicate detection
  uint32_t crc32;     // payload checksum
  uint32_t client_id; // which client sent this
}
```

### Packet Types (CRITICAL - these must match in receive_packet validation)
```c
typedef enum {
  // Unified frame packets (header + data in single packet)
  PACKET_TYPE_ASCII_FRAME = 1, // Server->Client - Complete ASCII frame with all metadata
  PACKET_TYPE_IMAGE_FRAME = 2, // Client->Server - Complete RGB image with dimensions

  // Audio and control
  PACKET_TYPE_AUDIO = 3, // Audio data
  PACKET_TYPE_SIZE = 4, // Terminal dimensions
  PACKET_TYPE_PING = 5, // Keepalive
  PACKET_TYPE_PONG = 6, // Keepalive response

  // Multi-user protocol extensions
  PACKET_TYPE_CLIENT_JOIN = 7,    // Client announces capability to send media
  PACKET_TYPE_CLIENT_LEAVE = 8,   // Clean disconnect notification
  PACKET_TYPE_STREAM_START = 9,   // Client requests to start sending video/audio
  PACKET_TYPE_STREAM_STOP = 10,   // Client stops sending media
  PACKET_TYPE_CLEAR_CONSOLE = 11, // Server tells client to clear console
  PACKET_TYPE_SERVER_STATE = 12,  // Server sends current state to clients
} packet_type_t;
// ... and more types to be added as the protocol evolves
```

### Primary Functionality (ASCII "video") Packet Flow
1. Client captures webcam → sends IMAGE_FRAME to server
2. Server converts to ASCII → stores in framebuffer
3. Mixer thread creates grid layout → broadcasts ASCII_FRAME to all clients
4. Clients receive and display ASCII frames

### Unified Packet System
- Frames are sent as single packets (header + data together)
- No more separate VIDEO_HEADER and VIDEO packets (deleted)
- Reduces race conditions and simplifies protocol compared to old two-packet system

## Git Workflow

### Working with Issues
```bash
# Create branch for issue (follow existing naming pattern)
git checkout master
git pull origin master
git checkout -b claude/issue-XX-$(date +%Y%m%d-%H%M)

# After changes
git add -A
git commit -m "fix: description of fix

Fixes #XX"

# Push to origin
git push
```

### Creating Pull Requests
```bash
# Always check these before creating PR
git status  # Should be clean
git diff master...HEAD  # Review all changes
git log master..HEAD --oneline  # Check commit messages

# Use gh CLI for PR
gh pr create --title "Fix: Issue description" --body "## Summary
- Fixed X by doing Y
- Updated Z to prevent future issues

## Test Plan
- [ ] Server starts without errors
- [ ] Multiple clients can connect
- [ ] Video displays correctly
- [ ] No memory leaks
- [ ] No checksum errors

Fixes #issue-number

🤖 Generated with [Claude Code](https://claude.ai/code)
```

## Architecture Notes

### Per-Client Threading Architecture (January 2025)
ASCII-Chat uses a high-performance per-client threading model:
- **Each client gets 2 dedicated threads**: 1 video render (60 FPS) + 1 audio render (172 FPS)
- **Linear performance scaling**: No shared bottlenecks, scales to 9+ clients
- **Thread-safe design**: Proper synchronization eliminates race conditions
- **Real-time performance**: 60 FPS video + 172 FPS audio maintained per client

**See `notes/per-client-threading-architecture.md` for complete technical details.**

### Critical Synchronization Rules

#### ALWAYS Use Per-Client Mutex Protection
```c
// ✅ CORRECT: Mutex-protected client state access
pthread_mutex_lock(&client->client_state_mutex);
uint32_t client_id_snapshot = client->client_id;
bool active_snapshot = client->active;
bool has_video_snapshot = client->has_video;
pthread_mutex_unlock(&client->client_state_mutex);

// Use snapshot data for processing (never direct access)
if (active_snapshot && has_video_snapshot) {
  process_client(client_id_snapshot);
}
```

```c
// ❌ RACE CONDITION: Direct access to client fields
if (client->active && client->has_video) {  // DANGEROUS!
  process_client(client->client_id);        // DANGEROUS!
}
```

#### Lock Ordering Protocol (CRITICAL - Prevents Deadlocks)
**Always acquire locks in this exact order:**
1. **Global RWLock** (`g_client_manager_rwlock`)
2. **Per-Client Mutex** (`client_state_mutex`)
3. **Specialized Mutexes** (`g_stats_mutex`, `g_frame_cache_mutex`, etc.)

```c
// ✅ CORRECT: Global → Per-Client
pthread_rwlock_rdlock(&g_client_manager_rwlock);
pthread_mutex_lock(&client->client_state_mutex);
// ... process
pthread_mutex_unlock(&client->client_state_mutex);
pthread_rwlock_unlock(&g_client_manager_rwlock);

// ❌ DEADLOCK RISK: Per-Client → Global
pthread_mutex_lock(&client->client_state_mutex);
pthread_rwlock_wrlock(&g_client_manager_rwlock); // DEADLOCK POTENTIAL!
```

### Per-Client Packet Queues
- Each client has dedicated audio and video queues
- Audio queue: 100 packets max (prioritizes low latency)
- Video queue: MAX_FPS (120) frames max (can drop old frames)
- Single send thread per client prevents race conditions and makes the server efficient.
- Queue overflow drops oldest packets first

### Frame Buffer Management
- Multi-producer single-consumer design
- Uses reference counting internally
- CRITICAL: Never free data returned from framebuffer
- CRITICAL: Don't write frames back after reading
- Common crash cause: use-after-free from improper handling

### ASCII Grid Layout (ascii.c)
- Dynamic layout depending on number of clients: 2 side-by-side, 2x2, 3x2, and 3x3
- Supporting up to 9 clients
- Each section shows one client's webcam video as ASCII art
- Empty slots remain blank
- Grid coordinates: (0,0) top-left, (1,1) bottom-right
- When connected client count changes, the way this renders will change with it
- Padding (whitespace) is applied to make things look nice and centered when
   necessary, depending on terminal size and aspect ratio.

### Memory Management Rules
1. Always use SAFE_MALLOC() and SAFE_REALLOC() and SAFE_CALLOC instead of
   malloc() and realloc() and calloc() (they're in common.h)
2. Framebuffer owns its data - don't free it
3. Packet queues copy data if owns_data=true
4. Check for NULL before freeing when necessary but the developer doesn't like
   unnecessary null checks
5. Set pointers to NULL after freeing (this helps catch bugs)

## Testing Checklist

Before committing any changes:
1. [ ] `make clean && make debug` - check that it builds without warnings or errors
2. [ ] `make format` - code is properly formatted
3. [ ] Start server, connect 2+ clients
4. [ ] Video displays in correct grid layout
5. [ ] No "DEADBEEF" or "Unknown packet type" errors
6. [ ] No CRC checksum mismatches
7. [ ] Audio works (if testing audio changes)
8. [ ] Clients can disconnect/reconnect cleanly
9. [ ] No crashes over 1+ minute runtime
10. [ ] Check with AddressSanitizer if changed memory handling (`make clean && make sanitize`)

## Development Principles

### No Emergency Cleanup Code
**NEVER** write "emergency cleanup" or "force cleanup" code. This is a code smell indicating improper resource management design.

**Problems with Emergency Cleanup:**
- Masks underlying synchronization bugs
- Creates complex error-prone code paths
- Indicates lack of understanding of timing issues
- Makes debugging harder by hiding root causes
- Often leads to double-free or use-after-free bugs

**Instead, Code Properly From Start:**
- Assume threads and networks have **irregular, unpredictable timing**
- Design resource ownership to be **unambiguous** (single owner per resource)
- Use **deterministic cleanup sequences** that work regardless of timing
- **Wait for threads properly** before cleaning up their resources
- Use **reference counting** or **RAII patterns** for complex resource lifetimes
- **Test timing edge cases** explicitly (add delays, stress test)

**Example - Wrong Approach:**
```c
// BAD: Emergency cleanup for "missed" resources
if (remaining_clients > 0) {
    log_error("EMERGENCY: Force cleaning %d clients", remaining_clients);
    force_cleanup_clients(); // ❌ This shouldn't be needed!
}
```

**Example - Right Approach:**
```c
// GOOD: Proper synchronization ensures deterministic cleanup
wait_for_all_client_threads_to_finish();  // Block until threads done
cleanup_all_clients();                    // Now safe to cleanup
assert(remaining_clients == 0);           // Should always be true
```

**Key Insight**: If you need emergency cleanup, the **design is wrong**. Fix the design, don't code around it.

## Important Files to Understand

1. **network.h**: Defines ALL packet types and protocol structures
2. **network.c**: Contains receive_packet() validation switch (MUST have all types)
3. **packet_queue.c**: Thread-safe per-client queue implementation
4. **server.c**:
   - `video_broadcast_thread`: Mixes and sends video
   - `audio_mixer_thread`: Mixes and sends audio
   - `client_thread_func`: Handles individual client
5. **client.c**:
   - `handle_ascii_frame_packet`: Processes received video
   - `video_capture_thread_func`: Captures and sends webcam
6. **ascii.c**: Functions for working with ascii from images (ascii_*). Grid
   layout functions (ascii_grid_*)
7. **mixer.c**: Multi-client audio sample mixing logic. Ducking and compression
8. **ringbuffer.c**: Framebuffer code lives here (data structure for
   multi-frame storage with metadata)
9. **packet_queue.c**: Thread-safe per-client queue implementation for the
   network protcol and its packets. It has node pools and uses buffer_pool.c/h

## SIMD Optimization: The Complete Journey (January 2025)

### Background
We implemented comprehensive SIMD optimizations for ASCII art generation and discovered critical insights about performance optimization, algorithmic complexity, and the two-phase approach required for success.

### The Breakthrough: Two-Phase Optimization Strategy

**Key Discovery**: SIMD performance issues weren't due to vectorization failure, but to algorithmic complexity and string generation bottlenecks that masked SIMD benefits.

#### Phase 1: Fix Algorithmic Complexity (O(n²) → O(n))
**Problem**: NEON color implementation had nested loops creating O(n²) complexity due to run-length encoding lookahead logic.
**Solution**: Simplified to match scalar's O(n) approach - process pixels sequentially without complex lookahead.

#### Phase 2: Optimize String Generation Bottleneck
**Problem**: `snprintf()` dominated execution time (90%+ vs 5% pixel processing).
**Solution**: Precomputed decimal lookup tables eliminated divisions and format parsing.

### Final Performance Results ✅

#### After Both Optimizations (SUCCESS!)
| Test Case | Old snprintf | New SIMD+LUT | Speedup | Status |
|-----------|-------------|--------------|---------|---------|
| Terminal 203x64 (FG) | 1.102ms | 0.131ms | **8.4x faster** | ✅ SIMD Winning |
| Terminal 203x64 (BG) | Multiple ms | 0.189ms | **>10x faster** | ✅ SIMD Winning |
| String Generation | 1.102ms | 0.105ms | **10.5x faster** | ✅ Mission Complete |

### Critical SIMD Implementation Insights

#### What Actually Worked
1. **16-pixel NEON processing** with `vld3q_u8()` interleaved loads
2. **16-bit precision math** (`vmlaq_n_u16`) - sufficient for luminance, 2x wider than 32-bit
3. **Eliminated O(n²) algorithmic complexity** from run-length encoding
4. **Precomputed decimal lookup tables** for RGB→string conversion
5. **Combined FG+BG ANSI sequences** to reduce escape sequence overhead

#### Why Previous SIMD Failed
1. **Wrong bottleneck targeted**: Optimized 5% (pixels) while ignoring 95% (strings)
2. **O(n²) complexity**: Complex lookahead logic made SIMD slower than necessary
3. **Compiler auto-vectorization competition**: "Scalar" code was already vectorized by Clang
4. **Insufficient processing width**: 4 pixels vs optimal 16 pixels per iteration

### Architecture Lessons Confirmed

#### ✅ Bottleneck Migration is Real
- **Before**: String generation dominated (90%+ of time), masking SIMD potential
- **After**: Balanced workload where SIMD pixel processing shows true performance

#### ✅ Algorithmic Optimization First
- **Wrong approach**: Add SIMD to existing O(n²) algorithm
- **Right approach**: Fix algorithm complexity THEN apply SIMD to clean O(n) code

#### ✅ Profile the Full Pipeline
- Don't optimize 5% of execution time while ignoring the 95%
- Both pixel processing AND string generation needed optimization
- End-to-end measurement reveals true bottlenecks

### Implementation Strategy That Worked

#### String Optimization: The Critical Fix
```c
// Before: snprintf() dominating performance
snprintf(buffer, remaining, "\033[38;2;%d;%d;%dm", r, g, b);
// Cost: ~200 cycles per pixel (format parsing + division)

// After: Precomputed lookup tables
memcpy(p, "\033[38;2;", 7); p += 7;
p += write_rgb_triplet(r, p); *p++ = ';';    // dec3[r] lookup
p += write_rgb_triplet(g, p); *p++ = ';';    // dec3[g] lookup
p += write_rgb_triplet(b, p); *p++ = 'm';    // dec3[b] lookup
// Cost: ~50 cycles per pixel (no divisions, no format parsing)
```

#### SIMD Optimization: The Right Approach
```c
// 16-pixel NEON processing with optimal data types
uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + i));  // Load 16 pixels
uint16x8_t luma_lo = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_lo, 77), g_lo, 150), b_lo, 29);
uint16x8_t luma_hi = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_hi, 77), g_hi, 150), b_hi, 29);
// Process 16 pixels per iteration with 16-bit precision
```

### Key Architectural Insights

#### When SIMD Actually Works
- **After algorithmic fixes**: Clean O(n) code without nested loops
- **Appropriate data width**: 16 pixels/iteration vs 4 pixels
- **Right precision**: 16-bit math (sufficient, 2x wider than 32-bit)
- **When string bottleneck is eliminated**: Pixel processing becomes worthwhile to optimize

#### The Auto-Vectorization Lesson
**Critical discovery**: "Scalar" code was already vectorized by modern Clang!
- **Compiler advantages**: Automatic optimization with 8-16 pixel processing
- **Manual SIMD value**: Cross-platform consistency and specialized algorithms
- **When to use manual SIMD**: Complex algorithms compilers can't auto-vectorize

### Current Architecture Status ✅

#### Production Performance (January 2025)
- **Terminal 203×64**: 0.131ms/frame (FG), 0.189ms/frame (BG)
- **SIMD actively outperforming scalar** after both phases optimized
- **10.5x string generation speedup** eliminates the major bottleneck
- **Security hardened** with exact buffer overflow protection
- **Memory-safe** with SAFE_MALLOC() patterns throughout

**Important Note**: Scalar still outperforms NEON for large images in foreground color mode:
- **320×240**: Scalar 0.371ms vs NEON 0.391ms (0.9x speedup)
- **640×480**: Scalar 1.495ms vs NEON 1.584ms (0.9x speedup)
- **1280×720**: Scalar 4.531ms vs NEON 4.812ms (0.9x speedup)

This suggests debug build overhead or remaining static variable dependencies that prevent full SIMD optimization in larger workloads. Background mode shows NEON winning for large images, indicating the issue is specific to foreground processing patterns.

### Essential SIMD Lessons Learned

1. **Profile end-to-end pipeline** - Don't optimize 5% while ignoring 95%
2. **Fix algorithmic complexity first** (O(n²) → O(n)) before adding SIMD
3. **Compiler auto-vectorization** often beats manual SIMD for simple loops
4. **Two-phase optimization required**: Both pixel processing AND string generation
5. **Use appropriate SIMD width**: 16 pixels/iteration vs 4 pixels
6. **Choose right precision**: 16-bit math (sufficient, 2x wider than 32-bit)

#### When Manual SIMD Actually Works
- **After algorithmic fixes**: Clean O(n) code without nested loops
- **Complex algorithms**: Compilers can't auto-vectorize
- **Cross-platform consistency**: Explicit intrinsics vs compiler variance
- **When string bottlenecks eliminated**: Pixel processing becomes optimization target

#### The Two-Phase Success Formula ✅
1. **Phase 1**: Fix algorithmic complexity (O(n²) → O(n))
2. **Phase 2**: Optimize string generation (snprintf → lookup tables)
3. **Result**: SIMD pixel processing finally shows true performance potential

**Final validation**: Both optimizations delivered exactly as predicted - SIMD is now actively outperforming scalar in production! The two-phase strategy was essential for success.

### Advanced Optimization Techniques (Future Work)

**Additional Performance Opportunities:**
1. **Run-length encoding**: Skip SGR when colors don't change (2-50x fewer sequences)
2. **Combined FG+BG ANSI**: Single `\033[38;2;R;G;B;48;2;r;g;bm` sequence
3. **Two pixels per cell**: Use ▀ character (halves terminal cells and sequences)
4. **256-color mode**: Pre-computed strings eliminate runtime integer conversion
5. **Batched terminal writing**: Single `write()` call eliminates syscall overhead

**Performance Measurement Strategy:**
```c
// Separate timing into phases to identify true bottlenecks
timer_start(&pixel_timer);
convert_to_ascii(pixels, ascii_chars, count);        // Phase 1: Pixel processing
pixel_time = timer_stop(&pixel_timer);

timer_start(&string_timer);
generate_ansi_output(ascii_chars, colors, buffer);   // Phase 2: String generation
string_time = timer_stop(&string_timer);

timer_start(&output_timer);
write(STDOUT_FILENO, buffer, buffer_length);        // Phase 3: Terminal output
output_time = timer_stop(&output_timer);
```

## Critical C Programming Pattern: Integer Overflow Prevention

### The Problem: Multiplication Overflow Before Type Conversion
**This has come up multiple times and must be remembered:**

```c
// WRONG - can overflow 'int' before conversion to size_t
const size_t buffer_size = output_height * w * max_per_char + extra;

// RIGHT - cast to larger type BEFORE multiplication
const size_t buffer_size = (size_t)output_height * (size_t)w * max_per_char + extra;
```

### Why This Matters
1. **CodeQL Analysis**: Will fail builds if multiplication can overflow `int`
2. **Security**: Integer overflow can lead to buffer underallocation
3. **Large Images**: Modern webcam resolutions (1920×1080+) easily overflow 32-bit integers
4. **Subtle Bugs**: May work fine in testing, fail catastrophically in production

### When to Apply This Pattern
- **Any buffer size calculation** involving width × height
- **Memory allocation sizes** based on image dimensions
- **Loop bounds** that multiply image dimensions
- **Whenever CodeQL warns** about "Multiplication result converted to larger type"

### Fixed Examples in This Codebase
```c
// Fixed in ascii_simd_color.c:859
const size_t buffer_size = (size_t)output_height * (size_t)w * max_per_char + (size_t)output_height * reset_len + total_newlines + 1;

// Pattern for other buffer calculations
const size_t total_pixels = (size_t)width * (size_t)height;
const size_t buffer_len = total_pixels * (size_t)bytes_per_pixel;
```

### Remember: Cast Early, Cast Often
**Always cast to the target type BEFORE performing arithmetic that might overflow.**

