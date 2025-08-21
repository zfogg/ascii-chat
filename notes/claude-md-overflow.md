# CLAUDE.md overflow
The stuff that can't fit in CLAUDE.md.

## CRITICAL UNDERSTANDING: Proper Ringbuffer Usage in Video Streaming

### The Breakthrough (2025-08-10)
After struggling with flickering video and lag issues, we finally understood how ringbuffers SHOULD be used in networked video applications:

#### What We Were Doing Wrong:
- Trying to always get the "latest" frame by consuming/discarding all old frames
- This defeated the entire purpose of having a buffer
- Caused flickering when broadcast rate > client send rate (empty buffers)
- Wasted all the frames we carefully saved

#### The Correct Architecture:
```
Client sends frames → [Ringbuffer (FIFO)] → Server reads OLDEST frame → Mix → Send
                           ↑
                    Network lag? Frames queue up here
                    Good network? Buffer drains naturally
```

#### Key Insights:
1. **FIFO Processing is Essential**: Always process frames in the order they arrived (oldest first)
2. **Buffers Handle Network Jitter**: That's their whole purpose - smooth out irregular packet arrival
3. **Never Discard Frames Unnecessarily**: We saved them for a reason - to handle lag!
4. **Cache Last Valid Frame**: Each client needs a cached frame for when buffer is empty

#### The Solution That Fixed Everything:
```c
// Each client now has:
multi_source_frame_t last_valid_frame;  // Cache of most recent valid frame
bool has_cached_frame;                  // Whether we have a valid cached frame

// When mixing frames:
1. Try to read ONE frame from buffer (oldest first - proper FIFO)
2. If successful, update cached frame
3. If buffer empty, use cached frame (no flicker!)
4. Never have missing clients in grid
```

This is how **real video streaming applications** work - they display the last frame until a new one arrives!

### Why This Matters:
- **No More Flicker**: Clients never disappear from the grid
- **Smooth Playback**: Frames are processed in temporal order
- **Proper Lag Handling**: Buffer fills during network issues, drains when network is good
- **Natural Flow Control**: System self-regulates based on network conditions

### Remember:
- Ringbuffers are FIFO queues, not "latest frame" getters
- The buffer's job is to smooth out network irregularities
- Always have a fallback (cached frame) for when buffer is empty
- This pattern applies to ALL streaming media applications

### Common Code Patterns

### Safe Memory Allocation
```c
// Always use these macros instead of their standard non-macro counterparts.
char *buffer;
SAFE_MALLOC(buffer, size, char *);
ringbuffer_t *rb;
SAFE_CALLOC(rb, 1, sizeof(ringbuffer_t), ringbuffer_t *);
char *new_buffer;
SAFE_REALLOC(buffer, new_buffer, len, char *);
// Automatically logs errors and returns on failure
```

### Packet Sending Pattern
```c
// Build packet with network byte order
header.field = htonl(value);  // 32-bit version
// Build packet with host byte order
header.height = ntohl(header.height);

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

### Performance Considerations

- Compression threshold: 0.8 (only compress if <80% of original && > 0)
- Typical frame rate: 30-120 FPS (the developer wants to test this some day)
- Audio packet size: 256 samples (#define AUDIO_SAMPLES_PER_PACKET)
- Max packet size: 5MB (#define MAX_PACKET_SIZE)
- Socket timeouts: 10 seconds (#define CONNECT_TIMEOUT, #define SEND_TIMEOUT,
   #define RECV_TIMEOUT) from network.h
- Terminal size: The developer has a terminal of size 203x64. Claude's terminal
   is smaller but he can set it by setting COLUMNS=50 LINES=25 before running
   a command.


### Troubleshooting Quick Reference

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

### Protocol Migration Notes

### Old Two-Packet System (REMOVED)
- Used PACKET_TYPE_VIDEO_HEADER + PACKET_TYPE_VIDEO
- Caused race conditions between header and data
- Led to "DEADBEEF" corruption issues

### New Unified System (CURRENT)
- Single PACKET_TYPE_ASCII_FRAME contains header + data
- Single PACKET_TYPE_IMAGE_FRAME for camera data
- Atomic send operation prevents corruption
- 50% reduction in network overhead

### CRC Mismatch Bug Fix (TCP Stream Corruption)

### Problem
Intermittent CRC mismatches were occurring for various packet types (audio, video, stream control) between client and server. The error logs showed:
```
[ERROR] Packet CRC mismatch for type 3 from client 0: got 0x45125ad7, expected 0x12b9e3f2 (len=1024)
```

### Root Cause
The client had multiple threads sending packets concurrently to the same socket without synchronization:
- Webcam capture thread - sends video frames
- Audio capture thread - sends audio packets
- Ping thread - sends ping packets
- Data receive thread - sends pong responses

Without a mutex, these concurrent writes could interleave, causing TCP stream corruption where packet headers and data from different threads would mix.

### Solution
Added a global send mutex (`g_send_mutex`) in the client and created thread-safe wrapper functions for all packet sending operations. This ensures only one thread can write to the socket at a time, preventing packet interleaving.

**Important Architecture Note**: The server doesn't have this issue because each client has a dedicated send thread that's the only writer to that client's socket. The server can still send to multiple clients simultaneously.


## Send Thread Blocking Bug Fix (2025-08-12)

**Critical Bug**: Client send threads would get permanently blocked waiting for audio packets in video-only scenarios, preventing ASCII video frames from ever being sent to clients.

### The Problem
- Send threads check audio queue first (higher priority), then video queue
- When no packets found, code used blocking `packet_queue_dequeue()` on audio queue
- In video-only mode, audio queue stays empty forever
- Send thread blocks indefinitely, never processes queued ASCII video frames
- Result: Server creates/queues ASCII frames perfectly, but clients never receive them

### The Fix
**Location**: `src/server.c` in `client_send_thread_func()`

**Before** (blocking):
```c
if (!packet) {
  if (client->audio_queue) {
    packet = packet_queue_dequeue(client->audio_queue); // BLOCKS FOREVER
  }
}
```

**After** (non-blocking):
```c
if (!packet) {
  usleep(1000); // 1ms sleep instead of blocking indefinitely
}
```

### Why This Works
- **usleep(1ms)** prevents busy-waiting while allowing continuous queue checking
- Send thread now processes both audio and video packets efficiently
- Works for video-only, audio-only, and audio+video scenarios
- Maintains audio priority when audio packets are present
- No performance impact (1ms sleep vs indefinite blocking)

### Key Lesson
**Classic threading anti-pattern**: One blocking call can deadlock an entire threaded system. Sometimes the simplest fix (a tiny sleep) is better than complex synchronization.

**Status**: ✅ FIXED - ASCII video now displays correctly in clients

### Notes for Future Development

- The packet queue system is CRITICAL for preventing race conditions
- Always validate packet sizes before processing
- Network byte order conversion required for ALL multi-byte fields
- Framebuffer uses reference counting - let it manage memory
- Test with multiple clients early and often
- On macOS: use lldb and AddressSanitizer, NOT gdb
- When adding new packet types, MUST add to receive_packet() validation
- Check existing branch naming patterns before creating new branches
- Run `make format` before EVERY commit
- CLIENT SIDE: Must synchronize socket writes if multiple threads send packets
- NEVER use forward declarations - use proper header files instead
- When you need to reference a type from another module, include its header file
