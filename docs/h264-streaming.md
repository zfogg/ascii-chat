# H.264 Streaming Architecture for ascii-chat

## Problem Statement

Current implementation sends raw RGB frames (~6.2MB at 1920×1080) over WebSocket, fragmenting into 1,500+ pieces per frame. This causes:
- WebSocket recv_queue to fill immediately (4096 slot limit)
- Fragment reassembly timeout (2 second) → client disconnect
- Complete frame loss and transmission failure

## Solution: H.264 Video Streaming

Instead of sending raw frames, clients encode frames as H.264 video (42KB per frame) before transmission, reducing fragmentation by **147×**.

### Architecture Changes

```
Current:
  Client: Capture RGB (6.2MB)
    ↓
  Send raw pixels → WebSocket fragmentation (1500+ pieces)
    ↓
  Server: Receive, reassemble, process RGB
    ↓
  ASCII conversion + audio mixing

New (H.264):
  Client: Capture RGB → Encode H.264 (42KB)
    ↓
  Send compressed video → Single or 2-3 WebSocket messages
    ↓
  Server: Receive, decode H.264 → RGB
    ↓
  ASCII conversion + audio mixing
```

## Why H.264?

**Compression:** 147× reduction (6.2MB → 42KB)
- At 1080p@60fps = 2.5 Mbps bitrate
- Fits on 10 Mbps connection with 4.7× overhead

**Latency:** 32-74ms end-to-end (with hardware acceleration)
- Encoding: 1-3ms (GPU-accelerated)
- Network: 20-50ms
- Decoding: 2-4ms (GPU-accelerated)
- Acceptable for real-time video chat

**Hardware Support:**
- NVIDIA: NVENC (native H.264 support)
- Intel: QuickSync
- AMD: VCE
- Mobile: iOS/Android native
- Browser: WebRTC already uses H.264

**Bitrate vs Quality:**
| Bitrate | Quality | CPU Load |
|---------|---------|----------|
| 1 Mbps | Fair (6-7/10) | Low |
| 2 Mbps | Good (7.5-8/10) | Low-Medium |
| 4 Mbps | Very Good (8.5-9/10) | Medium |

## Implementation Using FFmpeg

FFmpeg is **already integrated** into ascii-chat as a dependency. Current status:
- ✅ Decoders enabled (h264, hevc, vp8, vp9, etc.)
- ❌ Encoders **not** enabled

### Implementation Steps

1. **Enable H.264 encoder in FFmpeg build** (FFmpeg.cmake)
   - Add `--enable-encoder=libx264` to FFmpeg configure
   - Or use built-in `--enable-encoder=h264` (software fallback)

2. **Create `lib/media/ffmpeg_encoder.c`**
   - Mirror `lib/media/ffmpeg_decoder.c` pattern
   - Initialize encoder context
   - Encode raw frames to H.264 bitstream

3. **Integrate into client capture loop**
   - After webcam frame capture → Encode H.264
   - Send encoded bitstream instead of raw RGB
   - Send PACKET_TYPE_VIDEO_FRAME (new packet type) or IMAGE_FRAME with H.264 format

4. **Integrate into server receive**
   - On receiving H.264 data → Decode using ffmpeg_decoder
   - Feed decoded RGB to ASCII conversion pipeline
   - Audio mixing unchanged

5. **Add format field to packets**
   - Update IMAGE_FRAME/VIDEO_FRAME packet types
   - Add format enum: RGB24 (current), H264 (new), HEVC (future)

### Estimated Effort

| Task | Time | Notes |
|------|------|-------|
| Enable H.264 in CMake | 30 mins | 2-3 line change |
| Create ffmpeg_encoder module | 1.5-2 hrs | Copy decoder template, adapt |
| Client integration | 1-2 hrs | Modify capture loop |
| Server integration | 1 hr | Modify packet handling |
| Testing & debugging | 1-2 hrs | Verify compression, latency |
| **Total** | **5-7 hours** | Much faster than custom codec |

## Comparison: FFmpeg vs Custom Implementation

