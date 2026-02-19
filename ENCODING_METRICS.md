# Video Encoding Metrics - Detailed Data Tables

Comprehensive quantitative comparison for 1080p@60fps streaming.

---

## Frame Size Analysis (1080p = 1920×1080 pixels)

### Raw Data Baselines

| Format | Bytes/Pixel | Frame Size | Annual Data (GB) | Notes |
|--------|------------|-----------|-----------------|-------|
| RGB 8-bit | 3 | 6,220,800 | 19,872 | Industry standard |
| RGBA 8-bit | 4 | 8,294,400 | 26,496 | With alpha channel |
| YUV 4:2:0 | 1.5 | 3,110,400 | 9,936 | H.264/HEVC input |

### Compressed Frame Sizes

| Codec | Bitrate | Frame Avg | Keyframe | Monthly (GB) | Notes |
|-------|---------|-----------|----------|-------------|-------|
| Raw RGB | - | 6,220 KB | 6,220 KB | 1,656 | Baseline |
| **H.264** | **500 Kbps** | **10.4 KB** | **200 KB** | **0.26** | Emergency mode |
| **H.264** | **1 Mbps** | **20.8 KB** | **350 KB** | **0.53** | Mobile 4G |
| **H.264** | **2 Mbps** | **41.6 KB** | **500 KB** | **1.06** | **Home internet** |
| **H.264** | **4 Mbps** | **83.3 KB** | **800 KB** | **2.11** | Good quality |
| **H.264** | **6 Mbps** | **125 KB** | **1.2 MB** | **3.17** | High quality |
| HEVC | 1 Mbps | 20.8 KB | 350 KB | 0.53 | Better compression |
| HEVC | 800 Kbps | 16.7 KB | 280 KB | 0.42 | Good quality |
| HEVC | 400 Kbps | 8.3 KB | 180 KB | 0.21 | Low bandwidth |
| VP9 | 1.5 Mbps | 31.2 KB | 400 KB | 0.79 | Good compression |
| VP9 | 1 Mbps | 20.8 KB | 300 KB | 0.53 | Worst encoding latency |
| WebP Q85 | - | 400-500 KB | - | 10.7-13.4 | Per-frame compression |
| WebP Q75 | - | 150-200 KB | - | 4.0-5.3 | Aggressive compression |
| JPEG Q85 | - | 400-500 KB | - | 10.7-13.4 | Similar to WebP |
| JPEG Q75 | - | 250-300 KB | - | 6.7-8.0 | Common quality |
| JPEG Q65 | - | 150-180 KB | - | 4.0-4.8 | Low quality |

### Compression Ratios (vs Raw RGB)

| Codec | Config | Ratio | Reduction |
|-------|--------|-------|-----------|
| H.264 @ 500 Kbps | Emergency | **1:597** | **99.83%** |
| H.264 @ 1 Mbps | Mobile | **1:299** | **99.67%** |
| **H.264 @ 2 Mbps** | **Home** | **1:149** | **99.33%** |
| H.264 @ 4 Mbps | Good | 1:75 | 98.7% |
| HEVC @ 1 Mbps | Mobile | 1:299 | 99.67% |
| HEVC @ 800 Kbps | Better | 1:374 | 99.73% |
| VP9 @ 1.5 Mbps | Good | 1:199 | 99.50% |
| WebP Q75 | Image | 1:31-41 | 96.9-97.4% |
| JPEG Q75 | Image | 1:20-25 | 95.0-96.0% |

---

## Latency Analysis

### Per-Frame Processing Time (1080p)

| Step | H.264 SW | H.264 HW | HEVC SW | HEVC HW | VP9 | JPEG |
|------|----------|----------|---------|---------|-----|------|
| Capture | 0.5 ms | 0.5 ms | 0.5 ms | 0.5 ms | 0.5 ms | 0.5 ms |
| YUV Conversion | 3-5 ms | 3-5 ms | 3-5 ms | 3-5 ms | 3-5 ms | 3-5 ms |
| Encoding | **30-50 ms** | **1-3 ms** | **100-200 ms** | **3-5 ms** | **80-150 ms** | **15-30 ms** |
| Network TX | 20-50 ms | 20-50 ms | 20-50 ms | 20-50 ms | 20-50 ms | 20-50 ms |
| Network RX | 20-50 ms | 20-50 ms | 20-50 ms | 20-50 ms | 20-50 ms | 20-50 ms |
| Decoding | **15-30 ms** | **2-4 ms** | **40-80 ms** | **3-5 ms** | **20-50 ms** | **30-50 ms** |
| Display | 8-16 ms | 8-16 ms | 8-16 ms | 8-16 ms | 8-16 ms | 8-16 ms |
| **TOTAL (ms)** | **97-201** | **64-142** | **199-365** | **72-142** | **169-237** | **95-191** |

