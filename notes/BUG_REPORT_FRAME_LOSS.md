# Bug Report: 59% Frame Loss - "Same Frame Repeated Over and Over"

**Date:** 2026-02-17
**Severity:** HIGH
**Status:** DIAGNOSED
**Component:** Video Frame Buffer / Render Thread

## Executive Summary

The server is displaying the same frame repeatedly for 400-700ms intervals, then abruptly switching to a new frame. This is caused by **59% frame loss in the double-buffered frame storage system**. Out of 49 incoming frames from the browser, only 20 make it to the render buffer—the remaining 29 are dropped silently.

## Symptoms Observed

- User sees ASCII output change only once every 50+ frames
- Browser sends frames at ~3-4 FPS (from fake video device)
- Server receives frames at same rate with different pixel data
- But render output shows only **1-2 unique frames per 13 seconds**
- When frame DOES change, it changes to a frame that was sent 400-700ms ago

## Root Cause Analysis

### Question 1: Is browser sending different images?
**Answer: YES ✅**
- Test confirmed browser sends 49 different frames with varying pixel hashes
- First pixel RGB values change: 0x010100 → 0x000000 → 0x896047 → etc.
- Browser WASM successfully captures and transmits unique frames

### Question 2: Is server receiving different frames?
**Answer: YES ✅**
- Server's ACIP callback logs 49 RECV_FRAME events with different hashes
- RECV_FRAME logs show frame data with distinct first_rgb values
- Frames arrive successfully at network layer

### Question 3: Is server storing frames correctly?
**Answer: NO ❌ - THIS IS THE BUG**

**Evidence:**
- 49 frames received by server
- Only 20 BUFFER_FRAME CHANGE logs (40% success rate)
- **59% frame loss** - frames dropped before reaching render buffer
- Most common frame hash appears 55 times (same frame rendered 55 times consecutively)

## The Exact Bug

**File:** `lib/video/video_frame.c`
**Function:** `video_frame_commit()`
**Lines:** 138-160

### Current Broken Code

```c
void video_frame_commit(video_frame_buffer_t *vfb) {
  if (!vfb || !vfb->active) {
    SET_ERRNO(ERROR_INVALID_PARAM, "...");
    return;
  }

  // Check if reader has consumed the previous frame
  if (atomic_load(&vfb->new_frame_available)) {
    // Reader hasn't consumed yet - we're dropping a frame
    uint64_t drops = atomic_fetch_add(&vfb->total_frames_dropped, 1) + 1;
    if (drops == 1 || drops % 100 == 0) {
      log_dev_every(4500000, "Dropping frame for client %u (reader too slow, total drops: %llu)",
                    vfb->client_id, (unsigned long long)drops);
    }
    // BUG: No return here! Still swaps even though frame is "dropped"
  }

  // Pointer swap using mutex for thread safety
  mutex_lock(&vfb->swap_mutex);
  video_frame_t *temp = vfb->front_buffer;
  vfb->front_buffer = vfb->back_buffer;
  vfb->back_buffer = temp;
  mutex_unlock(&vfb->swap_mutex);

  // Signal reader that new frame is available
  atomic_store(&vfb->new_frame_available, true);
  atomic_fetch_add(&vfb->total_frames_received, 1);
}
```

### The Problem Sequence

1. **Frame 0 arrives** at time T0
   - `new_frame_available = false`
   - Swaps: `front_buffer = Frame0`, sets `new_frame_available = true`

