# Video Encoding Options Analysis for Real-Time Streaming

## Executive Summary

For ascii-chat's 1080p@60fps real-time streaming with minimal network overhead, **H.264/AVC** is the best choice despite being more complex than alternatives. It achieves 50-100x compression over raw RGB while maintaining sub-50ms latency and offering universal hardware acceleration support.

---

## Test Case: 1080p@60fps Baseline

**Raw RGB Frame:** 1920 × 1080 × 3 bytes/pixel = **6.22 MB/frame**
- At 60 fps: **373 MB/sec** (2.98 Gbps) - completely infeasible

**Target:** Reduce to ~100-500 KB/frame for typical home internet (10 Mbps upstream)

---

## Detailed Comparison

### 1. JPEG (Lossy Image Compression)

**Compression Method:** Discrete Cosine Transform (DCT) + Huffman encoding
**Latency Profile:** Moderate

#### Frame Size Reduction
| Quality | File Size | Ratio | Notes |
|---------|-----------|-------|-------|
| Q95 | ~800 KB | 1:8 | High quality, visible banding |
| Q85 | ~400-500 KB | 1:12-15 | Balanced quality/size |
| Q75 | ~250-300 KB | 1:20-25 | Noticeable compression artifacts |
| Q65 | ~150-180 KB | 1:35-40 | Aggressive compression |

**Encoding/Decoding Latency:**
- Software encoding: 50-100 ms (on modern CPU)
- Software decoding: 20-40 ms
- Hardware support: Limited (NVIDIA NVJPEG: 3-5 ms encode/decode on RTX GPUs, not widely available)

**Quality at Low Bitrates:**
- JPEG excels at moderate compression (Q75-85)
- Suffers from blocking artifacts and color banding at low bitrates (Q60-70)
- DCT transforms struggle with sharp edges and text
- Not ideal for ASCII-heavy frames with text

**Hardware Acceleration:**
- CPU: Universal software support (libjpeg-turbo achieves 4-6 Gbps throughput on x86)
- GPU: NVIDIA NVJPEG (RTX series), AMD hardware limited, Intel UHD/Iris minimal
- Mobile: ARM NEON support in libjpeg-turbo

**Browser/FFmpeg Support:**
- FFmpeg: Excellent (native JPEG encoder/decoder)
- Browser: JPEG image format (not video streaming native - must send individual frames)
- WebRTC: Not used (only H.264/VP8/VP9)

**Best Use Cases:**
- Low-latency single frame capture (snapshot mode with `--snapshot`)
- Fallback when video codecs unavailable
- Extremely lightweight embedded systems

**Why Not for Streaming:**
- No inter-frame compression (each frame independent = 100% overhead)
- Blocking artifacts at low bitrates degrade perceived quality
- Poor temporal coherence (visual artifacts when frames differ slightly)
- No reference frame reuse between frames

---

### 2. H.264/AVC (Video Codec, Widely Supported)

**Compression Method:** Motion compensation + Transform coding + Entropy encoding
**Latency Profile:** Low

#### Frame Size Reduction
| Bitrate | Use Case | Frame Size (60fps) | Notes |
|---------|----------|-------------------|-------|
| 8 Mbps | High quality, studio | ~167 KB | Keyframes larger (~500 KB) |
| 4 Mbps | Good quality, home | ~83 KB | 1:75 compression |
| 2 Mbps | Acceptable quality | ~42 KB | 1:150 compression |
| 1 Mbps | Low bandwidth | ~21 KB | 1:300 compression |
| 500 Kbps | Emergency mode | ~10 KB | 1:600 compression |

**Keyframe Overhead:** Every 2 seconds (120 frames @ 60fps)
- Keyframe: ~400-600 KB
- P-frames: ~5-20 KB each
- B-frames: ~3-10 KB each

**Typical Network Usage (1080p60, 2 Mbps constant):**
- 4 Mbps bitrate = 500 KB/sec to network
- With 5 second GOP: Average ~84 KB/frame
- Burst traffic during keyframes: 400+ KB every 5 seconds

**Encoding/Decoding Latency:**
- Software encoding (x264 preset=fast): 30-50 ms per frame
- Software decoding: 15-30 ms
- Hardware encoding (NVIDIA NVENC): 1-3 ms per frame
- Hardware decoding (NVIDIA NVDEC): 2-4 ms