**Best case (hardware accelerated):** H.264 NVENC = 64-142 ms
**Worst case (software):** HEVC = 199-365 ms (unacceptable for real-time)

### Frame-to-Frame Latency Budget (60 fps = 16.67 ms per frame)

| Codec | Encoding Time | Fits in Budget? | Notes |
|-------|---------------|----------------|-------|
| H.264 SW (30 ms) | 30 ms | ❌ No | Exceeds budget (1.8x) |
| H.264 SW (50 ms) | 50 ms | ❌ No | Exceeds budget (3x) |
| H.264 HW (1-3 ms) | 3 ms | ✅ Yes | Well within budget |
| HEVC SW (100+ ms) | 100 ms | ❌ No | 6x over budget |
| HEVC HW (3-5 ms) | 5 ms | ✅ Yes | Within budget |
| VP9 (80-150 ms) | 150 ms | ❌ No | 9x over budget |
| JPEG (15-30 ms) | 30 ms | ❌ No | 1.8x over budget |

**Solution:** For 60fps real-time, requires either:
1. Hardware acceleration (H.264 NVENC: 1-3 ms)
2. Lower frame rate (30 fps = 33 ms budget, accommodates SW encoding)
3. Multi-threaded software encoding (distribute frames across cores)

---

## Network Bandwidth Requirements

### Data Flow at 60 fps

| Codec | Bitrate | Bytes/Sec | Mbps | Mbps+Audio | 10Mbps Link |
|-------|---------|-----------|------|-----------|------------|
| Raw RGB | - | 373.2 MB | 2984 | 2984.1 | ❌ 298x over |
| H.264 | 500 Kbps | 62.5 KB | 0.5 | 0.628 | ✅ 16x margin |
| H.264 | 1 Mbps | 125 KB | 1.0 | 1.128 | ✅ 8.9x margin |
| **H.264** | **2 Mbps** | **250 KB** | **2.0** | **2.128** | **✅ 4.7x margin** |
| H.264 | 4 Mbps | 500 KB | 4.0 | 4.128 | ✅ 2.4x margin |
| H.264 | 6 Mbps | 750 KB | 6.0 | 6.128 | ⚠️ 1.6x margin |
| H.264 | 8 Mbps | 1 MB | 8.0 | 8.128 | ⚠️ 1.2x margin |
| HEVC | 1 Mbps | 125 KB | 1.0 | 1.128 | ✅ 8.9x margin |
| HEVC | 800 Kbps | 100 KB | 0.8 | 0.928 | ✅ 10.8x margin |
| VP9 | 1.5 Mbps | 187.5 KB | 1.5 | 1.628 | ✅ 6.1x margin |
| JPEG | avg 300 KB | 18 MB | 144 | 144.1 | ❌ 14x over |
| WebP | avg 200 KB | 12 MB | 96 | 96.1 | ❌ 9.6x over |

### Network Overhead Calculations

| Connection | Speed | H264@2Mbps | HEVC@1Mbps | VP9@1.5Mbps | Note |
|------------|-------|-----------|-----------|-----------|------|
| LTE Cat-4 | 150 Mbps | ✅ 70x | ✅ 130x | ✅ 92x | Excellent |
| LTE Cat-6 | 300 Mbps | ✅ 140x | ✅ 260x | ✅ 184x | Excellent |
| 5G | 1 Gbps | ✅ 468x | ✅ 885x | ✅ 614x | Overkill |
| WiFi-5 (11ac) | 200 Mbps | ✅ 94x | ✅ 177x | ✅ 123x | Good |
| WiFi-6 (11ax) | 1 Gbps | ✅ 468x | ✅ 885x | ✅ 614x | Excellent |
| Home ADSL | 10 Mbps | ✅ 4.7x | ✅ 8.9x | ✅ 6.1x | **Tight but works** |
| Home Fiber | 100 Mbps | ✅ 47x | ✅ 89x | ✅ 61x | Good |
| Satellite (Starlink) | 150 Mbps | ✅ 70x | ✅ 130x | ✅ 92x | Good |
| Tethered Mobile | 5 Mbps | ⚠️ 2.3x | ⚠️ 4.4x | ⚠️ 3.1x | **Emergency only** |

