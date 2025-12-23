# Bug Report - ascii-chat Codebase Analysis

Generated: 2025-12-23

## Summary

A comprehensive analysis of the ascii-chat codebase identified **28 bugs** across multiple severity levels:

| Severity | Count |
|----------|-------|
| CRITICAL | 6 |
| HIGH | 10 |
| MEDIUM | 8 |
| LOW | 4 |

---

## CRITICAL Bugs

### 1. Thread Cleanup Timing Race Condition
**File**: `src/server/client.c:785-818`
**Type**: Race condition

After thread join attempts, the code waits only 10ms + 1ms before destroying mutexes. On Windows, threads may still be starting (at `RtlUserThreadStart`) when mutexes are destroyed, causing crashes when threads try to lock destroyed mutexes.

```c
platform_sleep_usec(10000); // 10ms - INSUFFICIENT
// ...
platform_sleep_usec(1000); // 1ms - INSUFFICIENT
mutex_destroy(&target_client->client_state_mutex); // Threads may still be running!
```

**Fix**: Use proper thread synchronization (events/semaphores) instead of sleep-based waits.

---

### 2. Windows Thread Detach Without Validation
**File**: `lib/platform/windows/thread.c:711-713`
**Type**: Null pointer dereference

```c
int ascii_thread_detach(asciithread_t *thread) {
  CloseHandle((*thread));  // No validation!
  return 0;
}
```

No validation that `thread` is non-NULL or that `*thread` is valid before calling `CloseHandle()`.

**Fix**: Add null/invalid handle checks before `CloseHandle()`.

---

### 3. ANSI Escape Sequence Buffer Overflow
**File**: `lib/image2ascii/ansi_fast.c:128`
**Type**: Buffer overflow

```c
if (color_changed && (ctx->length + 32 < ctx->capacity)) { // Reserves 32 bytes
```

Comment at line 68 says FG+BG mode needs "Maximum output: 38 bytes", but only 32 bytes are reserved.

**Fix**: Change `32` to `40` (38 + safety margin).

---

### 4. Buffer Underflow in Grid Layout
**File**: `lib/image2ascii/ascii.c:499-503`
**Type**: Integer underflow

```c
int dst_pos = dst_row * (width + 1) + h_padding;
```

If `h_padding` becomes negative, `dst_pos` underflows. When cast to `size_t`, becomes huge positive, bypassing bounds checks.

**Fix**: Validate `h_padding >= 0` and use `size_t` throughout.

---

### 5. Windows Thread Startup Race
**File**: `src/server/client.c:599-614`
**Type**: Race condition (Windows-specific)

After `ascii_thread_create()` returns, the thread may not have started executing yet (queued at `RtlUserThreadStart`). If cleanup runs before thread starts, crash occurs.

**Fix**: Add thread initialization event that threads signal after completing startup.

---

### 6. Socket Access Race During Cleanup
**File**: `src/server/client.c:694-704, 943`
**Type**: Race condition

Socket is closed in `remove_client()` while receive thread reads it without holding mutex:
```c
// remove_client() - holds mutex
socket_close(client->socket);

// client_receive_thread() - NO mutex
socket_t socket = client->socket;  // Race!
```

**Fix**: Always protect socket access with mutex or use atomic operations.

---

## HIGH Severity Bugs

### 7. Winsock Initialization Not Thread-Safe
**File**: `lib/platform/windows/socket.c:21-31`

```c
static int winsock_initialized = 0;  // Not atomic!
if (!winsock_initialized) {  // Race condition
```

Multiple threads can simultaneously pass the check and call `WSAStartup()`.

**Fix**: Use `atomic_int` with compare-exchange.

---

### 8. Weak Atomic CAS Without Retry
**File**: `src/server/protocol.c:519`

```c
if (atomic_compare_exchange_weak(&client->is_sending_video, &was_sending_video, true)) {
```

`_weak` can fail spuriously. Should use `_strong` or add retry loop.

---

### 9. Ringbuffer TOCTOU Race Condition
**File**: `lib/ringbuffer.c:100-120`

Check-then-act pattern where buffer state is checked outside lock, then read inside lock. State can change between check and read.

---

### 10. Ringbuffer Framebuffer Write Race
**File**: `lib/ringbuffer.c:250-261`

Buffer fullness checked outside mutex, then frame written with mutex. Another thread could drain queue between check and lock.

---

### 11. Audio Callback Static Variable Race
**File**: `lib/audio/audio.c:37-38`

```c
static int callback_count = 0;  // NOT thread-safe!
callback_count++;  // Data race
```