**Quality at Low Bitrates:**
- H.264 excellent at 500 Kbps-2 Mbps (1:12-1:24 compression)
- Graceful degradation: Reduces resolution/fps rather than blocking artifacts
- Temporal interpolation handles motion smoothly
- Motion compensation recovers "free" compression from scene static regions

**Hardware Acceleration:**
- **Encoding:** NVIDIA NVENC (Kepler+), AMD VCE, Intel Quick Sync (Sandy Bridge+)
- **Decoding:** Nearly universal (desktop GPU, mobile SoC, Raspberry Pi)
  - NVIDIA NVDEC (Kepler+)
  - AMD UVD
  - Intel VAAPI (HS+)
  - ARM MALI
  - Qualcomm Adreno
  - Apple VideoToolbox (all Macs)
  - Android MediaCodec

**Browser/FFmpeg Support:**
- FFmpeg: Excellent (libx264 encoder, libx264/hardware decoders)
- Browser: Native WebRTC video codec (mandatory for WebRTC)
- WebRTC: Universal support, mandatory negotiation

**Best Use Cases:**
- **Primary choice for streaming** (video chat, remote desktop, screen sharing)
- Real-time communication where latency matters
- Mixed content (video + ASCII overlays)
- Mobile/embedded (battery efficient via hardware decode)
- Legacy systems compatibility

**Patent/Licensing:**
- Patents expired or expiring in many jurisdictions (US: 2026-2027)
- MPEG LA pools available ($0.20-$2 per unit for consumer devices)

**Why It's Best for ascii-chat:**
1. **Hardware acceleration everywhere** - enables real-time encode/decode
2. **Proven streaming codec** - designed for this exact use case
3. **Latency tuning** - can reduce frame buffering for <50ms RTT
4. **Adaptive bitrate ready** - can scale from 500 Kbps to 10 Mbps
5. **Standardized in WebRTC** - compatible with browser-based clients
6. **Ecosystem maturity** - battle-tested in production systems

---

### 3. VP8/VP9 (Royalty-Free Video Codecs)

**Compression Method:** Similar to H.264 (motion compensation + DCT) but optimized differently
**Latency Profile:** Moderate-High

#### Frame Size Reduction

**VP8 (older, moderate efficiency):**
| Bitrate | Frame Size | Compression Ratio |
|---------|-----------|-------------------|
| 2 Mbps | ~50 KB | 1:120 |
| 1 Mbps | ~25 KB | 1:240 |

**VP9 (newer, better efficiency):**
| Bitrate | Frame Size | Compression Ratio |
|---------|-----------|-------------------|
| 1.5 Mbps | ~31 KB | 1:200 |
| 800 Kbps | ~17 KB | 1:360 |
| 400 Kbps | ~8.5 KB | 1:730 |

**Encoding/Decoding Latency:**
- VP8 software encoding (libvpx): 80-150 ms per frame (SLOW)
- VP9 software encoding (libvpx-vp9): 150-300 ms per frame (VERY SLOW)
- VP8 hardware: Limited (some ARM, no desktop)
- VP9 hardware: Extremely limited (only modern ARM SoCs)
- Decoding: 20-50 ms (software), 5-10 ms (hardware, rare)

**Quality at Low Bitrates:**
- VP9 superior to H.264 at very low bitrates (<500 Kbps)
- Better perceptual quality with better psychovisual modeling
- Less aggressive quality reduction
- HOWEVER: Encoding time makes real-time streaming difficult

**Hardware Acceleration:**
- **Encoding:** Almost none for desktop (VP9 limited to ARM SoCs)
- **Decoding:** Modern phones (Android 5+), some ARM boards, rare desktop support
- No NVIDIA/AMD/Intel encoding support (desktop)

**Browser/FFmpeg Support:**
- FFmpeg: Excellent codec support
- Browser: Chrome, Firefox, Edge (VP8/VP9 mandatory WebRTC)
- WebRTC: Mandatory in open-source implementations, optional for H.264

**Best Use Cases:**
- YouTube/WebM (async, non-real-time encoding acceptable)
- Patent-concerned organizations (royalty-free)
- Mobile-only applications

