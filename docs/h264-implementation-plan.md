# H.264 Implementation Plan

## Overview

Reduce WebSocket fragmentation (1500+ → 1-2 fragments per frame) by encoding client frames as H.264 before transmission. Uses FFmpeg which is already a project dependency.

## Phase 1: FFmpeg Encoder Setup (30 mins)

### Step 1: Enable H.264 encoder in CMake

**File:** `cmake/dependencies/FFmpeg.cmake`

Add encoder configuration to FFmpeg build. Find line 149:

```cmake
# Current (decoders only):
--enable-decoder=h264,hevc,vp8,vp9,av1,mpeg4,png,gif,mjpeg,mp3,aac,flac,vorbis,opus,pcm_s16le

# Change to (add encoder):
--enable-encoder=libx264,mpeg4,png
--enable-decoder=h264,hevc,vp8,vp9,av1,mpeg4,png,gif,mjpeg,mp3,aac,flac,vorbis,opus,pcm_s16le
```

Alternative (if libx264 unavailable):
```cmake
--enable-encoder=h264,mpeg4,png
```

### Step 2: Rebuild FFmpeg

```bash
rm -rf ~/.cache/asciichat-deps/ffmpeg*
cmake --preset default -B build
cmake --build build
```

## Phase 2: Create ffmpeg_encoder Module (1.5-2 hours)

### Step 1: Create `lib/media/ffmpeg_encoder.h`

```c
#ifndef FFMPEG_ENCODER_H
#define FFMPEG_ENCODER_H

#include <stdint.h>
#include <stddef.h>

typedef struct ffmpeg_encoder_t ffmpeg_encoder_t;

/**
 * Create an H.264 encoder for the given frame dimensions
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param fps Frames per second
 * @param bitrate_kbps Target bitrate in kbps (0 = auto)
 * @return Encoder context or NULL on error
 */
ffmpeg_encoder_t *ffmpeg_encoder_create(uint16_t width, uint16_t height, uint16_t fps, uint32_t bitrate_kbps);

/**
 * Encode a single RGB frame
 * @param encoder Encoder context
 * @param rgb_data RGB24 pixel data (width * height * 3 bytes)
 * @param out_buffer Output buffer for H.264 bitstream
 * @param out_size Output size (updated with actual encoded size)
 * @param out_buffer_size Maximum output buffer size
 * @return 0 on success, -1 on error
 */
int ffmpeg_encoder_encode_frame(ffmpeg_encoder_t *encoder, const uint8_t *rgb_data,
                                 uint8_t *out_buffer, size_t *out_size, size_t out_buffer_size);

/**
 * Flush encoder (get final NAL units)
 * @param encoder Encoder context
 * @param out_buffer Output buffer for H.264 bitstream
 * @param out_size Output size (updated with actual encoded size)
 * @param out_buffer_size Maximum output buffer size
 * @return 0 on success, -1 on error
 */
int ffmpeg_encoder_flush(ffmpeg_encoder_t *encoder, uint8_t *out_buffer,
                         size_t *out_size, size_t out_buffer_size);

/**
 * Destroy encoder and free resources
 */
void ffmpeg_encoder_destroy(ffmpeg_encoder_t *encoder);

#endif // FFMPEG_ENCODER_H
```

### Step 2: Create `lib/media/ffmpeg_encoder.c`

Follow the pattern of `ffmpeg_decoder.c`:
1. Copy ffmpeg_decoder.c structure
2. Replace AVCodecContext decoder with encoder
3. Implement encoding loop instead of decoding loop
4. Handle NAL unit output (H.264 bitstream)

**Key differences from decoder:**
- Use `avcodec_find_encoder_by_name("libx264")` instead of decoder
- Use `avcodec_send_frame()` to send raw frames
- Use `av_packet_alloc()` to receive encoded packets
- Output is bitstream NAL units, not decoded frames

### Step 3: Update CMakeLists.txt

Add to source file list in `src/CMakeLists.txt` or `lib/media/CMakeLists.txt`:

```cmake
if(FFMPEG_FOUND)
    target_sources(ascii-chat-lib PRIVATE
        lib/media/ffmpeg_decoder.c
        lib/media/ffmpeg_encoder.c  # Add this
    )
endif()
```

## Phase 3: Client Integration (1-2 hours)

### Step 1: Update packet format

**File:** `include/ascii-chat/network/packet.h`

Add format enum to IMAGE_FRAME:
```c
#define PIXEL_FORMAT_RGB24   0
#define PIXEL_FORMAT_H264    1  // Add this
#define PIXEL_FORMAT_HEVC    2  // Future
```

### Step 2: Modify client capture loop

