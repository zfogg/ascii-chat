# Generic Stream Resolution Design

**Date:** 2026-02-15
**Author:** Claude Code
**Status:** Approved

## Overview

Replace YouTube-specific URL handling with a generic stream resolver that supports any URL yt-dlp can handle, plus direct FFmpeg streams. The resolver intelligently routes between FFmpeg (for direct streams) and yt-dlp (for complex sites), with fallback between them.

## Problem Statement

Currently, ascii-chat only supports YouTube URLs via `youtube.c/h`. This limits users to YouTube and requires maintaining YouTube-specific code. The new design:
- Supports thousands of sites that yt-dlp handles
- Removes YouTube-specific code and options
- Allows arbitrary yt-dlp options via `--yt-dlp-options` flag
- Avoids unnecessary subprocess overhead for direct streams

## Architecture

### New Module: `lib/media/yt_dlp.c/h`

Encapsulates all yt-dlp subprocess logic. This is a reusable module for any yt-dlp extraction needs.

**Public API:**

```c
/**
 * Extract stream URL using yt-dlp with optional parameters
 *
 * @param url Input URL (YouTube, Twitch, direct streams, etc.)
 * @param yt_dlp_options Arbitrary yt-dlp options string
 *                       e.g., "--cookies-from-browser=chrome --no-warnings"
 *                       Pass NULL or "" for no extra options
 * @param output_url Output buffer for extracted stream URL
 * @param output_size Size of output buffer (recommend 8192+)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Errors are logged via log_error() before returning.
 * Extracted URLs are cached for 30 seconds.
 */
asciichat_error_t yt_dlp_extract_stream_url(
    const char *url,
    const char *yt_dlp_options,
    char *output_url,
    size_t output_size
);

/**
 * Check if yt-dlp is installed and accessible
 * @return true if yt-dlp --version succeeds
 */
bool yt_dlp_is_available(void);
```

**Implementation notes:**
- Move existing YouTube extraction logic from `youtube.c`
- Reuse 30-second cache from existing code
- Accept arbitrary `--yt-dlp-options` string and pass to subprocess
- Log errors via `log_error()` when yt-dlp subprocess fails

### Updated Module: `lib/media/source.c`

Add smart routing function to determine which resolver to use first:

```c
/**
 * Resolve URL to playable stream URL
 *
 * Routing logic:
 * - If URL has FFmpeg-native extension or is rtsp:// / rtmp:// → try FFmpeg first
 * - Else (looks like webpage) → try yt-dlp first
 * - If first fails → try the other as fallback
 * - If both fail → return error with full context
 *
 * @param url Input URL
 * @param yt_dlp_options Optional yt-dlp options from --yt-dlp-options flag
 * @param output_url Output buffer for resolved stream URL
 * @param output_size Size of output buffer
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t media_source_resolve_url(
    const char *url,
    const char *yt_dlp_options,
    char *output_url,
    size_t output_size
);
```

**Resolution algorithm:**

1. Check cache (existing 30-second cache)
2. Detect if URL is "direct stream":
   - File extension is FFmpeg-native (.mp4, .mkv, .webm, .mov, .avi, .flv, .ogg, .m3u8, .mp3, .wav, .flac, etc.)
   - OR protocol is rtsp:// or rtmp://
3. Route based on type:
   - **Direct stream**: Try FFmpeg probe → if fails, fall back to yt-dlp
   - **Complex site** (no extension, unknown extension): Try yt-dlp → if fails, fall back to FFmpeg
4. Log errors at each failure point with context
5. Cache successful result (30 seconds)

**Error logging flow:**
- `log_debug()` when starting each resolution attempt
- `log_debug()` on probe success (FFmpeg can handle)
- `log_error()` when each resolver fails
- `log_error()` with full URL if all methods fail

### Removed Files

- `lib/media/youtube.c` - replaced by yt_dlp.c
- `include/ascii-chat/media/youtube.h` - replaced by yt_dlp.h
- `tests/unit/youtube_test.c` - replaced by yt_dlp_test.c

