# Video Encoding Analysis Documentation

Complete analysis of video encoding options for real-time low-latency streaming in ascii-chat.

## Quick Navigation

### For Decision-Makers
Start here: **ENCODING_SUMMARY.md** (12 KB, 5-minute read)
- Executive summary and recommendation
- Why H.264 was chosen
- Risks and next steps

### For Technical Architects
Then read: **VIDEO_ENCODING_ANALYSIS.md** (18 KB, 20-minute read)
- Detailed technical comparison of all 5 codecs
- Compression ratios, latency profiles, quality analysis
- Deep-dive into each option with use cases

### For Developers
Implementation guide: **ENCODING_IMPLEMENTATION_GUIDE.md** (19 KB, reference document)
- Step-by-step H.264 integration walkthrough
- Code examples for libx264 backend
- CMake integration and testing strategy
- Migration path from RGB to H.264

### For Data Analysis
Metrics and numbers: **ENCODING_METRICS.md** (12 KB, reference document)
- Quantitative comparison tables
- Network bandwidth calculations
- Hardware acceleration matrix
- Energy efficiency analysis

### Quick Lookup
Cheat sheet: **ENCODING_QUICK_REFERENCE.md** (8.6 KB, reference document)
- Visual comparison charts
- Decision matrix
- TL;DR summaries
- Hardware availability matrix

---

## Analysis Scope

**Video Format:** 1080p (1920×1080) at 60 fps
**Baseline:** Raw RGB = 6.22 MB/frame = 373 MB/sec
**Target:** Reduce to ~100-500 KB/frame for typical internet

### Codecs Analyzed

1. **JPEG** - Lossy image compression
2. **H.264/AVC** - Video codec, widely supported ← **RECOMMENDED**
3. **VP8/VP9** - Royalty-free video codecs
4. **WebP** - Modern image compression
5. **HEVC/H.265** - Modern video codec

---

## Key Findings Summary

### Frame Size Comparison (1080p60)

| Codec | Bitrate | Frame Size | Compression | Real-time? |
|-------|---------|-----------|-------------|-----------|
| Raw RGB | - | 6,220 KB | 1:1 | No |
| **H.264** | **2 Mbps** | **42 KB** | **1:148** | **✓ YES** |
| HEVC | 2 Mbps | 42 KB | 1:148 | No (too slow) |
| VP9 | 1.5 Mbps | 31 KB | 1:200 | No (way too slow) |
| JPEG | Q75 | 250 KB | 1:25 | No (1x/frame) |
| WebP | Q75 | 150-200 KB | 1:31-41 | No (too slow) |

### Encoding Latency (Per Frame)

| Codec | Software | Hardware | Fits 60fps? |
|-------|----------|----------|-----------|
| H.264 | 30-50 ms | 1-3 ms | ✓ YES |
| HEVC | 100-200 ms | 3-5 ms | No |
| VP9 | 150-300 ms | N/A | No |
| JPEG | 15-30 ms | N/A | ✓ YES (but wrong tool) |
| WebP | 100-200 ms | N/A | No |

### Recommendation

**H.264 @ 2 Mbps with Hardware Acceleration Fallback**

- **Frame size:** 42 KB (147× compression)
- **Encoding latency:** 1-3 ms (NVIDIA NVENC) or 30-50 ms (libx264)
- **Hardware support:** Excellent (desktop, mobile, embedded, browser)
- **Network:** 2.128 Mbps (fits on 10 Mbps home internet)
- **Quality:** Good (minor compression artifacts)
- **Ecosystem:** Proven, mature, well-documented

---

## Network Impact

### Current (Raw RGB + zstd)
```
6.22 MB/frame × 60 fps = 373 MB/sec = 2,984 Mbps
Result: Completely infeasible for real-time streaming
```

### With H.264 @ 2 Mbps
```
Video:  2.0 Mbps
Audio:  128 Kbps
Total:  2.128 Mbps
10 Mbps connection overhead: 4.7× (works well)
```

