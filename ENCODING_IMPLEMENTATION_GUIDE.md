# H.264 Implementation Guide for ascii-chat

This guide provides concrete implementation details for integrating H.264 video encoding into ascii-chat, replacing or supplementing the current RGB frame transmission.

---

## Current Architecture (Status Quo)

### Frame Transmission Pipeline

```
Webcam Input (1920x1080 @ 60fps)
    ↓
RGB Conversion (libyuv or OpenCV)
    ↓
Raw RGB Buffer (6.22 MB/frame)
    ↓
zstd Compression (network layer)
    ↓
Packet Fragmentation (5 MB max)
    ↓
Encryption (libsodium)
    ↓
Network Transmission
```

**Problem:** Raw RGB at 60fps = 373 MB/sec network requirement

### Current Implementation Files

- **Video frame buffers:** `/home/user/ascii-chat/include/ascii-chat/video/video_frame.h`
  - Pre-allocated 2MB × 2 frame buffers
  - Double-buffering with atomic swaps
  - Zero-copy frame transfer

- **Compression:** `/home/user/ascii-chat/lib/network/compression.c`
  - Uses zstd with 1KB minimum threshold
  - 20% compression ratio requirement (0.8x)
  - Configurable compression levels 1-9

- **Packet structure:** `/home/user/ascii-chat/include/ascii-chat/network/packet.h`
  - PACKET_TYPE_IMAGE_FRAME (3001) for raw RGB
  - Max packet size: 5 MB
  - CRC32 validation + magic number

---

## Proposed Architecture (H.264 Integration)

### New Frame Transmission Pipeline

```
Webcam Input (1920x1080 @ 60fps)
    ↓
YUV Conversion (libyuv)
    ↓
H.264 Encoder (libx264 or NVIDIA NVENC)
    ├─ Input: YUV 4:2:0 (2.97 MB/frame)
    ├─ Output: 21-83 KB/frame (depending on bitrate)
    ├─ GOP structure: IDRP{B}P{B}P (every 120 frames)
    └─ Latency: 1-3 ms (HW) or 30-50 ms (SW)
    ↓
Encoded Packet Queue
    ├─ NAL units (SPS/PPS/IDR/P/B)
    ├─ Keyframe bundling (send SPS/PPS with IDR)
    └─ Temporal layer extraction (optional, for low-delay)
    ↓
Packet Fragmentation (if >5 MB = rare)
    ↓
Encryption (libsodium) - optional, already encrypted
    ↓
Network Transmission (2-4 Mbps)
```

**Benefit:** 2 Mbps sustained = 4-5x overhead on 10 Mbps connection

---

## Step 1: Add Encoder Abstraction

Create `/home/user/ascii-chat/include/ascii-chat/video/encoder.h`:

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../asciichat_errno.h"
#include "video_frame.h"

/**
 * @brief Video codec enumeration
 */
typedef enum {
  VIDEO_CODEC_H264,   ///< H.264/AVC (recommended)
  VIDEO_CODEC_HEVC,   ///< H.265/HEVC (future)
  VIDEO_CODEC_VP8,    ///< VP8 (fallback)
  VIDEO_CODEC_VP9,    ///< VP9 (future)
} video_codec_t;

/**
 * @brief Video encoder preset
 * Speed vs quality tradeoff
 */
typedef enum {
  ENCODER_PRESET_ULTRAFAST,  ///< Fastest (worse quality)
  ENCODER_PRESET_FAST,        ///< Real-time acceptable (H.264 default)
  ENCODER_PRESET_MEDIUM,      ///< Balanced
  ENCODER_PRESET_SLOW,        ///< Slower (better quality)
} video_encoder_preset_t;

/**
 * @brief Encoded frame data
 * Contains compressed video data
 */
typedef struct {
  uint8_t *data;         ///< Encoded NAL unit data
  size_t size;           ///< Size in bytes
  uint64_t timestamp_ns; ///< Frame timestamp (nanoseconds)
  bool is_keyframe;      ///< True if this is an IDR frame (I-frame)
  uint32_t frame_number; ///< Frame sequence number for reordering
} encoded_frame_t;

/**
 * @brief Video encoder context (opaque to caller)
 */
typedef struct video_encoder video_encoder_t;

/**
 * @brief Video encoder configuration
 */