| Aspect | FFmpeg | Custom H.264 |
|--------|--------|--------------|
| External deps | Already have | Would add libx264/libx265 |
| Development time | 5-7 hours | 45-65 hours |
| Code size | ~500 lines | 2000+ lines |
| Hardware accel | Built-in support | Custom implementation needed |
| Maintainability | Proven, stable | Custom, fragile |
| Quality | Proven quality | Unknown tuning |
| Testing | Existing patterns | Must develop |

## Alternative Approaches Considered

### Compression at Transport Layer
- Enable WebSocket permessage-deflate
- **Status:** Disabled due to LWS buffer underflow bug (4.5.2)
- **Issue:** Compression overhead, doesn't solve fundamental 6.2MB problem
- **Verdict:** Band-aid, not solution

### Reduce Resolution
- 1920×1080 → 1280×720
- **Impact:** 2.8MB → still requires 700+ fragments
- **Verdict:** Insufficient alone

### JPEG Encoding
- Reduce to 200-500KB per frame
- **Issue:** Still 50-125 fragments per frame
- **Quality:** Lossy, visible compression artifacts
- **Verdict:** Insufficient vs H.264

## Future Enhancements

1. **Adaptive bitrate**
   - Monitor network conditions
   - Dynamically adjust encoding quality
   - FFmpeg supports this natively

2. **VP8/VP9 codec option**
   - Already enabled in FFmpeg
   - Software-only (slower encoding)
   - Better for royalty-free deployments

3. **HEVC/H.265**
   - Better compression (2× better than H.264)
   - Slower encoding (2-4×)
   - Future-proofing option

4. **Selective frame encoding**
   - I-frames only on connection/motion
   - P-frames for smooth streaming
   - Reduces bitrate further

## Testing Plan

### Unit Tests
```bash
# Test encoder creation/destruction
TEST(ffmpeg_encoder, create_destroy)

# Test frame encoding quality
TEST(ffmpeg_encoder, encode_frame_quality)

# Test bitrate control
TEST(ffmpeg_encoder, bitrate_settings)
```

### Integration Tests
```bash
# End-to-end with H.264
./build/bin/ascii-chat server --snapshot 0
./build/bin/ascii-chat client --snapshot 0 --use-h264

# Check fragmentation reduction
./build/bin/ascii-chat --log-level debug server --grep "fragment|packet_size"
```

### Manual Testing
```bash
# Mirror mode (local testing, no networking)
./build/bin/ascii-chat mirror --use-h264 --snapshot 5

# Server + client with H.264
./build/bin/ascii-chat server --use-h264
./build/bin/ascii-chat client --use-h264
```

## Configuration Options

Add to CLI options:
```
--h264                    Enable H.264 encoding (default: on)
--h264-bitrate <kbps>    Target bitrate (default: 2000)
--h264-preset <preset>   Encoding preset: fast/medium/slow (default: medium)
--h264-crf <0-51>        Quality (0=lossless, 51=worst, default: 28)
```

## Performance Expectations

### Before (Raw RGB)
- Frame size: 6.2 MB
- Fragments per frame: 1500+
- Queue fill time: 45ms (at 60fps)
- Result: Frame loss, disconnects

### After (H.264)
- Frame size: 42-50 KB
- Fragments per frame: 1-2
- Queue fill time: 28 seconds (at 60fps)
- Result: Smooth 60fps streaming

### Bandwidth Savings
- Monthly data: 1.06 GB (vs 1,656 GB raw)
- Network requirement: 2.5 Mbps (vs 2,984 Mbps)
- Reduction: **99.9%**

## References

- **FFmpeg Documentation:** https://ffmpeg.org/documentation.html
- **H.264 Specification:** ITU-T H.264
- **libx264 Options:** http://x264.nl/x264_32bit.exe -h
- **Current decoder code:** `lib/media/ffmpeg_decoder.c`
- **FFmpeg build config:** `cmake/dependencies/FFmpeg.cmake`

## Related Issues

- WebSocket fragmentation timeout causes disconnects
- recv_queue fills immediately with large frames
- Fragment reassembly fails after 2 seconds
- Terminal resizes during frame transmission cause desynchronization