---

## Implementation Summary

### Architecture Change

**Before:**
```
Webcam → RGB Conversion → Raw Frames (6.2 MB) → zstd Compression → Network
```

**After:**
```
Webcam → YUV Conversion → H.264 Encoder → NAL Units (42 KB) → Network
```

### Development Effort

| Phase | Hours | Duration | Outcome |
|-------|-------|----------|---------|
| Phase 1: Foundation | 20-30 | Weeks 1-2 | Working software encoding |
| Phase 2: Optimization | 15-20 | Weeks 3-4 | Hardware acceleration + adaptive bitrate |
| Phase 3: Polish | 10-15 | Weeks 5-6 | Multi-platform support |
| **Total** | **45-65** | **1.5-2 weeks FT** | Production-ready |

---

## File Structure

```
/home/user/ascii-chat/
├── ENCODING_README.md                    ← You are here
├── ENCODING_SUMMARY.md                   ← Executive summary
├── VIDEO_ENCODING_ANALYSIS.md            ← Detailed technical analysis
├── ENCODING_QUICK_REFERENCE.md           ← Quick lookup charts
├── ENCODING_METRICS.md                   ← Quantitative data tables
└── ENCODING_IMPLEMENTATION_GUIDE.md      ← Step-by-step implementation
```

**Total:** 5 documents, 2,067 lines, 69.6 KB

---

## Reading Paths

### Path 1: Decision-Maker (15 minutes)
1. ENCODING_SUMMARY.md (full read)
2. ENCODING_QUICK_REFERENCE.md (skim charts)
→ **Action:** Approve H.264 implementation

### Path 2: Technical Lead (45 minutes)
1. ENCODING_SUMMARY.md (full read)
2. VIDEO_ENCODING_ANALYSIS.md (full read, pages 1-15)
3. ENCODING_METRICS.md (skim tables)
→ **Action:** Plan technical approach

### Path 3: Implementing Engineer (2 hours)
1. ENCODING_IMPLEMENTATION_GUIDE.md (full read)
2. VIDEO_ENCODING_ANALYSIS.md (reference H.264 section)
3. ENCODING_METRICS.md (latency tables)
4. ENCODING_QUICK_REFERENCE.md (hardware matrix)
→ **Action:** Begin implementation

### Path 4: Architecture Review (1 hour)
1. ENCODING_SUMMARY.md (read)
2. VIDEO_ENCODING_ANALYSIS.md (read)
3. ENCODING_IMPLEMENTATION_GUIDE.md (skim code)
→ **Action:** Approve architecture

---

## Key Decisions

### Decision 1: H.264 vs Alternatives
**Result:** H.264 chosen
**Why:** Only codec achieving real-time 1080p60 with acceptable latency
- JPEG: 6× larger, no frame-to-frame compression
- WebP: 4× larger, 100+ ms encoding
- VP9: 31 KB frame size but 150+ ms encoding (impossible for real-time)
- HEVC: Same compression as H.264 but 2-4× slower encoding

### Decision 2: Software vs Hardware Encoding
**Result:** Software (libx264) with hardware fallback chain
**Why:** Fallback approach maximizes compatibility
- Primary: NVIDIA NVENC (1-3 ms, RTX cards)
- Secondary: AMD VCE (3-5 ms, newer cards)
- Tertiary: Intel Quick Sync (5-10 ms, 6th gen+)
- Fallback: libx264 (30-50 ms, always works)

### Decision 3: Bitrate Selection
**Result:** 2 Mbps as default, adaptive presets for other conditions
**Why:** Balances quality and accessibility
- 500 Kbps: Emergency/mobile fallback
- 1 Mbps: Mobile 4G
- **2 Mbps: Home internet (4.7× overhead on 10 Mbps)**
- 4 Mbps: Good quality (fiber/office)
- 8 Mbps: Maximum quality (LAN)

