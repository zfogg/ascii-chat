# Video Encoding Quick Reference

## TL;DR: Use H.264

For 1080p@60fps real-time streaming with minimal network overhead:

```
H.264 @ 2 Mbps = 42 KB/frame (147x compression vs raw RGB)
├─ Encoding latency: 1-3 ms (NVIDIA) or 30-50 ms (CPU)
├─ Decoding latency: 2-4 ms (NVIDIA) or 15-30 ms (CPU)
├─ Hardware everywhere: NVIDIA, AMD, Intel, ARM, Apple
└─ WebRTC mandatory: Works with any browser client
```

---

## Visual Comparison: 1080p Frame Sizes

```
Raw RGB:        [====================================] 6,220 KB
H.264 1 Mbps:   [=] 21 KB                            (1:296x)
H.264 2 Mbps:   [==] 42 KB                           (1:148x)
HEVC 1 Mbps:    [=] 21 KB                            (1:296x)
VP9 1.5 Mbps:   [==] 31 KB                           (1:200x)
JPEG Q75:       [=======] 250 KB                     (1:25x)
WebP Q75:       [======] 150-200 KB                  (1:31-41x)
```

---

## Encoding Time Comparison (Per Frame @ 1080p)

```
H.264 (libx264 fast):   ████████░░░░ 30-50 ms   (acceptable for 60fps)
H.264 (NVIDIA NVENC):   ░░░░░░░░░░░░ 1-3 ms     (excellent, hardware)
HEVC (libx265 fast):    ████████████████░░░░░░ 100-200 ms  (too slow)
HEVC (NVIDIA NVENC):    ░░░░░░░░░░░░ 3-5 ms    (good but limited)
VP8 (libvpx):           ████████████████████░░ 80-150 ms   (slow)
VP9 (libvpx-vp9):       ████████████████████████ 150-300 ms (very slow)
JPEG (libjpeg-turbo):   ███░░░░░░░░░░░░░░░░░░░░ 15-30 ms   (fast but low compression)
WebP (libwebp):         ██████░░░░░░░░░░░░░░░░░ 100-200 ms (medium)
```

---

## Hardware Acceleration Availability

### Desktop (NVIDIA/AMD/Intel)

```
Codec     Encoding              Decoding                Available Since
─────────────────────────────────────────────────────────────────────
H.264     NVENC/VCE/QuickSync   Universal               2010s (NVENC)
HEVC      Limited NVENC         Limited support         2015+ (NVENC RTX)
VP8       Rare/None             Some AMD                2012+
VP9       None                  Rare (newer AMD)        2016+
AV1       Emerging              Emerging (NVIDIA 40s)   2023+
```

### Mobile (iOS/Android)

```
iOS:                All modern iPhones (H.264, HEVC)
Android:            All recent phones (H.264, VP8, VP9, HEVC)
Raspberry Pi:       H.264 only (hardware decoder)
```

### Browser (WebRTC)

```
Chrome:             H.264, VP8, VP9
Firefox:            H.264, VP8, VP9
Safari:             H.264, HEVC
Edge:               H.264, VP8, VP9
```

---

## Bandwidth Usage (Real-World Scenarios)

### Scenario 1: Home Internet (10 Mbps upstream)

```
H.264 @ 2 Mbps:     ▓▓░░░░░░░░ 2.1 Mbps (5.0x overhead) ✅ GOOD
H.264 @ 4 Mbps:     ▓▓▓▓░░░░░░ 4.1 Mbps (2.4x overhead) ✅ GOOD
H.264 @ 6 Mbps:     ▓▓▓▓▓▓░░░░ 6.1 Mbps (1.6x overhead) ⚠️ TIGHT
JPEG:               ▓▓▓▓▓▓▓░░░ 6.3 Mbps (1.6x overhead) ⚠️ TIGHT
Raw RGB:            ▓▓▓▓▓▓▓▓▓▓ 373 Mbps (blocked)        ❌ IMPOSSIBLE
```

### Scenario 2: Mobile 4G (5 Mbps upstream)

```
H.264 @ 1 Mbps:     ▓░░░░░░░░░ 1.1 Mbps (4.5x overhead) ✅ GOOD
H.264 @ 2 Mbps:     ▓▓░░░░░░░░ 2.1 Mbps (2.4x overhead) ⚠️ TIGHT
HEVC @ 800 Kbps:    ▓░░░░░░░░░ 0.9 Mbps (5.5x overhead) ✅ GOOD
```

### Scenario 3: LAN/Office (100 Mbps)

```
H.264 @ 8 Mbps:     ▓▓▓▓▓▓▓▓░░ 8.1 Mbps (12x overhead)  ✅ EXCELLENT
H.264 @ 15 Mbps:    ▓▓▓▓▓▓▓▓▓▓ 15+ Mbps                 ✅ EXCELLENT
```

---

## Quality Perception @ Low Bitrates

### H.264 (Adaptive bitrate scaling)

```
1 Mbps:  1080p@30fps → 720p@30fps → 480p@60fps (graceful degradation)
         Quality: Acceptable, slight blockiness

2 Mbps:  1080p@60fps (smooth)
         Quality: Good, minor compression artifacts

4 Mbps:  1080p@60fps (high quality)
         Quality: Excellent, near-transparent compression
```

### JPEG (Fixed frame size)