---

## Quality-at-Bitrate Comparison

### Subjective Quality Scale (1-10)

| Bitrate | H.264 | HEVC | VP9 | JPEG Q85 | WebP Q85 |
|---------|-------|------|-----|----------|----------|
| 500 Kbps | 4-5 | 5-6 | N/A | N/A | N/A |
| 800 Kbps | 5-6 | 6-7 | 6-7 | N/A | N/A |
| 1 Mbps | 6-7 | 7-8 | 7-8 | N/A | N/A |
| 1.5 Mbps | 7-8 | 8 | 8 | N/A | N/A |
| 2 Mbps | 7.5-8 | 8.5 | 8-8.5 | 6-7 | 6-7 |
| 4 Mbps | 8.5-9 | 9 | 9+ | N/A | N/A |

### Artifact Type by Codec

| Artifact | H.264 | HEVC | VP9 | JPEG | WebP |
|----------|-------|------|-----|------|------|
| Blocking | Minimal | Rare | Rare | Visible | Minimal |
| Banding | Minimal | Minimal | Minimal | Visible | Minimal |
| Blur | Slight | Slight | Slight | None | Minimal |
| Ringing | Minimal | Minimal | Minimal | Visible | Minimal |
| Motion | Smooth | Smooth | Smooth | Jittery | Jittery |
| Temporal | Smooth | Smooth | Smooth | Inconsistent | Inconsistent |

---

## Hardware Support Matrix

### Desktop Systems

| GPU/iGPU | H.264 Encode | H.264 Decode | HEVC Enc | HEVC Dec | VP9 | Notes |
|----------|-------------|-------------|----------|----------|-----|-------|
| NVIDIA RTX | ✅ NVENC | ✅ NVDEC | ✅ NVENC | ✅ NVDEC | ⚠️ | Best support |
| NVIDIA GTX | ❌ | ✅ NVDEC | ❌ | ⚠️ | ❌ | Older cards |
| AMD RX 5700+ | ✅ VCE | ✅ UVD | ✅ | ✅ | ❌ | Newer RDNA |
| AMD RX Vega | ⚠️ | ✅ | ❌ | ❌ | ❌ | Limited |
| Intel 10th+ | ✅ QuickSync | ✅ | ✅ | ✅ | ❌ | Good support |
| Intel 8th-9th | ✅ QuickSync | ✅ | ❌ | ❌ | ❌ | H.264 only |
| Intel UHD | ✅ | ✅ | ⚠️ | ⚠️ | ❌ | Integrated |
| Apple Silicon | ✅ | ✅ | ✅ | ✅ | ✅ | VideoToolbox |

### Mobile Systems

| Platform | H.264 | HEVC | VP8/VP9 | AV1 | Notes |
|----------|-------|------|---------|-----|-------|
| iPhone 11+ | ✅ | ✅ | ⚠️ | ❌ | Hardware accelerated |
| iPhone 8-10 | ✅ | ⚠️ | ⚠️ | ❌ | Software fallback |
| Android 9+ | ✅ | ✅ | ✅ | ⚠️ | Device dependent |
| Android 5-8 | ✅ | ⚠️ | ⚠️ | ❌ | Limited support |
| Raspberry Pi | ✅ decode | ❌ | ⚠️ | ❌ | H.264 decode only |
| Snapdragon 888+ | ✅ | ✅ | ✅ | ✅ | Latest flagship |

---

## Energy Efficiency (Battery Impact)

### Power Consumption per Frame (Estimated)

| Device | H.264 SW | H.264 HW | HEVC SW | HEVC HW |
|--------|----------|----------|---------|---------|
| i7-9700K | 45W (30ms) | - | 60W (150ms) | - |
| NVIDIA RTX 2080 | - | 5W (1-3ms) | - | 5W (3-5ms) |
| iPhone 14 | 3W (SW) | 0.5W (HW) | 3W (SW) | 0.5W (HW) |
| Raspberry Pi 4 | 1.5W (decode) | 0.3W (HW) | N/A | N/A |