typedef struct {
  video_codec_t codec;
  uint32_t bitrate_kbps;          ///< Target bitrate (1000-8000)
  uint32_t fps;                   ///< Frame rate (24-60)
  uint32_t width;                 ///< Frame width (must match input)
  uint32_t height;                ///< Frame height (must match input)
  video_encoder_preset_t preset;  ///< Speed/quality tradeoff
  uint32_t gop_size;              ///< Keyframe interval (120 for 2s @ 60fps)
  bool adaptive_bitrate;          ///< Allow bitrate changes mid-stream
} video_encoder_config_t;

/**
 * @brief Create video encoder
 * @param config Encoder configuration
 * @return Encoder context, or NULL on failure
 *
 * Attempts to use hardware acceleration (NVENC) if available,
 * falls back to software (libx264) if not.
 */
asciichat_error_t video_encoder_create(const video_encoder_config_t *config,
                                       video_encoder_t **encoder);

/**
 * @brief Destroy encoder and free resources
 */
void video_encoder_destroy(video_encoder_t *encoder);

/**
 * @brief Encode a frame
 * @param encoder Encoder instance
 * @param frame Input frame (RGB or YUV, depends on encoder)
 * @param output Output encoded frame (caller must free with SAFE_FREE)
 * @return ASCIICHAT_OK on success
 *
 * Performs encoding synchronously. For real-time performance,
 * hardware acceleration strongly recommended.
 */
asciichat_error_t video_encoder_encode(video_encoder_t *encoder,
                                       const video_frame_t *frame,
                                       encoded_frame_t **output);

/**
 * @brief Update bitrate (for adaptive streaming)
 * @param encoder Encoder instance
 * @param bitrate_kbps New bitrate in kbps
 * @return ASCIICHAT_OK on success
 *
 * Changes take effect on next keyframe for smooth quality transition.
 */
asciichat_error_t video_encoder_set_bitrate(video_encoder_t *encoder,
                                            uint32_t bitrate_kbps);

/**
 * @brief Force next frame to be keyframe
 * @param encoder Encoder instance
 * @return ASCIICHAT_OK on success
 *
 * Used for scene cuts or stream restart.
 */
asciichat_error_t video_encoder_force_keyframe(video_encoder_t *encoder);

