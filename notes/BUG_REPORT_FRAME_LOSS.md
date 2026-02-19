# Bug Report: Frame Loss in WebSocket Client Mode

**Date:** 2026-02-17
**Severity:** CRITICAL
**Status:** FIXED
**Component:** WebSocket server event loop

## User-Observed Behavior

1. Start server, navigate to `/client` in browser
2. **ONE frame renders**
3. Several more frames render over the next few seconds
4. **Frame rendering STOPS completely (0 FPS)**
5. After **~45 seconds**, the browser suddenly renders many frames at once (catching up to real-time)
6. **STOPS again (0 FPS)**
7. Pattern repeats: 45-second cycle of ~1-3 FPS followed by 0 FPS stalls

## Root Cause Found

**Location:** `lib/network/websocket/server.c:785`

```c
int result = lws_service(server->context, -1);  // BUG: -1 causes blocking
```

### The Problem

Passing `-1` to `lws_service()` was supposed to force non-blocking mode, but this is incorrect. The parameter `-1` actually causes `lws_service()` to:
- Use a very long internal timeout
- Block the service thread indefinitely or for extended periods
- Prevent RECEIVE callbacks from firing while blocked
- Prevent WRITEABLE callbacks from firing

### The Cascade

1. Service thread calls `lws_service(-1)` and blocks
2. No RECEIVE callbacks fire → fragments don't queue to recv_queue
3. No WRITEABLE callbacks fire → responses can't be sent
4. Handler thread blocks waiting for fragments in websocket_recv()
5. Queue remains empty
6. After ~45 seconds, an OS-level timeout (TCP or internal) wakes the service thread
7. Queued fragments suddenly process as a burst
8. Back to blocking state → cycle repeats

This explains the exact 45-second burst/stall pattern observed.

## The Fix

Changed line 785:

```c
// BEFORE (BLOCKING):
int result = lws_service(server->context, -1);

// AFTER (NON-BLOCKING):
int result = lws_service(server->context, 50); // 50ms timeout
```

Use a **50ms timeout** instead of -1. This keeps the event loop responsive:
- Poll every 50ms for new socket activity
- RECEIVE callbacks fire continuously as fragments arrive
- WRITEABLE callbacks fire to send responses
- No more 45-second stalls

This matches the correct pattern already used in `transport.c:87` for the client-side WebSocket service thread.

### UPDATE:
this fixed NOTHING. maybe it's good practice but not relevant to this bug. just remember this has nothing to do with it.

## Current Bug: WebSocket Frames Not Sent Back to Client (2026-02-17 23:10)

**Observed Behavior:**
1. Browser connects to server via WebSocket ✓
2. Frames arrive continuously (RECEIVE callbacks firing) ✓
3. Send thread loops and gets video frames ✓
4. Frame->data pointer is valid ✓
5. **Frame->size is 0** - frame skipped
6. No ASCII art sent back to browser ✗

**Root Cause Chain:**
1. **WebSocket frames aren't being processed into incoming_video_buffer**
   - No RECV_FRAME logs appear (incoming frames should be logged)
   - TCP clients show RECV_FRAME logs immediately upon connection
   - WebSocket client never shows ANY RECV_FRAME logs

2. **incoming_video_buffer stays empty → sources_with_video = 0**
   - Render thread: sources=0 (no active video sources)
   - create_mixed_ascii_frame_for_client returns NULL when sources=0
   - Frame size set to 0

3. **Send thread skips zero-size frames**
   - Logs show: FRAME_DATA_OK but SKIP_ZERO_SIZE
   - Nothing sent back to client

**Why TCP Works, WebSocket Doesn't:** I DON'T KNOW!!! I have one theory.
- Same code path for storing incoming frames
- Same send thread implementation
- TCP clients' incoming frames ARE being stored (RECV_FRAME logs appear)
- WebSocket clients' incoming frames are NOT being stored (no logs)
- My one theory: the difference must be WebSockets, so our LWS usage has bugs. We didn't implement it right.

**Missing Link:**
WebSocket client frames are arriving but not being decoded/dispatched to the IMAGE_FRAME handler. The receive thread
isn't processing WebSocket packets correctly or isn't dispatching them to protocol handlers.