**Conclusion:** Hardware acceleration = 6-10x less power

### Battery Runtime (iPhone streaming 1 hour)

| Codec | Power Draw | Battery Remaining (%) |
|-------|-----------|----------------------|
| H.264 HW (HEVC SW fallback) | 2-3W | 85-90% |
| H.264 SW | 5-8W | 80-85% |
| HEVC SW | 8-12W | 75-80% |

---

## Practical Bitrate Recommendations by Use Case

### Real-Time Chat (1080p60)

| Internet | Codec | Bitrate | Frame Size | Sustainable |
|----------|-------|---------|-----------|------------|
| Home ADSL (10 Mbps) | H.264 | 2 Mbps | 42 KB | ✅ Yes |
| Home Cable (50 Mbps) | H.264 | 4 Mbps | 83 KB | ✅ Yes |
| Mobile 4G (5 Mbps) | H.264 | 1 Mbps | 21 KB | ✅ Yes |
| LTE (15 Mbps) | H.264 | 3 Mbps | 63 KB | ✅ Yes |
| 5G (100+ Mbps) | H.264 | 6-8 Mbps | 125-167 KB | ✅ Yes |

### Screen Sharing (1080p60)

H.264 with scene-cut detection (force keyframe on large changes)
- High motion: 3-4 Mbps
- Medium motion: 2-3 Mbps
- Low motion (static desktop): 800 Kbps-1.5 Mbps

### Recorded Streaming (Async, not real-time)

- Quality: HEVC @ 1-2 Mbps or VP9 @ 1.5-2.5 Mbps
- Bitrate can increase (encoding time not critical)
- Keyframe spacing: 5-10 seconds (less frequent)

---

## Encoding Throughput (Frames/Second Achievable)

### Single Core Performance

| Codec | Settings | 1080p30 | 1080p60 | 720p60 | 480p60 |
|-------|----------|---------|---------|--------|--------|
| H.264 | fast | ✅ 60 | ⚠️ 20 | ✅ 40 | ✅ 100 |
| H.264 | medium | ✅ 30 | ❌ 10 | ⚠️ 20 | ✅ 60 |
| HEVC | fast | ⚠️ 20 | ❌ 10 | ⚠️ 15 | ✅ 40 |
| HEVC | medium | ❌ 10 | ❌ 5 | ❌ 8 | ⚠️ 20 |
| VP9 | fast | ⚠️ 10 | ❌ 5 | ❌ 8 | ⚠️ 15 |
| NVENC H.264 | - | ✅ 2000+ | ✅ 2000+ | ✅ 2000+ | ✅ 2000+ |

**Requirement: 1080p60 = need either H.264 "fast" + 3-4 cores, or hardware acceleration**

---

## Cost-Benefit Analysis

### Development Effort vs Bandwidth Savings

| Option | Dev Hours | Bandwidth Saved | ROI |
|--------|-----------|-----------------|-----|
| Keep RGB (status quo) | 0 | 0% | 0 |
| Add JPEG | 4-8 | 95% | Moderate |
| Add H.264 | 20-30 | 99.3% | **High** |
| Add HEVC | 25-35 | 99.6% | Marginal gain |
| Add VP9 | 30-40 | 99.5% | Negative (encoding time) |

---

## Summary Table: Best Choice by Constraint

| Primary Constraint | Recommended | Bitrate | Frame Size | Reason |
|-------------------|------------|---------|-----------|--------|
| Lowest latency | H.264 + NVENC | 2 Mbps | 42 KB | 1-3 ms encoding |
| Lowest bandwidth | HEVC | 800 Kbps | 17 KB | 25% smaller |
| Most compatible | H.264 | 2 Mbps | 42 KB | Universal hardware |
| Easiest impl | H.264 SW | 2 Mbps | 42 KB | Simpler fallback |
| Royalty-free | VP9 (async) | 1.5 Mbps | 31 KB | But slow encoding |
| Real-time+sync | H.264 | 2 Mbps | 42 KB | Only viable option |

**Winner for ascii-chat:** H.264 @ 2 Mbps with NVENC fallback to libx264