PortAudio callbacks may be called from multiple threads.

**Fix**: Use atomic operations.

---

### 12. Buffer Pool Free Logic Error
**File**: `lib/buffer_pool.c:358-374`

If global pool not initialized, assumes memory was malloc'd and frees it. Wrong assumption if allocated from local pool.

---

### 13. Webcam Test Pattern Memory Leak
**File**: `lib/os/webcam.c:57-70`

Every `webcam_read()` in test pattern mode allocates new `image_t`, but documentation says caller shouldn't free it. Leaks ~60 frames/second.

**Fix**: Use static buffer or update documentation.

---

### 14. Invalid Pointer Arithmetic in Image Flip
**File**: `lib/os/webcam.c:181-182`

```c
rgb_t *right = frame->pixels + frame->w - 1;  // If w==0, points before buffer
```

**Fix**: Add `if (frame->w <= 1) return frame;`

---

### 15. Missing Sensitive Data Cleanup
**File**: `lib/crypto/handshake.c:258`

Password stored in context but not zeroed on error paths.

**Fix**: `sodium_memzero()` password immediately after key derivation.

---

### 16. Hardcoded HMAC Size Mismatch
**File**: `lib/crypto/crypto.c:812-824, 998`

`crypto_verify_hmac()` uses hardcoded 32-byte sizes, but context allows negotiable sizes.

---

## MEDIUM Severity Bugs

### 17. Redundant Username Initialization
**File**: `lib/platform/windows/system.c:77-79`

```c
get_username_env();
get_username_env();  // Duplicate
get_username_env();  // Duplicate
```

---

### 18. Global Errno State Not Thread-Safe
**File**: `lib/network/network.c:160-182`

`asciichat_errno_context` modified without synchronization in multi-threaded scenarios.

---

### 19. Palette Length Byte/Character Confusion
**File**: `lib/palette.c:363`

Uses `strlen()` (byte count) instead of `palette->length` (character count) for UTF-8 palettes.

---

### 20. Incomplete Error Handling in Handshake
**File**: `lib/crypto/handshake.c:1307-1320`

Redundant `SET_ERRNO()` calls overwrite actual error messages with generic ones.

---

### 21. Nonce Counter Edge Case
**File**: `lib/crypto/crypto.c:103`

Comment says 0 reserved for testing, but no protection against reaching 0 during wrap-around.

---

### 22. Deterministic Password Salt
**File**: `lib/crypto/crypto.c:310`

Hardcoded salt weakens password-based authentication against offline attacks.

---

### 23. Signed/Unsigned Comparison in Network Receive
**File**: `lib/network/network.c:273-328`

`total_received` (signed) compared with `len` (unsigned). Undefined behavior if total_received negative.

---

### 24. Framebuffer Double-Free Detection Fragile
**File**: `lib/ringbuffer.c:224-236`

Uses magic pointer `0xDEADBEEF` as freed indicator - not reliable on all systems.

---

## LOW Severity Bugs

### 25. Redundant NULL Check in Thread Wrapper
**File**: `lib/platform/windows/thread.c:385-401`

Same NULL check for `wrapper->posix_func` performed twice.

---

### 26. Missing Error Context in Thread Join
**File**: `lib/platform/windows/thread.c:642-660`

Returns -1 without `SET_ERRNO()` when `WaitForSingleObject()` fails.

---

### 27. Inconsistent Socket Close Error Handling
**File**: `lib/platform/windows/socket.c:60-65`

Windows version doesn't set error context, unlike POSIX version.

---

### 28. Unused Buffer Allocation
**File**: `lib/image2ascii/image.c:306-311`

`lines` buffer allocated but never used; only `ob.buf` is used.

---

## False Positives (Verified as Not Bugs)

### packet_queue.c:573 - Reported as "use-after-free"
The agent incorrectly flagged this:
```c
packet->header.magic = 0xDEADBEEF;  // Written BEFORE free
SAFE_FREE(packet);                   // Then freed
```
This is actually CORRECT - magic is written before free to detect double-free, not after.

### ssh_agent.c:167 - Reported as "buffer over-read"
The code actually has proper bounds checking: `blob_pos + 32 <= pos + blob_len`

---

## Recommendations

1. **Immediate Priority**: Fix CRITICAL bugs #1-#6 (thread synchronization, buffer overflows)
2. **Short Term**: Address HIGH severity thread safety issues
3. **Testing**: Add stress tests for multi-client disconnect scenarios
4. **Windows**: Consider adding proper thread lifecycle events instead of sleep-based synchronization
