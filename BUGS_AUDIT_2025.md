# ascii-chat Security Audit - December 2025

## Executive Summary

Comprehensive codebase audit identified **50+ bugs** across all major components:
- **CRITICAL**: 15 bugs (security vulnerabilities, crashes, memory corruption)
- **HIGH**: 14 bugs (race conditions, resource leaks, logic errors)
- **MEDIUM**: 15 bugs (code quality, edge cases, performance)
- **LOW**: 6 bugs (minor issues, dead code)

---

## CRITICAL BUGS (Immediate Fix Required)

### 1. Missing Ed25519 Signature Verification in Client Authentication
- **File**: `lib/crypto/handshake.c:1552-1580`
- **Impact**: Authentication bypass - attackers can impersonate whitelisted users
- **Description**: Server receives client's Ed25519 signature on challenge nonce but NEVER verifies it. Any attacker can claim to be a whitelisted user.
- **Fix**: Add `crypto_sign_verify_detached()` after extracting signature from payload

### 2. Integer Overflow in Client Frame Allocation (RCE Risk)
- **File**: `src/client/protocol.c:354,377`
- **Impact**: Buffer overflow leading to potential remote code execution
- **Description**: When `header.original_size = UINT32_MAX`, adding 1 overflows to 0, causing tiny allocation followed by massive write
- **Fix**: Validate `header.original_size <= SIZE_MAX - 1` before allocation

### 3. Missing Mutex Protection in Framebuffer Operations (3 locations)
- **Files**: `lib/ringbuffer.c:239-285, 287-334, 336-375`
- **Impact**: Race conditions causing data corruption, crashes
- **Description**: Single-source `framebuffer_write_frame`, `framebuffer_read_frame`, and `framebuffer_clear` have no mutex protection despite documentation claiming thread-safety
- **Fix**: Add `mutex_lock/unlock` around ringbuffer operations

### 4. Buffer Pool Corruption from Size Mismatch
- **File**: `src/server/client.c:1730-1755`
- **Impact**: Heap corruption, crashes
- **Description**: Buffer allocated with ciphertext size but freed with plaintext size after decryption
- **Fix**: Store original allocation size before updating length

### 5. Dangling Pointer After Header Skip
- **File**: `src/server/client.c:1747-1756`
- **Impact**: Heap corruption when freeing adjusted pointer
- **Description**: Data pointer adjusted to skip header, then that adjusted pointer is freed
- **Fix**: Keep original pointer for deallocation

### 6. Hash Collision Vulnerability in Audio Mixer
- **File**: `lib/audio/mixer.c:558, 562-564`
- **Impact**: Wrong client audio excluded, incorrect mixing
- **Description**: Hash table uses only `client_id & 0xFF` with no collision detection, causing overwrites
- **Fix**: Implement proper collision detection using `source_id_at_hash` array

### 7. Undefined Behavior in Bitset Operations
- **File**: `lib/audio/mixer.c:562-564`
- **Impact**: UB when `exclude_index >= 64`
- **Description**: `1ULL << exclude_index` is undefined when exclude_index >= 64
- **Fix**: Add bounds check `exclude_index < MIXER_MAX_SOURCES && exclude_index != 0xFF`

### 8. Null Pointer Dereference in Audio Device Info (2 locations)
- **File**: `lib/audio/audio.c:558-560, 646-649`
- **Impact**: Crash when device info unavailable
- **Description**: `Pa_GetDeviceInfo()` can return NULL but is dereferenced immediately
- **Fix**: Add NULL check before dereferencing

### 9. Integer Overflow in Dithering Error Buffer
- **File**: `lib/image2ascii/ansi_fast.c:353, 399, 409, 416, 423`
- **Impact**: Out-of-bounds buffer access
- **Description**: `y * width + x` can overflow when processing large images
- **Fix**: Cast to `size_t` before multiplication

### 10. Unbounded image_pixel() Access
- **File**: `lib/image2ascii/image.c:151-153`
- **Impact**: Buffer overflow on any out-of-bounds access
- **Description**: No bounds checking on x/y coordinates
- **Fix**: Add bounds validation, return NULL on invalid access