**Why Not for Real-Time Streaming:**
1. **Encoding time unacceptable** - 80+ ms CPU overhead per frame
2. **Limited hardware acceleration** - desktop systems can't use GPU
3. **Latency adds up** - 80ms encode + 50ms network + 20ms decode = 150ms RTT minimum
4. **No advantage over H.264** at real-time encode speeds (quality/bitrate similar)
5. **CPU pegging** - consumes 100% CPU on single core for encoding

---

### 4. WebP (Modern Image Compression)

**Compression Method:** VP8 codec repurposed for still images (similar to JPEG alternative)
**Latency Profile:** Moderate-Low

#### Frame Size Reduction
| Compression | File Size | Ratio | Notes |
|-------------|-----------|-------|-------|
| Q95 (lossless equivalent) | ~1.2 MB | 1:5 | Rarely used |
| Q85 (good quality) | ~300-400 KB | 1:15-20 | Better than JPEG at same quality |
| Q75 (balanced) | ~150-200 KB | 1:30-40 | Significantly better than JPEG |
| Q65 (aggressive) | ~80-100 KB | 1:60-75 | Still maintains quality |

**Encoding/Decoding Latency:**
- Software encoding: 100-200 ms per frame (single-threaded)
- Software decoding: 30-50 ms
- Hardware: None (WebP not accelerated anywhere)

**Quality at Low Bitrates:**
- Better than JPEG at same bitrate
- Reduces blocking artifacts
- Supports lossless mode (1:3 compression for RGB data)
- Still frame-based (no inter-frame compression like video codecs)

**Hardware Acceleration:**
- CPU: Excellent software support (libwebp highly optimized)
- GPU: None
- Browser: Chrome, Firefox, Edge (native support)

**Browser/FFmpeg Support:**
- FFmpeg: Good support (libwebp encoder/decoder)
- Browser: Native image format (not video streaming native)
- WebRTC: Not used (image, not video)

**Best Use Cases:**
- Single frame snapshots (better than JPEG)
- Image gallery streaming (better compression than JPEG)
- Fallback when video codecs unavailable

**Why Not for Video Streaming:**
- Still an image codec, not video codec (no frame-to-frame compression)
- 100-200 ms encoding too slow for 60 fps real-time
- No inter-frame reference (every frame independent = massive overhead)
- For 1080p60 at quality parity with H.264: 150-200 KB/frame
- Same fundamental limitation as JPEG: 6x larger than H.264

---

### 5. HEVC/H.265 (Modern Video Codec)

**Compression Method:** Advanced motion compensation with larger prediction blocks + entropy coding
**Latency Profile:** Moderate

#### Frame Size Reduction
| Bitrate | Frame Size | Compression Ratio | vs H.264 |
|---------|-----------|-------------------|----------|
| 2 Mbps | ~42 KB | 1:150 | ~Same |
| 1 Mbps | ~21 KB | 1:300 | ~Same |
| 800 Kbps | ~17 KB | 1:370 | -25% (better) |
| 400 Kbps | ~8.5 KB | 1:730 | -50% (better) |

**Key Difference:** HEVC achieves 25-50% better compression than H.264, BUT at cost of:
- Increased encoding complexity (2-3x slower)
- Patent minefield (multiple patent pools: MPEG LA, Velos, Qualcomm)
- Reduced hardware acceleration availability

**Encoding/Decoding Latency:**
- Software encoding (x265 preset=fast): 100-200 ms per frame (2-4x slower than x264)
- Software decoding: 40-80 ms
- Hardware encoding: NVIDIA NVENC (RTX 20 series+): 3-5 ms
- Hardware decoding: NVIDIA NVDEC (Maxwell+), AMD UVD (Tonga+), Intel iGPU (10th gen+)

**Quality at Low Bitrates:**
- 25-50% bitrate savings vs H.264 for equivalent quality
- Superior at <800 Kbps
- Better handling of fine details

**Hardware Acceleration:**
- **Encoding:** NVIDIA NVENC (limited to RTX cards), AMD VCE (newer), Intel Quick Sync (10th gen+)
- **Decoding:** More limited than H.264 (missing on many older GPUs, older iGPUs)
- Mobile: Good support on modern phones (iPhone 11+, Android 9+)
- Desktop: Spotty (not all GPUs support HEVC decode)