**File:** `src/client/client.c` or equivalent capture handler

```c
// Before sending frame:
if (use_h264_encoding) {
    ffmpeg_encoder_t *encoder = get_encoder();
    uint8_t *encoded = malloc(512 * 1024);  // 512KB buffer
    size_t encoded_size = 0;

    ffmpeg_encoder_encode_frame(encoder, rgb_data, encoded, &encoded_size, 512 * 1024);

    // Send encoded H.264 bitstream instead of raw RGB
    send_image_frame_packet(sock, encoded, width, height, PIXEL_FORMAT_H264);
    free(encoded);
} else {
    // Current path: raw RGB
    send_image_frame_packet(sock, rgb_data, width, height, PIXEL_FORMAT_RGB24);
}
```

### Step 3: Add CLI option

**File:** `lib/options/options.c` or similar

```c
// Add to options struct:
bool use_h264_encoding;
uint32_t h264_bitrate_kbps;
const char *h264_preset;  // "fast", "medium", "slow"
```

Parse from CLI:
```
--h264                    Enable H.264 encoding
--h264-bitrate <kbps>    Target bitrate (default: 2000)
--h264-preset <preset>   Encoding preset (default: medium)
```

## Phase 4: Server Integration (1 hour)

### Step 1: Update packet receiving

**File:** `src/server/packet_handler.c` or network receive handler

```c
// When receiving IMAGE_FRAME:
if (packet->format == PIXEL_FORMAT_H264) {
    // Decode H.264 to RGB
    ffmpeg_decoder_t *decoder = get_decoder_for_client(client_id);
    uint8_t *rgb_data = malloc(width * height * 3);

    ffmpeg_decoder_decode_h264(decoder, packet->data, packet->size, rgb_data);

    // Feed RGB to ASCII conversion (existing pipeline)
    process_frame_for_ascii(rgb_data, width, height);
    free(rgb_data);
} else if (packet->format == PIXEL_FORMAT_RGB24) {
    // Current path: raw RGB
    process_frame_for_ascii(packet->data, width, height);
}
```

### Step 2: Manage decoders per client

Add to `client_info_t`:
```c
struct client_info_t {
    // ... existing fields ...
    ffmpeg_decoder_t *h264_decoder;  // Add this
};
```

Initialize/destroy with client lifecycle.

## Phase 5: Testing (1-2 hours)

### Unit Tests

```bash
# Test encoder creation
./build/bin/test_media_encoder --filter "*create*"

# Test compression ratio
./build/bin/test_media_encoder --filter "*compression*"
```

### Integration Tests

```bash
# Client-only test (mirror mode)
./build/bin/ascii-chat mirror --h264 --snapshot 5

# End-to-end server + client
./build/bin/ascii-chat server --h264 &
sleep 1
./build/bin/ascii-chat client --h264 --snapshot 5
pkill -f "ascii-chat server"
```

### Manual Verification

```bash
# Check fragmentation reduction
./build/bin/ascii-chat --log-level debug server --grep "fragment|packet_size"

# Verify encoding quality (visually)
./build/bin/ascii-chat server --h264
./build/bin/ascii-chat client --h264

# Compare bandwidth with/without H.264
# Use: iftop, nethogs, or tcpdump
tcpdump -i lo port 27224
```

## Timeline

| Phase | Task | Time |
|-------|------|------|
| 1 | FFmpeg encoder in CMake | 30 mins |
| 2 | Create ffmpeg_encoder module | 1.5-2 hrs |
| 3 | Client integration | 1-2 hrs |
| 4 | Server integration | 1 hr |
| 5 | Testing & debugging | 1-2 hrs |
| **Total** | | **5-7 hours** |

## Success Criteria

- ✅ Frame size: 6.2MB → <50KB (147× reduction)
- ✅ Fragments: 1500+ → 1-2 per frame
- ✅ No frame loss at 60fps
- ✅ Encoding latency <50ms (with GPU accel)
- ✅ Server CPU usage reasonable
- ✅ End-to-end latency <150ms
- ✅ Visual quality acceptable (no visible artifacts)

## Rollback Plan

If H.264 causes issues:
1. Add `--disable-h264` flag
2. Fall back to RGB transmission
3. Keep both codepaths (dynamic selection)
4. Default to whichever works best

## Future Work

1. **Adaptive bitrate** - Monitor network, adjust quality dynamically
2. **VP8/VP9 support** - Already enabled in FFmpeg
3. **HEVC/H.265** - Better compression for future
4. **SVC (Scalable Video Coding)** - Multiple quality layers
5. **Key frame insertion** - Force I-frames on scene changes
6. **Bandwidth profiling** - Measure actual vs theoretical