---

## Success Metrics

After H.264 implementation:

1. **Frame Size:** ✓ Reduce from 6.22 MB to 42 KB (147×)
2. **Bandwidth:** ✓ Fit 1080p60 on 10 Mbps home internet
3. **Latency:** ✓ End-to-end <100 ms (acceptable for video chat)
4. **Hardware:** ✓ Support GPU acceleration where available
5. **Compatibility:** ✓ Work on desktop, mobile, embedded, browser
6. **Fallback:** ✓ Graceful degradation if GPU unavailable

---

## Questions Answered

**Q: How much compression?**
A: 147× (6.22 MB → 42 KB per frame)

**Q: Will it work on my internet?**
A: H.264 @ 2 Mbps requires ≥2.5 Mbps upstream. Most connections work.

**Q: What if hardware not available?**
A: Fallback to libx264 software encoding (30-50 ms acceptable)

**Q: How does this compare to JPEG?**
A: H.264 is 6× smaller due to inter-frame compression

**Q: Why not VP9 or HEVC?**
A: Both require 100-200+ ms encoding per frame (impossible for 60fps real-time)

**Q: Will existing clients break?**
A: No, we support both RGB and H.264 codec paths

**Q: How long to implement?**
A: 45-65 hours (1.5-2 weeks full-time)

---

## References & Resources

### Standards
- H.264 spec: ITU-T H.264 | ISO/IEC 14496-10
- WebRTC H.264: RFC 7741 Appendix B
- HEVC spec: ITU-T H.265 | ISO/IEC 23008-2

### Libraries
- **libx264:** https://www.videolan.org/developers/x264.html
- **libyuv:** https://chromium.googlesource.com/libyuv/libyuv
- **NVIDIA NVENC:** https://developer.nvidia.com/nvidia-video-codec-sdk
- **FFmpeg:** https://ffmpeg.org/

### Real-Time Encoding
- FFmpeg H.264 guide: https://trac.ffmpeg.org/wiki/Encode/H.264
- libx264 presets: https://trac.ffmpeg.org/wiki/Encode/H.264#Preset
- WebRTC codecs: https://webrtc.org/blog/videocodecs

---

## Document Statistics

| Document | Lines | KB | Focus | Read Time |
|----------|-------|----|----|-----------|
| ENCODING_SUMMARY.md | 280 | 12 | Decision-making | 5 min |
| VIDEO_ENCODING_ANALYSIS.md | 410 | 18 | Technical details | 20 min |
| ENCODING_IMPLEMENTATION_GUIDE.md | 550 | 19 | Code walkthrough | 30 min |
| ENCODING_METRICS.md | 380 | 12 | Data tables | Reference |
| ENCODING_QUICK_REFERENCE.md | 280 | 8.6 | Charts/lookup | Reference |
| ENCODING_README.md | 187 | 9.6 | Navigation | 10 min |
| **Total** | **2,087** | **78.6** | - | **90 min** |

---

## Recommended Next Steps

1. **Immediate:** Read ENCODING_SUMMARY.md (5 minutes)
2. **Next:** Read VIDEO_ENCODING_ANALYSIS.md if technical questions
3. **Then:** Share ENCODING_SUMMARY.md with stakeholders
4. **Finally:** Assign engineer to ENCODING_IMPLEMENTATION_GUIDE.md

---

## Contact & Issues

For questions about specific aspects:

- **General question:** See ENCODING_SUMMARY.md
- **Technical question:** See VIDEO_ENCODING_ANALYSIS.md
- **Implementation question:** See ENCODING_IMPLEMENTATION_GUIDE.md
- **Data/metrics question:** See ENCODING_METRICS.md
- **Quick lookup:** See ENCODING_QUICK_REFERENCE.md

All documents cross-reference each other.

---

Last updated: February 19, 2026