```
Q75:     250-300 KB/frame
         Quality: Noticeable blocking, especially on edges
         Issue: No temporal coherence (flicker between frames)

Q85:     400-500 KB/frame
         Quality: Acceptable, balanced compression/quality
         Issue: Still no frame-to-frame correlation
```

### VP9 (Theoretically best, practically unusable)

```
1.5 Mbps: ~31 KB/frame (excellent frame size)
         Quality: Superior to H.264 at same bitrate
         Issue: Takes 200+ ms to encode (impossible for real-time)
```

---

## Decision Matrix

| Requirement | H.264 | HEVC | VP9 | JPEG | WebP |
|-------------|-------|------|-----|------|------|
| Frame size <100 KB | ✅ | ✅ | ✅ | ❌ | ❌ |
| Real-time 60fps | ✅ | ⚠️ | ❌ | ❌ | ❌ |
| HW acceleration | ✅ | ⚠️ | ❌ | ❌ | ❌ |
| WebRTC compat | ✅ | ❌ | ✅ | ❌ | ❌ |
| Ecosystem maturity | ✅ | ⚠️ | ⚠️ | ✅ | ⚠️ |
| Patent clarity | ✅ | ❌ | ✅ | ✅ | ✅ |
| Mobile support | ✅ | ✅ | ⚠️ | ✅ | ⚠️ |

**Overall Winner: H.264** ✅

---

## Specific Numbers for 1080p@60fps

### H.264 @ 2 Mbps (Recommended for Home Internet)

```
Raw frame:              6,220,800 bytes
Compressed frame:       42,000 bytes (average)
Keyframe:              500,000 bytes (every 2 seconds)

Compression ratio:      1:148x
Bitrate sustained:      2.0 Mbps
Bitrate with audio:     2.128 Mbps (128 Kbps audio)
Network requirement:    >=3 Mbps downstream
                       >=2.5 Mbps upstream

End-to-end latency:
  Encoding:   1-3 ms (HW) / 30-50 ms (SW)
  Network:    20-50 ms (typical broadband)
  Decoding:   2-4 ms (HW) / 15-30 ms (SW)
  Total:      25-80 ms (HW) / 65-130 ms (SW)

Quality:               Good (minor compression artifacts on edges)
Motion smoothness:     Excellent (60 fps)
```

### H.264 @ 1 Mbps (Emergency/Mobile)

```
Compressed frame:       21,000 bytes (average)
Keyframe:              350,000 bytes (every 2 seconds)

Compression ratio:      1:296x
Bitrate sustained:      1.0 Mbps
Bitrate with audio:     1.128 Mbps

Network requirement:    >=2 Mbps downstream
                       >=1.5 Mbps upstream

Quality:               Acceptable (noticeable compression, reduced resolution)
Resolution fallback:   1080p@30fps → 720p@30fps
```

### H.264 @ 4 Mbps (High Quality)

```
Compressed frame:       83,000 bytes (average)
Compression ratio:      1:75x
Bitrate sustained:      4.0 Mbps

Network requirement:    >=5 Mbps downstream
                       >=5 Mbps upstream

Quality:               Excellent (near-transparent compression)
Artifacts:             Minimal
```

---

## Implementation Priority

### Immediate (Phase 1)
- [ ] Add libx264 software encoding
- [ ] Implement basic H.264 packet fragmentation
- [ ] Add bitrate control (1-4 Mbps range)
- [ ] Test on development hardware

### Short-term (Phase 2)
- [ ] Add NVIDIA NVENC support with fallback
- [ ] Implement keyframe injection at scene cuts
- [ ] Add quality metrics and adaptive bitrate
- [ ] Test on Raspberry Pi (decode only)

### Medium-term (Phase 3)
- [ ] AMD VCE support
- [ ] Intel Quick Sync support
- [ ] WebRTC integration for browser clients
- [ ] Performance optimization/profiling

### Future (Phase 4+)
- [ ] HEVC support (only if >30% bandwidth savings critical)
- [ ] AV1 evaluation (wait for hardware maturity)

---

## Final Recommendation for ascii-chat

```c
// In src/server/stream.c

#define DEFAULT_VIDEO_CODEC    VIDEO_CODEC_H264
#define DEFAULT_VIDEO_BITRATE  2000  // 2 Mbps for home internet
#define DEFAULT_VIDEO_FPS      60
#define DEFAULT_VIDEO_PRESET   "fast"  // 30-50ms/frame acceptable
#define FORCE_KEYFRAME_EVERY   2000  // ms (every 2 seconds)

// Adaptive bitrate presets
QUALITY_PRESET low_bandwidth = {
  .bitrate = 1000,  // 1 Mbps
  .fps = 30,
  .resolution = {1280, 720}
};

QUALITY_PRESET home_internet = {
  .bitrate = 2000,  // 2 Mbps
  .fps = 60,
  .resolution = {1920, 1080}
};

QUALITY_PRESET fast_network = {
  .bitrate = 4000,  // 4 Mbps
  .fps = 60,
  .resolution = {1920, 1080}
};
```

---

## References for Implementation

- H.264 in FFmpeg: `ffmpeg -h encoder=libx264`
- Real-time tuning: https://trac.ffmpeg.org/wiki/Encode/H.264#Preset
- NVIDIA NVENC: https://developer.nvidia.com/nvidia-video-codec-sdk
- WebRTC H.264 profile: RFC 7741 Appendix B
- Bitrate calculator: https://www.dr-lex.be/info-stuff/videosize.html