**Browser/FFmpeg Support:**
- FFmpeg: Excellent support (libx265 encoder, hardware/software decoders)
- Browser: Limited (Chrome/Edge VP9 prioritized, Safari on Apple devices)
- WebRTC: Not mandatory (H.264/VP8 prioritized in most implementations)

**Best Use Cases:**
- Bandwidth-constrained scenarios (<1 Mbps)
- Modern mobile-first applications
- On-device storage (local files, not streaming)

**Why NOT (Yet) for Real-Time Streaming:**
1. **Encoding complexity** - requires modern CPU or RTX GPU for acceptable latency
2. **Patent uncertainty** - HEVC licensing model less clear than H.264
3. **Ecosystem maturity** - hardware support spotty vs H.264
4. **Diminishing returns** - 25% savings not worth complexity (vs H.264 at 2 Mbps)
5. **WebRTC gaps** - not mandatory in WebRTC, less decoder support
6. **Future relevance** - AV1 emerging as next standard, why adopt HEVC now?

**Future Note:** HEVC becomes interesting IF:
- Patent issues resolve
- Hardware encoding becomes universal (2026+)
- Bandwidth constraints critical (< 500 Kbps required)

---

## Recommendation for ascii-chat

### Primary: H.264 with These Parameters

```c
// Proposed configuration for src/server/stream.c and src/client/protocol.c

// Encoding parameters
#define VIDEO_CODEC H264
#define VIDEO_BITRATE_ADAPTIVE true      // Scale 500 Kbps - 8 Mbps
#define VIDEO_PRESET_DEFAULT "fast"      // x264 preset: ~30-50ms/frame
#define VIDEO_GOP_SIZE 120               // 2 seconds @ 60fps (every 120 frames)
#define VIDEO_PROFILE "high"              // B-frames, 4:2:0, level 4.2

// Keyframe insertion (critical for stream restart)
#define FORCE_KEYFRAME_EVERY_MS 2000     // Force I-frame every 2 seconds
#define FORCE_KEYFRAME_ON_SCENE_CUT true // Insert I-frame on large changes

// Buffer settings
#define VIDEO_FRAME_BUFFER_DEPTH 3       // 3-frame buffer for reordering
#define MAX_BITRATE_BURST_FACTOR 1.5     // Allow 150% burst for keyframes

// Quality presets for adaptive bitrate
typedef struct {
  const char *name;
  uint32_t bitrate_kbps;
  uint32_t fps;
  uint16_t width;
  uint16_t height;
} video_quality_preset_t;

const video_quality_preset_t video_presets[] = {
  {"ultra_low",    500,  30, 1280, 720},  // Emergency mode
  {"low",          1000, 30, 1920, 1080}, // 2G/mobile
  {"medium",       2000, 60, 1920, 1080}, // Home internet
  {"high",         4000, 60, 1920, 1080}, // Fiber
  {"ultra_high",   8000, 60, 1920, 1080}, // LAN
};
```

### Why H.264

1. **Frame Size Advantage:**
   - 1080p60 @ 2 Mbps: 42 KB/frame (vs 6 MB raw = **147x compression**)
   - 5-8x smaller than JPEG at equivalent quality
   - 2-3x smaller than WebP

2. **Latency Unbeatable:**
   - Hardware acceleration: 1-3 ms encoding
   - Network: 20-50 ms (typical broadband)
   - Total end-to-end: 30-80 ms (acceptable for chat)
   - VP9: 150-300 ms encoding alone (too slow)

3. **Hardware Everywhere:**
   - Desktop: NVIDIA/AMD/Intel all support H.264 encode
   - Mobile: 100% support in iOS/Android
   - Embedded: Raspberry Pi, SBCs all support decode
   - WebRTC: Mandatory codec (interop guaranteed)

4. **Ecosystem Maturity:**
   - 15+ years production use
   - Patent holders committed to licensing
   - Lowest royalty rates
   - Most widely deployed codec globally

### Implementation Path

**Phase 1 (Current):** Keep software encoding, add bitrate adaptation
```c
// Use libx264 with hardware fallback chain:
// 1. Try NVIDIA NVENC (if available)
// 2. Fall back to x264 "fast" preset (30-50 ms acceptable for 60fps)
// 3. Graceful degradation to lower fps if needed
```