### Removed Options

- `--cookies-from-browser` - use `--yt-dlp-options="--cookies-from-browser=..."`
- `--no-cookies-from-browser` - use `--yt-dlp-options="--no-cookies-from-browser"`

### New Option

```
--yt-dlp-options="OPTION_STRING"
```

Passes arbitrary yt-dlp options to the subprocess. Examples:
- `--yt-dlp-options="--cookies-from-browser=chrome"`
- `--yt-dlp-options="--no-cookies-from-browser --embed-subs"`
- `--yt-dlp-options="--proxy socks5://127.0.0.1:1080"`

This option is global (not mode-specific) and applies to all stream resolution attempts.

## FFmpeg-Native Formats

When detecting "direct streams", check for these file extensions (case-insensitive):

**Video containers:** mp4, mkv, webm, mov, avi, flv, ogv, ts, m2ts, mts, mxf, gxf, 3gp, 3g2, f4v, asf, wmv, quicktime, matroska

**Audio containers:** ogg, oga, wma, wav, flac, aac, m4a, m4b, mp3, aiff, au

**Streaming:** m3u8 (HLS), mpd (DASH)

**Streaming protocols:** rtsp://, rtmp://

## Error Handling

Each failure point logs an error with context:

```
Direct stream attempt:
  FFmpeg probe failed → log_debug("FFmpeg probe failed for direct stream, falling back to yt-dlp")

yt-dlp attempt:
  Subprocess failed → log_error("yt-dlp extraction failed: [error details]")

FFmpeg fallback attempt:
  Probe failed → log_error("FFmpeg fallback also failed")

Final failure:
  log_error("Failed to resolve URL - all extraction methods failed: [url]")
  return ERROR_STREAM_RESOLUTION_FAILED
```

User sees the final error message clearly indicating what went wrong.

## Usage Examples

### YouTube with cookies
```bash
./ascii-chat mirror --yt-dlp-options="--cookies-from-browser=chrome"
```

### Direct MP4 stream (no yt-dlp subprocess overhead)
```bash
./ascii-chat mirror "https://example.com/video.mp4"
# Tries FFmpeg directly, bypasses yt-dlp
```

### Complex site with custom proxy
```bash
./ascii-chat mirror --yt-dlp-options="--proxy socks5://127.0.0.1:1080"
```

### Direct RTSP stream
```bash
./ascii-chat mirror rtsp://camera.local/stream
# Tries FFmpeg directly, bypasses yt-dlp
```

## Testing Strategy

1. **Unit tests** (`tests/unit/yt_dlp_test.c`):
   - Test yt-dlp extraction with various URL types
   - Test cache hit/miss
   - Test error handling and logging
   - Mock yt-dlp subprocess failures

2. **Integration tests**:
   - Test source.c resolution with direct streams
   - Test source.c resolution with complex sites
   - Test fallback behavior
   - Test FFmpeg-native format detection

3. **Manual testing**:
   - YouTube URL with various options
   - Direct HTTP video (.mp4, .mkv, .webm)
   - RTSP stream
   - Invalid URLs
   - yt-dlp unavailable scenario

## Migration Guide

For users:
- Replace `--cookies-from-browser=chrome` with `--yt-dlp-options="--cookies-from-browser=chrome"`
- Replace `--no-cookies-from-browser` with `--yt-dlp-options="--no-cookies-from-browser"`
- All other functionality remains the same, now supports any URL yt-dlp handles

For developers:
- `youtube_is_youtube_url()` → no longer needed, routing handled in source.c
- `youtube_extract_stream_url()` → use `media_source_resolve_url()` instead
- Direct yt-dlp calls → use `yt_dlp_extract_stream_url()` from yt_dlp.c

## Future Extensibility

The `yt_dlp.c/h` module is reusable for any future yt-dlp-based features:
- Format selection
- Subtitle extraction
- Quality preferences
- etc.

The routing logic in source.c can be extended to support other resolvers (ffmpeg native support, direct HTTP streaming, etc.).