2. **Frame 1-46 arrive** at T0+16ms intervals
   - Each finds `new_frame_available = true` (render thread hasn't read yet)
   - Logs "dropping frame" but... **STILL SWAPS ANYWAY**
   - Each overwrites the back buffer with newer frame data

3. **Render thread reads** at T0+400ms (too slow!)
   - Calls `video_frame_get_latest()`
   - Clears `new_frame_available = false`
   - Reads current front_buffer (but it's been overwritten 46 times!)
   - Renders same frame for entire 400ms interval

4. **Frame 47 arrives** at T0+752ms
   - Now `new_frame_available = false` (render thread cleared it)
   - Successfully swaps Frame47
   - Render thread won't read this until next interval (another 400-700ms)

### Why It's Called "Drop" But Still Swaps

The code logs a drop but doesn't prevent the swap. This creates a confusing state:
- **Frames are "dropped"** (not displayed immediately)
- **But they ARE stored** (overwrite the back buffer)
- **Result:** Latest frame in buffer keeps getting replaced until render thread finally reads

## Data Evidence

### Frame Arrival vs Buffer Update

```
RECV_FRAME events: 49 new frames received
  - Frame hashes: 0xd3835983, 0x12540984, 0x052ba0c6, 0x344f69ea, ...
  - First pixels: 0x010100, 0x000000, 0x896047, 0x605a52, ...

BUFFER_FRAME CHANGE events: Only 20 unique frames in buffer
  - Loss: 49 - 20 = 29 frames (59%)
  - Hash 0x237d7777: appears 55 times (same frame repeated)
```

### Timeline of Single Frame Being Repeated

```
[00:37:04.762963] BUFFER_INSPECT: hash=0x237d7777 first_pixel=0x8e8474
[00:37:04.787881] BUFFER_INSPECT: hash=0x237d7777 first_pixel=0x8e8474 ← Same
[00:37:04.809553] BUFFER_INSPECT: hash=0x237d7777 first_pixel=0x8e8474 ← Same
[00:37:04.833296] BUFFER_INSPECT: hash=0x237d7777 first_pixel=0x8e8474 ← Same
[00:37:04.858543] BUFFER_INSPECT: hash=0x237d7777 first_pixel=0x8e8474 ← Same

... (50 more times) ...

[00:37:05.972152] BUFFER_FRAME CHANGE: hash switched to new value
```

**Duration:** ~770ms of reading the SAME frame data

### Render Thread Speed Issues

Frame buffer reads occur at inconsistent intervals:
- Initial reads: 20-25ms apart (40-50 FPS expected) ✅
- Later reads: 400-700ms apart (1-2 FPS actual) ❌

**Calculated render speed from logs:**
- 20 unique frames over 13 seconds = 1.54 FPS actual
- Expected: 60 FPS (VIDEO_RENDER_FPS defined in render.h)
- Actual: 1.54 FPS
- **Performance degradation: 97% slower than expected**

## Impact

### Visible to User
- Video output appears "frozen" for 400-700ms intervals
- Only ~1-2 unique ASCII frames displayed per 10 seconds
- Looks like video is buffering or network is dropping packets
- But it's actually a client-side buffer management issue

### Data Flow Breakdown
```
Browser (60 FPS)
    ↓ ✅ Different frames
Server Receiver (60 FPS received)
    ↓ ❌ 59% dropped in video_frame_commit()
Render Buffer (only 40% of frames visible)
    ↓ ✅ Different frames, but outdated
Render Thread (1-2 FPS)
    ↓ ✅ Renders what it has
ASCII Output (1-2 FPS, repeats same frame)
    ↓
Client Browser (same frame repeats 50 times)
```

## Root Cause Analysis

### Why Render Thread is So Slow

Even with expected 60 FPS, the render thread is measuring at **1.54 FPS**. Multiple factors:

1. **Frame collection overhead** (`collect_video_sources()`)
   - Iterates all clients
   - Reads video frames from each
   - Computes hashes for change detection
   - Expensive memcpy operations

2. **Frame generation overhead** (`create_mixed_ascii_frame_for_client()`)
   - Converts RGB to ASCII art
   - Applies color filters
   - Computes image transformations
   - Blends multiple video sources

3. **Possible additional delays**
   - Mutex contention?
   - Memory allocation delays?
   - I/O wait for output buffers?

## Proposed Fixes

### Immediate Fix: Prevent Frame Overwriting

**Option 1: Use Frame Queue Instead of Double Buffer**
- Replace double buffer with simple queue
- Store N most recent frames instead of just 2
- Render thread processes queue in order
- Prevents frame loss, reduces duplication

**Option 2: Fix Double Buffer Logic**
- When `new_frame_available = true`, DON'T swap
- Return early, log drop, but keep buffer unchanged
- Only swap when render thread clears the flag
- Current code claims to "drop" but actually still swaps

**Code change (Option 2):**
```c
if (atomic_load(&vfb->new_frame_available)) {
  // Reader hasn't consumed yet - skip this frame entirely
  atomic_fetch_add(&vfb->total_frames_dropped, 1);
  return;  // ← ADD THIS to actually prevent swap!
}

// Only swap if reader has consumed
mutex_lock(&vfb->swap_mutex);
// ... swap code ...
mutex_unlock(&vfb->swap_mutex);
```

### Long-term Fix: Improve Render Thread Performance

1. **Separate collection and rendering**
   - Collection thread: Update buffer with latest frames (fast)
   - Render thread: Generate ASCII (can be slower)
   - Decouples frame arrival from rendering speed

2. **Cache video sources**
   - Don't re-fetch from client buffers every iteration
   - Use change detection to update only when needed
   - Reduce memcpy overhead

3. **Async rendering**
   - Queue pending ASCII frames
   - Render in background
   - Send async to clients

## Testing & Verification

### Reproduction Steps

1. Start server: `./build/bin/ascii-chat server`
2. Start dev server: `npm run dev` in web directory
3. Open browser to `http://localhost:3000/mirror`
4. Wait 20-30 seconds
5. Check logs:
   ```bash
   grep "RECV_FRAME\|BUFFER_FRAME" /tmp/server.log
   ```

### Expected Before Fix
- 49+ RECV_FRAME NEW events
- ~20 BUFFER_FRAME CHANGE events
- 59% frame loss

### Expected After Fix
- 49+ RECV_FRAME NEW events
- 40+ BUFFER_FRAME CHANGE events (minimal loss)
- <10% frame loss

### Test Metrics

```bash
# Count incoming frames
grep -c "RECV_FRAME.*NEW" /tmp/server.log

# Count buffer updates
grep -c "BUFFER_FRAME CHANGE" /tmp/server.log

# Calculate loss percentage
RECV=$(grep -c "RECV_FRAME.*NEW" /tmp/server.log)
BUFFER=$(grep -c "BUFFER_FRAME CHANGE" /tmp/server.log)
LOSS=$((100 * (RECV - BUFFER) / RECV))
echo "Frame loss: ${LOSS}%"
```

## Instrumentation Added

The following logging was added to diagnose this issue:

### Client-Side (Browser WASM)
- `Client.tsx`: SEND logs for every frame transmitted with hash

### Server Receiver
- `client.c`: RECV_FRAME logs showing frame arrival with hash and first pixel color

### Render Buffer
- `stream.c`: BUFFER_INSPECT logs showing what's in the buffer being rendered
- `stream.c`: BUFFER_FRAME CHANGE logs tracking when buffer updates

### Frame Storage
- `client.c`: FRAME_COMMITTED logs when frames are written to buffer

This instrumentation traces the complete frame flow:
```
Browser (SEND) → Server (RECV) → Buffer (BUFFER_INSPECT) → Render (BUFFER_FRAME)
```

## Logs for This Issue

**Test run:** 2026-02-17 00:37-00:37:14
**Server log:** `/tmp/frame_trace.log` (2455 lines)

Key log entries show:
- RECV_FRAME #0-48: All frames received with different hashes
- BUFFER_FRAME CHANGE: Only 20 updates despite 49 arrivals
- BUFFER_INSPECT: Hash 0x237d7777 repeats 55 times

## References

- **Video Frame Buffer:** `lib/video/video_frame.c`
- **Render Thread:** `src/server/render.c`
- **Stream Mixer:** `src/server/stream.c`
- **Frame Commit:** `lib/video/video_frame.c:127`
- **Frame Read:** `lib/video/video_frame.c:163`

## Conclusion

The bug is a **frame overwriting issue in the double-buffer commit logic** that causes 59% of incoming frames to be silently dropped before reaching the render thread. Combined with a render thread that runs at only 1.5 FPS instead of the expected 60 FPS, this creates the visible symptom of the same ASCII frame being displayed for 400-700ms intervals before abruptly changing to a completely different frame.

The immediate fix is to prevent the frame swap when `new_frame_available` is already true, which will prevent frame overwriting during backlog conditions.