**Phase 2 (Future):** Hardware acceleration optionality
```c
// Abstract encoder interface:
typedef struct {
  void (*encode)(video_frame_t *in, encoded_packet_t *out);
  void (*destroy)(void);
} video_encoder_t;

video_encoder_t *encoder_create(video_codec_t codec, int bitrate) {
  #ifdef HAVE_NVENC
  if (codec == H264) return nvenc_encoder_create(bitrate);
  #endif
  #ifdef HAVE_QSV
  if (codec == H264) return qsv_encoder_create(bitrate);
  #endif
  return libx264_encoder_create(codec, bitrate);  // Fallback
}
```

**Phase 3 (Post-Keyframe Rework):** AV1 consideration (2027+)
- Only if hardware encoding becomes universal
- Only if latency acceptable for real-time

---

## Compression Ratio Summary Table

| Codec | Bitrate | Frame Size | Compression | Encoding | Decoding | Hardware |
|-------|---------|-----------|-------------|----------|----------|----------|
| **Raw RGB** | - | 6220 KB | 1:1 | - | - | - |
| JPEG Q75 | - | 250-300 KB | 1:20 | 80 ms | 30 ms | Limited |
| **H.264** | 2 Mbps | **42 KB** | **1:150** | **30ms (HW:1ms)** | **15ms (HW:2ms)** | **Excellent** |
| H.264 | 1 Mbps | 21 KB | 1:300 | 30 ms | 15 ms | Excellent |
| WebP Q75 | - | 150-200 KB | 1:30 | 120 ms | 40 ms | None |
| VP8 | 2 Mbps | 50 KB | 1:120 | 120 ms | 25 ms | Minimal |
| VP9 | 1.5 Mbps | 31 KB | 1:200 | 200 ms | 30 ms | Minimal |
| HEVC | 2 Mbps | 42 KB | 1:150 | 100ms (HW:4ms) | 50ms (HW:3ms) | Moderate |
| HEVC | 1 Mbps | 21 KB | 1:300 | 100 ms | 50 ms | Moderate |

**Winner: H.264** (Best balance of compression, latency, hardware support, and ecosystem)

---

## Network Bandwidth Implications

**Scenario: 1080p60 home internet (10 Mbps upstream)**

| Codec | Bitrate | Frame Rate | Audio | Total | Sustainable? |
|-------|---------|-----------|-------|-------|---------------|
| H.264 | 2 Mbps | 60 fps | 128 Kbps | 2.128 Mbps | ✅ Yes (5x overhead) |
| H.264 | 4 Mbps | 60 fps | 128 Kbps | 4.128 Mbps | ✅ Yes (2.4x overhead) |
| H.264 | 6 Mbps | 60 fps | 128 Kbps | 6.128 Mbps | ⚠️ Tight |
| HEVC | 1 Mbps | 60 fps | 128 Kbps | 1.128 Mbps | ✅ Yes (8.8x overhead) |
| VP9 | 2 Mbps | 60 fps | 128 Kbps | 2.128 Mbps | ✅ Yes (5x overhead) |
| JPEG | - | 60 fps | 128 Kbps | 6.3 Mbps | ⚠️ Tight |

---

## Implementation Checklist for ascii-chat

- [ ] Add H.264 encoder configuration to `lib/options/registry/video.c`
- [ ] Implement bitrate adaptation in `src/server/stream.c`
- [ ] Add keyframe insertion logic to ensure stream reliability
- [ ] Integrate libx264 (already used for testing?)
- [ ] Add optional NVIDIA NVENC support (graceful fallback)
- [ ] Update packet protocol to support H.264 frame types (SPS/PPS/IDR/P/B)
- [ ] Implement temporal layer extraction for low-delay scenarios
- [ ] Add quality metrics to `video_frame_stats_t` for adaptive streaming
- [ ] Test real-time encoding on target hardware (x86, ARM)
- [ ] Benchmark compression ratios vs. current JPEG/WebP if used

---

## References

- H.264 Specification: ITU-T H.264 | ISO/IEC 14496-10 (AVC)
- HEVC Specification: ITU-T H.265 | ISO/IEC 23008-2
- VP8/VP9: RFC 7741 (VP8) / Open Source Alliance
- WebP: https://developers.google.com/speed/webp
- Real-time encoding: https://trac.ffmpeg.org/wiki/Encode/H.264
- libx264 presets: https://trac.ffmpeg.org/wiki/Encode/H.264#Preset