/**
 * @brief Get encoder statistics
 * @param encoder Encoder instance
 * @param [out] avg_encode_time_ms Average encoding time in milliseconds
 * @param [out] frames_encoded Total frames encoded
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t video_encoder_get_stats(video_encoder_t *encoder,
                                          uint32_t *avg_encode_time_ms,
                                          uint64_t *frames_encoded);
```

---

## Step 2: Implement libx264 Backend

Create `/home/user/ascii-chat/lib/video/encoder_libx264.c`:

```c
#include <x264.h>
#include <ascii-chat/video/encoder.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/time.h>

typedef struct {
  x264_t *encoder;
  x264_param_t param;

  // Statistics
  uint64_t frames_encoded;
  uint64_t total_encode_time_ns;

  // Frame buffering for B-frames
  x264_nal_t *nal;
  int nal_count;
} libx264_encoder_t;

// Preset mapping: ENCODER_PRESET_* → x264 preset string
static const char *preset_strings[] = {
  "ultrafast",  // ENCODER_PRESET_ULTRAFAST
  "fast",       // ENCODER_PRESET_FAST (default for real-time)
  "medium",     // ENCODER_PRESET_MEDIUM
  "slow",       // ENCODER_PRESET_SLOW
};

asciichat_error_t libx264_create(const video_encoder_config_t *config,
                                 libx264_encoder_t **encoder_out) {
  libx264_encoder_t *enc = SAFE_MALLOC(1, libx264_encoder_t *);
  if (!enc) return SET_ERRNO(ERROR_MEMORY, "Failed to allocate encoder");

  // Initialize x264 parameters
  x264_param_default_preset(&enc->param,
                             preset_strings[config->preset],
                             "zerolatency");  // Profile for real-time

  // Configure parameters
  enc->param.i_csp = X264_CSP_I420;  // Input: YUV 4:2:0
  enc->param.i_width = config->width;
  enc->param.i_height = config->height;
  enc->param.i_fps_num = config->fps;
  enc->param.i_fps_den = 1;
  enc->param.i_keyint_max = config->gop_size;
  enc->param.i_keyint_min = config->gop_size;  // Fixed GOP
  enc->param.rc.i_bitrate = config->bitrate_kbps;
  enc->param.rc.i_rc_method = X264_RC_ABR;  // Average bitrate
  enc->param.b_repeat_headers = 1;  // Repeat SPS/PPS before each IDR
  enc->param.i_threads = 4;  // Multi-threaded encoding

  // Apply profile constraints
  x264_param_apply_profile(&enc->param, "high");

  // Open encoder
  enc->encoder = x264_encoder_open(&enc->param);
  if (!enc->encoder) {
    SAFE_FREE(enc);
    return SET_ERRNO(ERROR_GENERAL, "Failed to open x264 encoder");
  }

  enc->frames_encoded = 0;
  enc->total_encode_time_ns = 0;
  *encoder_out = enc;

  return ASCIICHAT_OK;
}

asciichat_error_t libx264_encode(libx264_encoder_t *enc,
                                 const video_frame_t *frame,
                                 encoded_frame_t **output_out) {
  // This is simplified - real implementation needs YUV conversion
  // Assuming frame->data is already YUV 4:2:0 (Y plane + UV planes)

  uint64_t start_ns = get_time_ns();

  x264_picture_t pic_in;
  x264_picture_alloc(&pic_in, enc->param.i_csp,
                     enc->param.i_width, enc->param.i_height);

  // Copy frame data (simplified - needs proper YUV plane handling)
  size_t y_size = enc->param.i_width * enc->param.i_height;
  SAFE_MEMCPY(pic_in.img.plane[0], y_size,
              frame->data, frame->size);

  pic_in.i_pts = frame->sequence_number;

  // Encode
  x264_picture_t pic_out;
  int nal_count;
  int ret = x264_encoder_encode(enc->encoder, &enc->nal, &nal_count,
                                &pic_in, &pic_out);

  x264_picture_clean(&pic_in);

  if (ret < 0) {
    return SET_ERRNO(ERROR_GENERAL, "x264 encoding failed");
  }

  // Calculate output size
  size_t out_size = 0;
  for (int i = 0; i < nal_count; i++) {
    out_size += enc->nal[i].i_payload;
  }

  // Allocate output
  encoded_frame_t *output = SAFE_MALLOC(1, encoded_frame_t *);
  if (!output) return SET_ERRNO(ERROR_MEMORY, "Failed to allocate output");

  output->data = SAFE_MALLOC(out_size, uint8_t *);
  if (!output->data) {
    SAFE_FREE(output);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate output data");
  }

  // Copy NAL units to output
  size_t offset = 0;
  for (int i = 0; i < nal_count; i++) {
    SAFE_MEMCPY(output->data + offset, out_size - offset,
                enc->nal[i].p_payload, enc->nal[i].i_payload);
    offset += enc->nal[i].i_payload;
  }

  output->size = out_size;
  output->timestamp_ns = frame->capture_timestamp_ns;
  output->frame_number = frame->sequence_number;
  output->is_keyframe = (enc->nal[0].i_type == NAL_SLICE_IDR);

  // Update statistics
  enc->frames_encoded++;
  uint64_t encode_time_ns = get_time_ns() - start_ns;
  enc->total_encode_time_ns += encode_time_ns;

  *output_out = output;
  return ASCIICHAT_OK;
}
```

---

## Step 3: Update Packet Protocol

Add to `/home/user/ascii-chat/include/ascii-chat/network/packet.h`:

```c
/** @brief H.264 encoded video frame (NAL units) */
#define PACKET_TYPE_VIDEO_H264 3002

/** @brief HEVC encoded video frame (NAL units) */
#define PACKET_TYPE_VIDEO_HEVC 3003

/** @brief Video stream metadata (codec, bitrate, resolution) */
#define PACKET_TYPE_VIDEO_META 3004
```

Add NAL unit wrapper structure:

```c
/**
 * @brief H.264 NAL unit packet wrapper
 * Encapsulates one or more NAL units from H.264 encoder
 */
typedef struct {
  uint32_t frame_number;      ///< Sequence number for reordering
  uint64_t timestamp_us;      ///< Microseconds since stream start
  uint16_t nal_count;         ///< Number of NAL units in this packet
  bool is_keyframe;           ///< True if contains IDR frame

  // Variable-length NAL units follow (size = payload_size - header_size)
  // Each NAL unit: [4-byte length][NAL data]
} h264_frame_packet_t;
```

---

## Step 4: Integration Point - Server Stream

Modify `/home/user/ascii-chat/src/server/stream.c`:

```c
// Add to stream context
typedef struct {
  video_encoder_t *encoder;
  video_encoder_config_t encoder_config;

  // Statistics
  uint64_t frames_encoded;
  uint64_t bytes_saved;  // Compression savings vs raw RGB
} video_stream_t;