### 11. Integer Underflow in Log Rotation
- **File**: `lib/logging.c:144`
- **Impact**: Invalid lseek on log rotation
- **Description**: `g_log.current_size - keep_size` underflows if size tracking is corrupted
- **Fix**: Check `g_log.current_size >= keep_size` before subtraction

### 12. Double-Free Detection Broken on Big-Endian Systems
- **File**: `lib/packet_queue.c:557-573`
- **Impact**: Memory corruption on big-endian platforms
- **Description**: Magic marker written in host byte order, checked in network byte order
- **Fix**: Use `htonl(0xBEEFDEAD)` for freed marker

### 13. TOCTOU Race in Queue Size Tracking
- **File**: `lib/packet_queue.c:200-230`
- **Impact**: Queue can exceed max_size under concurrent load
- **Description**: Size check and increment are not atomic
- **Fix**: Use atomic compare-and-swap or mutex protection

### 14. Windows Thread Handle Leak on Error
- **File**: `lib/platform/windows/thread.c:642-661, 670-696`
- **Impact**: Handle exhaustion after ~1000 failed joins
- **Description**: Thread handles not closed when WaitForSingleObject fails
- **Fix**: Always close handle on error paths

### 15. Unprotected Crypto Context During Rekeying
- **File**: `src/server/client.c:1383, 1462`
- **Impact**: Race condition during key exchange
- **Description**: Crypto context accessed without mutex while rekeying may be modifying it
- **Fix**: Hold `client_state_mutex` when accessing crypto context

---

## HIGH SEVERITY BUGS

### 16. Race Condition on Global Terminal Caps
- **File**: `lib/logging.c:23-25, 302-304, 749-787`
- **Description**: `g_terminal_caps` accessed without synchronization from multiple threads

### 17. Race Condition on Shutdown Callback
- **File**: `lib/common.c:53`
- **Description**: `g_shutdown_callback` read/written without atomic operations

### 18. Format String Bug in Memory Pool Logging
- **File**: `lib/packet_queue.c:82-83`
- **Description**: Extra format specifier without argument

### 19. Integer Overflow in Encrypted Packet Size
- **File**: `lib/network/packet.c:465, 481`
- **Description**: No overflow check before adding header/crypto sizes

### 20. Packet Split-Send Race Condition
- **File**: `lib/network/packet.c:282-306`
- **Description**: Header and payload sent separately; another thread could interleave

### 21. Timing Attack in SSH Agent Key Comparison
- **File**: `lib/crypto/ssh_agent.c:168`
- **Description**: Uses `memcmp()` instead of `sodium_memcmp()`

### 22. Stack Overflow Risk from VLAs
- **File**: `lib/audio/mixer.c:453, 455`
- **Description**: 32KB VLA on stack per audio frame

### 23. Race Condition on Volatile Audio Indices
- **File**: `lib/audio/audio.c:343-357`
- **Description**: Volatile indices read without proper synchronization

### 24. Integer Overflow in Grid Layout
- **File**: `lib/image2ascii/ascii.c:643, 665`
- **Description**: `row * (width + 1)` can overflow

### 25. Socket Access After State Change
- **File**: `src/server/client.c:695-704, 888-889`
- **Description**: Socket validity check and socket use not atomic

### 26. Insufficient Lock Coverage in Broadcast
- **File**: `src/server/client.c:1599-1632`
- **Description**: Clients can be removed between snapshot and access

### 27. Double-Free Risk in Audio Batch
- **File**: `src/server/client.c:1261-1264, 1289`
- **Description**: Partial allocation failure can cause double-free

### 28. Missing Size Validation for Decompression
- **File**: `src/client/protocol.c:346-365`
- **Description**: No upper bound on frame size before allocation

### 29. Connection State Race Condition
- **File**: `src/client/server.c:793-816`
- **Description**: TOCTOU between connection check and socket use

---

## MEDIUM SEVERITY BUGS

### 30. Unchecked setsockopt Returns (6 locations)
- **File**: `lib/platform/posix/socket.c:59-90`

### 31. Buffer Overflow Risk in Timestamp Formatting
- **File**: `lib/logging.c:27-55`
- **Description**: Function assumes 32-byte buffer without size parameter

### 32. Memory Leak in framebuffer_peek Documentation Mismatch
- **File**: `lib/ringbuffer.c:487-495`
- **Description**: Documentation doesn't mention caller must free returned data

