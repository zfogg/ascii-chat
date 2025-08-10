# ASCII-Chat Development Guide for Claude

## Essential First Steps
- **ALWAYS** read and understand the `README.md` and `Makefile` files first
- Format code with `make format` after you edit it
- Use `SAFE_MALLOC()` macro from common.h rather than regular `malloc()`
- On macOS: use `lldb` for debugging (gdb doesn't work with this project)

## Project Overview
ASCII-Chat is a terminal-based video chat application that converts webcam video to ASCII art in real-time. It supports multiple clients connecting to a single server, with video mixing and audio streaming capabilities.

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
├── bin/                    # Compiled binaries (server, client)
├── build/                  # Object files (.o)
├── notes/                  # Text about the project, things I want to learn or do or remember.
├── todo/                   # Example code that I want to include eventually, usually from you and ChatGPT.
├── server.c                # Server main - handles multiple clients
├── client.c                # Client main - captures/displays video
├── network.c/h             # Network protocol and packet handling
├── packet_queue.c/h        # Per-client packet queue system
├── compression.c/h         # Frame compression with zlib
├── mixer.c/h               # Audio mixing for multiple clients
├── ascii.c/h               # ASCII art conversion and grid layout
├── webcam.c/h              # Webcam capture abstraction
├── webcam_avfoundation.m   # macOS webcam implementation (Objective-C file)
├── webcam_v4l2.c           # Linux webcam implementation (v4l2 API)
├── audio.c/h               # Audio capture/playback (PortAudio)
├── framebuffer.c/h         # Multi-producer frame buffering
├── ringbuffer.c/h          # Lock-free ring buffer
└── Makefile                # Build configuration
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

## Common Code Patterns

### Safe Memory Allocation
```c
// Always use SAFE_MALLOC macro
char *buffer;
SAFE_MALLOC(buffer, size, char *);
// Automatically logs errors and returns on failure
```

### Packet Sending Pattern
```c
// Build packet with network byte order
header.field = htonl(value);  // 32-bit version

// Server: use packet queue
packet_queue_enqueue(client->video_queue, type, data, len, client_id, true);

// Client: use send_packet directly
send_packet(sockfd, PACKET_TYPE_IMAGE_FRAME, data, len);
```

### Frame Processing Pattern
```c
// Read from framebuffer
multi_frame_t frame;
if (framebuffer_read_multi_frame(buffer, &frame)) {
    // Process frame.data
    // DON'T: free(frame.data)
    // DON'T: framebuffer_write_multi_frame(buffer, frame.data, ...)
}
```

## Performance Considerations

- Compression threshold: 0.8 (only compress if <80% of original && > 0)
- Typical frame rate: 30-120 FPS (the developer wants to test this some day)
- Audio packet size: 256 samples (#define AUDIO_SAMPLES_PER_PACKET)
- Max packet size: 5MB (#define MAX_PACKET_SIZE)
- Socket timeouts: 10 seconds (#define CONNECT_TIMEOUT, #define SEND_TIMEOUT, 
   #define RECV_TIMEOUT) from network.h
- Terminal size: The developer has a terminal of size 203x64. Claude's terminal 
   is smaller but he can set it by setting COLUMNS=50 LINES=25 before running 
   a command.


## Troubleshooting Quick Reference

 | Symptom                          | Likely Cause            | Solution                                               |
 |----------------------------------|-------------------------|--------------------------------------------------------|
 | "Unknown packet type: 1"         | Missing validation case | Add PACKET_TYPE_ASCII_FRAME to receive_packet() switch |
 | "DEADBEEF" in audio data         | TCP stream corruption   | Implement per-client packet queues                     |
 | Video not displaying             | Wrong packet type       | Verify server sends ASCII_FRAME (type 1)               |
 | Crash after "Image size exceeds" | Use-after-free          | Don't write frames back to buffer                      |
 | High CPU usage                   | Busy loops              | Add usleep(10000) in tight loops                       |
 | Clients randomly disconnect      | Timeout or queue full   | Increase timeouts or queue sizes                       |
 | CRC checksum mismatch            | Concurrent writes       | Use packet queues, not direct sends                    |
 | Memory leak                      | Missing free            | Check all SAFE_MALLOC has corresponding free           |

## Protocol Migration Notes

### Old Two-Packet System (REMOVED)
- Used PACKET_TYPE_VIDEO_HEADER + PACKET_TYPE_VIDEO
- Caused race conditions between header and data
- Led to "DEADBEEF" corruption issues

### New Unified System (CURRENT)
- Single PACKET_TYPE_ASCII_FRAME contains header + data
- Single PACKET_TYPE_IMAGE_FRAME for camera data
- Atomic send operation prevents corruption
- 50% reduction in network overhead

## Notes for Future Development

- The packet queue system is CRITICAL for preventing race conditions
- Always validate packet sizes before processing
- Network byte order conversion required for ALL multi-byte fields
- Framebuffer uses reference counting - let it manage memory
- Test with multiple clients early and often
- On macOS: use lldb and AddressSanitizer, NOT gdb
- When adding new packet types, MUST add to receive_packet() validation
- Check existing branch naming patterns before creating new branches
- Run `make format` before EVERY commit