// During server initialization
asciichat_error_t init_video_stream(video_stream_t *stream,
                                    uint32_t width, uint32_t height) {
  video_encoder_config_t config = {
    .codec = VIDEO_CODEC_H264,
    .bitrate_kbps = 2000,        // Start at 2 Mbps
    .fps = 60,
    .width = width,
    .height = height,
    .preset = ENCODER_PRESET_FAST,
    .gop_size = 120,             // 2 seconds @ 60fps
    .adaptive_bitrate = true,
  };

  return video_encoder_create(&config, &stream->encoder);
}

// In main video send loop
asciichat_error_t send_video_frame(video_stream_t *stream,
                                   const video_frame_t *frame) {
  // Encode frame
  encoded_frame_t *encoded = NULL;
  asciichat_error_t result = video_encoder_encode(stream->encoder,
                                                   frame, &encoded);
  if (result != ASCIICHAT_OK) {
    log_error("H.264 encoding failed: %s",
              asciichat_error_message(result));
    return result;
  }

  log_debug("H264: Frame %llu encoded to %zu bytes (%.1f%% of raw, keyframe=%d)",
            frame->sequence_number,
            encoded->size,
            100.0f * encoded->size / (frame->width * frame->height * 3),
            encoded->is_keyframe);

  // Send as network packet
  result = send_h264_frame_packet(client, encoded);

  stream->frames_encoded++;
  stream->bytes_saved += (frame->width * frame->height * 3) - encoded->size;

  SAFE_FREE(encoded->data);
  SAFE_FREE(encoded);

  return result;
}
```

---

## Step 5: Client Decoding

For clients, add decoder support in `/home/user/ascii-chat/lib/video/decoder.h`:

```c
/**
 * @brief Video decoder context (opaque)
 */
typedef struct video_decoder video_decoder_t;

/**
 * @brief Create H.264 decoder
 * @param width Frame width
 * @param height Frame height
 * @return Decoder context, or NULL on failure
 *
 * Attempts hardware decoding first (NVIDIA NVDEC, AMD UVD, Intel VAAPI),
 * falls back to libavcodec if unavailable.
 */
video_decoder_t *video_decoder_create(uint32_t width, uint32_t height);

/**
 * @brief Decode H.264 NAL units to RGB frame
 * @param decoder Decoder context
 * @param nal_data NAL unit data
 * @param nal_size Size of NAL data
 * @param [out] frame Output RGB frame (caller must allocate)
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t video_decoder_decode(video_decoder_t *decoder,
                                       const uint8_t *nal_data,
                                       size_t nal_size,
                                       video_frame_t *frame);

void video_decoder_destroy(video_decoder_t *decoder);
```

---

## Step 6: Network Bandwidth Monitoring

Add adaptive bitrate logic:

```c
/**
 * @brief Monitor bandwidth and adjust bitrate
 */
typedef struct {
  uint64_t bytes_sent;
  uint64_t time_elapsed_ns;
  float estimated_bitrate_kbps;
  uint32_t packet_loss_percent;
} bandwidth_monitor_t;

void update_adaptive_bitrate(video_stream_t *stream,
                             const bandwidth_monitor_t *bw) {
  // If bandwidth constrained, reduce bitrate
  if (bw->estimated_bitrate_kbps < 2000) {
    uint32_t new_bitrate = (uint32_t)(bw->estimated_bitrate_kbps * 0.8f);
    if (new_bitrate < 500) new_bitrate = 500;

    video_encoder_set_bitrate(stream->encoder, new_bitrate);
    log_info("Bandwidth reduced to %.0f Kbps, H264 bitrate set to %u Kbps",
             bw->estimated_bitrate_kbps, new_bitrate);
  }

  // If packet loss, request keyframe for recovery
  if (bw->packet_loss_percent > 5) {
    video_encoder_force_keyframe(stream->encoder);
    log_warn("Packet loss %.1f%%, forcing H264 keyframe",
             bw->packet_loss_percent);
  }
}
```

---

## Step 7: CMake Integration

Add to `CMakeLists.txt`:

```cmake
# H.264 encoder support
find_package(x264 REQUIRED)
find_package(yuv REQUIRED)  # For YUV conversion

add_library(video_encoder
  lib/video/encoder.c
  lib/video/encoder_libx264.c
  lib/video/encoder_nvenc.c  # Optional NVIDIA support
)

target_link_libraries(video_encoder
  PUBLIC x264::x264 libyuv
)