### 33. Null Pool Pointer Dereference Risk
- **File**: `lib/buffer_pool.c:144-172`
- **Description**: Sub-pool creation failures not checked

### 34. Misleading SAFE_MEMCPY Size Parameters
- **File**: `lib/ringbuffer.c:264, 270`
- **Description**: Allocated size differs from MEMCPY dest_size parameter

### 35. Double-Free Detection Using Magic Pointer
- **File**: `lib/ringbuffer.c:225-235`
- **Description**: Fragile detection using 0xDEADBEEF pointer comparison

### 36. Missing Packet Type Validation
- **File**: `lib/network/packet.c:625, 648`
- **Description**: Decrypted packet type not validated against enum range

### 37. Non-Constant-Time memcmp in SSH Keys
- **File**: `lib/crypto/keys/ssh_keys.c:937`
- **Description**: Uses `memcmp()` for key comparison

### 38. Missing Hash Collision Detection
- **File**: `lib/audio/mixer.c:278-280`
- **Description**: `source_id_at_hash` array allocated but never used

### 39. Integer Type Mismatch in Audio Read
- **File**: `lib/audio/audio.c:710-711`
- **Description**: `size_t` return cast to `int`

### 40. Unsigned Underflow in Palette
- **File**: `lib/palette.c:315`
- **Description**: `palette_len - 1` can underflow if len is 0

### 41. Buffer Underflow Risk in image_print
- **File**: `lib/image2ascii/image.c:709-711`
- **Description**: palette_index could be negative

### 42. Capacity Underflow in ANSI RLE
- **File**: `lib/image2ascii/ansi_fast.c:166`
- **Description**: Missing NULL check before buffer access

### 43. Uninitialized Crypto Buffers
- **File**: `src/server/client.h:131-134`
- **Description**: crypto_*_buffer fields never allocated

### 44. Integer Overflow in Audio Sample Calculation
- **File**: `src/server/client.c:1268-1269`
- **Description**: No overflow check on total_samples * sizeof(float)

### 45. Display Logic Error
- **File**: `src/client/display.c:353-375`
- **Description**: Frame output twice in snapshot mode with TTY

### 46. Insufficient Audio Batch Validation
- **File**: `src/client/protocol.c:569-605`
- **Description**: No upper bound on audio batch samples

### 47. Image Processing Memory Leak
- **File**: `src/client/capture.c:209-234`
- **Description**: Original image not destroyed on resize failure

---

## LOW SEVERITY BUGS

### 48. Potential Integer Overflow in next_power_of_two
- **File**: `lib/ringbuffer.c:25-38`
- **Description**: Edge case when n == SIZE_MAX

### 49. Unzeroed Frame Data in Clear
- **File**: `lib/ringbuffer.c:371-374`
- **Description**: Freed frame data not zeroed in buffer pool

### 50. API Inconsistency in Wrapper Returns
- **File**: `lib/network/packet.c:712-728`
- **Description**: Returns 0/-1 instead of asciichat_error_t

### 51. Inefficient Hash Table Cleanup
- **File**: `src/server/main.c:1141-1148`
- **Description**: Dead code in shutdown sequence

### 52. Unsafe strnlen in Display
- **File**: `src/client/display.c:211`
- **Description**: Hard-coded 1MB limit for frame length

### 53. Incomplete Error Path Cleanup
- **File**: `lib/audio/mixer.c:288`
- **Description**: Attempts to free unallocated mix_buffer

---

## Recommended Fix Priority

1. **Immediate** (CRITICAL security/crash bugs): #1, #2, #3, #4, #5, #6, #7, #8, #9, #10
2. **High Priority** (race conditions, data corruption): #11-#15, #16-#29
3. **Medium Priority** (code quality, edge cases): #30-#47
4. **Low Priority** (minor issues): #48-#53

---

## Notes

- All CRITICAL bugs should be fixed before any release
- Race conditions (#3, #15, #16, #17) require careful synchronization design
- Crypto bugs (#1, #21, #37) are high-priority for security
- Integer overflows (#2, #9, #11, #19, #24) can lead to memory corruption
- Platform-specific bugs (#12, #14) affect Windows and big-endian systems