## Files Modified

- `lib/network/websocket/server.c` - Fixed lws_service() timeout parameter from -1 to 50ms
- `lib/network/websocket/transport.c` - Reduced recv_mutex lock contention (2026-02-17)


# UPDATE (2026-02-18 19:00): 0fps yet crypto handshake completes
i'm at zero fps. some changes we made to fix lws implementation stopped me from even getting 1fps. now the
implementation sends zero ascii frames to the browser. frankly here's what's happening. we connect. we do the crypto
handshake so packets flow back and forth, so the server websocket implementation works here. we send images. we never
get any ascii art packets from the server. that's the bug. nothing else. it's not the fact that the recieve queue is
filling up and i want you to understand why: WE DONT SEND ANY ASCII ART TO THE BROWSER AND THAT IS LITERALLY THE BUG
WE'RE WORKING ON SO OF COURSE THE RECIEVE QUEUE IS FULL. the browser and server can clearly process acip packets over
websockets because the crypto handshake completes every time. and then after that the browser starts spamming images to
the server, but is never sent any ascii art. but it got sent crypto handshake packets. so there's something about image
packets or ascii art packets that are not being handled properly in the codebase. fixing this will probably fix the
entire bug report, i suspect, because i don't know why we ever got 1fps every 45 seconds to begin with if we can't seem
to process image or ascii data. we need to find out why we have image frames in memory but aren't calling the send
function, because no send logs log. idea: we could just log the image data to see if we can actually access it in
memory... if we can log it we have no excuse for why our code isn't just sending it back, because log and send are
simply two different functions to call. try this.
update: we tried my idea. the frame size is zero. investigating. claude thinks the browser isn't sending image frames.
it is and i haven't touched that code. console.log is logging image frame sends. the websocket connection in the network
tab has dozens of image frame sends per second until while connected to the server, after sending AND RECIEVING crypto
handshake packets. still no ascii art packets. the browser IS sending images according to the network tab which doesn't
lie randomly. so somewhere on the server the frame size is set to zero.

update: BACK TO 1 frame every 45 seconds! We fixed it and worked for some time without knowing because my lights were
off so the ascii art was all spaces. still, 1 frame every in 45 seconds is not what i want. i figured out one of the
reasons it's every 45 seconds: it's when the connection breaks, probably because we're filling the server buffers with
image frame data and it's not processing it into ascii art and sending it back to the browser. still. and i don't know
why yet, but i have a hint:

update: we discovered the writeable callbacks aren't draining the queue for some reason. investigating. they drain the queue for crypto
handshake data and for the first ascii art frame. but not for any more ascii art frames after that. the queue fills up.
this is my question "why do you think the callbacks are firing but not running the code that drains the queue past the
first ascii art frame sent back? why does it work for the whole crypto handshake back and forth and succeed for one
ascii art frame and then stop draining?"

idea: why don't we just use lldb or gdb to take backtraces of every thread after it sends one frame of ascii art to the
client? or python with lldb.py or whatever the liblldb python module is called. insight: each thread is doing something.
we want the network thread to be processing network data and the send thread toe be sending data to the client. each of
these threads is at a certain point in code. we want that point in code to be "sending ascii frames to the client" and
it's not. so we can simply see what it IS, and then we'll know what's going on and how to fix it. is LWS blocking
because buffers are fully, waiting for us to drain them and our code isn't setup properly to do it? i thought it
happened with LWS callbacks so that should be setup. maybe our code is hanging at a network mutex that was never
unlocked and needs to be fixed so it doesn't block the thread we need processing ascii art. we simply don't know. if
it's our code or LWS code that is the problem because we don't know what the program execution counter is at when it
hangs after sending one ascii frame. what's it doing? it's hanging somewhere, the send thread or network thread, or
something. LET'S FIND OUT.

update: debug/memory.c and logging have recursive mutex contention which makes using the logging system. debug/lock.h to
the rescue. we know the blocking call from lldb backtraces so we can log the currently held locks right before making
that blocking call to see who already has it. will update...