# Optional: NVIDIA NVENC support
if(CUDA_FOUND)
  target_sources(video_encoder PRIVATE lib/video/encoder_nvenc.c)
  target_link_libraries(video_encoder PUBLIC ${CUDA_LIBRARIES})
  target_compile_definitions(video_encoder PUBLIC HAVE_NVENC)
endif()
```

---

## Step 8: Configuration Options

Add to `lib/options/registry/video.c`:

```c
// Video codec selection
ADD_ENUM_OPTION(
  video_codec,
  'c', "video-codec",
  "Video codec for streaming (h264, hevc, vp8, vp9)",
  VIDEO_CODEC_H264,
  {"h264", VIDEO_CODEC_H264},
  {"hevc", VIDEO_CODEC_HEVC},
  {"vp8", VIDEO_CODEC_VP8},
)

// Target bitrate (kbps)
ADD_UINT32_OPTION(
  video_bitrate,
  'b', "video-bitrate",
  "Target video bitrate in kbps (500-8000, default: 2000)",
  2000,
  500, 8000
)

// Encoding preset
ADD_ENUM_OPTION(
  video_preset,
  'p', "video-preset",
  "Encoding speed preset (ultrafast, fast, medium, slow)",
  ENCODER_PRESET_FAST,
  {"ultrafast", ENCODER_PRESET_ULTRAFAST},
  {"fast", ENCODER_PRESET_FAST},
  {"medium", ENCODER_PRESET_MEDIUM},
  {"slow", ENCODER_PRESET_SLOW},
)

// Keyframe interval
ADD_UINT32_OPTION(
  video_keyframe_interval,
  'k', "video-keyframe-interval",
  "Keyframe interval in frames (60-300, default: 120)",
  120,
  60, 300
)
```

---

## Testing Strategy

### Unit Tests

```bash
# Test encoder creation and basic encoding
ctest --test-dir build -R "video_encoder" --output-on-failure

# Benchmark H.264 vs raw RGB
./build/bin/ascii-chat mirror \
  --file test.mp4 \
  --video-codec h264 \
  --video-bitrate 2000 \
  --snapshot \
  --log-level debug \
  --grep "H264|compression|bytes"
```

### Integration Tests

```bash
# Server with H.264 encoding
./build/bin/ascii-chat server --video-codec h264 --video-bitrate 2000

# Client connecting to server
./build/bin/ascii-chat client

# Verify frame sizes and latency
# Should see: ~42 KB/frame @ 2 Mbps instead of 6.2 MB/frame
```

### Performance Benchmarks

```c
// In tests/unit/video_encoder_benchmark.c

void benchmark_h264_vs_raw() {
  // Encode 1000 frames, measure throughput

  // Expected results (on i7-9700K):
  // H.264 libx264 "fast": 30-50 ms/frame (single-threaded)
  //                       3-5 ms/frame (multi-threaded)
  // H.264 NVENC RTX 2080:  1-2 ms/frame
  // Raw RGB zstd:         10-20 ms/frame
}
```

---

## Migration Path

### Phase 1: Proof of Concept (Week 1-2)
- [ ] Implement encoder abstraction
- [ ] Add libx264 backend (software only)
- [ ] Test frame size reduction (42 KB vs 6.2 MB)
- [ ] Verify latency acceptable

### Phase 2: Integration (Week 3-4)
- [ ] Update packet protocol
- [ ] Add to server streaming pipeline
- [ ] Add client decoding (FFmpeg libavcodec)
- [ ] Test end-to-end

### Phase 3: Optimization (Week 5-6)
- [ ] Add NVIDIA NVENC backend
- [ ] Implement adaptive bitrate
- [ ] Performance tuning
- [ ] Hardware compatibility testing

### Phase 4: Polish (Week 7+)
- [ ] CLI options for codec/bitrate selection
- [ ] Documentation
- [ ] Cross-platform testing

---

## Backward Compatibility

H.264 can coexist with current RGB streaming:

```c
// In server: Auto-detect client capabilities
if (client_supports_h264) {
  use_h264_encoding();  // New path
} else {
  use_rgb_encoding();   // Old path (fallback)
}
```

---

## References

- **libx264 API:** https://github.com/mirror/x264/blob/master/x264.h
- **H.264 NAL units:** ITU-T H.264 §7.3.2.2
- **WebRTC H.264 profile:** RFC 7741 Appendix B
- **Real-time encoding:** https://trac.ffmpeg.org/wiki/Encode/H.264#Preset
- **NVIDIA NVENC:** https://developer.nvidia.com/nvidia-video-codec-sdk
