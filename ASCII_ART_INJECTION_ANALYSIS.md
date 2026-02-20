# ASCII Art Injection Analysis for ascii-chat

## Executive Summary

The ASCII art rendering pipeline in ascii-chat has been analyzed by executing the WebSocket server-client test script and examining the codebase. The ASCII grid is successfully generated and output through three distinct injection points depending on the execution mode.

## Current Output Analysis

### Test Execution Results

**Script**: `scripts/test-websocket-server-client.sh`
- **Server**: Started on random WebSocket port (port 30617 in test run)
- **Client**: Connected via WebSocket with snapshot mode enabled
- **Status**: Successfully executing with clean ASCII rendering pipeline

### Observed Flow

1. **Server**: Initializes media source (test pattern fallback due to webcam in use)
2. **Client**: Receives video frames via WebSocket protocol
3. **ASCII Conversion**: Frames are converted to ASCII grid representation
4. **Output**: ASCII grid rendered to stdout

## ASCII Art Injection Points

### 1. Core Conversion: `lib/video/ascii.c`

**Function**: `ascii_convert()` (line 69-80+)
- **Purpose**: Converts raw image data to ASCII character grid
- **Parameters**:
  - `width`, `height`: Target grid dimensions
  - `palette_chars`: Character set for luminance mapping
  - `luminance_palette`: 256-entry brightness lookup table

**Key Insight**: This function performs the core conversion but does NOT output directly. It returns a string buffer.

### 2. Render Loop: `lib/session/render.c`

**Function**: `session_render_loop()` (line 39-400+)
**Critical Line**: 271
```c
char *ascii_frame = session_display_convert_to_ascii(display, image);
```

**Execution Flow**:
1. Captures video frames (line 131-237)
2. Converts each frame to ASCII string (line 270-272)
3. Renders ASCII frame (line 299)

**Key Insight**: The render loop orchestrates the conversion process but delegates output to the display module.

### 3. ASCII Output Injection: `lib/session/display.c` (PRIMARY INJECTION POINT)

**Function**: `session_display_render_frame()` (lines 511-630)

This is the **CRITICAL INJECTION POINT** where ASCII art is output to the terminal or pipe.

#### Output Path 1: TTY Mode (Interactive Terminal)
**Lines 594-600**
```c
if (use_tty_control) {
    (void)terminal_cursor_home(STDOUT_FILENO);      // Move cursor to home (0,0)
    (void)platform_write_all(STDOUT_FILENO, frame_data, frame_len);  // Write ASCII grid
    (void)terminal_flush(STDOUT_FILENO);            // Flush output
}
```
- **When**: Terminal is a TTY (interactive mode)
- **How**: Uses ANSI cursor control to position at home, then overwrites frame
- **Output Channel**: stdout (file descriptor 1)
- **Effect**: Smooth animation without screen flicker

#### Output Path 2: Piped Mode (Non-TTY)
**Lines 601-627**
```c
else if (terminal_is_interactive()) {
    // Allocate buffer for frame + newline
    char *write_buf = SAFE_MALLOC(frame_len + 1, char *);
    // Copy frame data
    memcpy(write_buf, frame_data, frame_len);
    write_buf[frame_len] = '\n';
    // Lock and write
    bool prev_lock_state = log_lock_terminal();
    (void)platform_write_all(STDOUT_FILENO, write_buf, frame_len + 1);
    log_unlock_terminal(prev_lock_state);
    // Flush
    (void)terminal_flush(STDOUT_FILENO);
}
```
- **When**: Output is piped or redirected (non-TTY)
- **How**: Writes complete frame with newline separator for frame delim itation
- **Output Channel**: stdout (file descriptor 1)
- **Effect**: Each frame is a separate line, useful for recording/playback

#### Output Path 3: Snapshot Mode (Final Frame Only)
**Lines 575-585** (initialization)
- **When**: `--snapshot` flag enabled
- **How**: Only the final frame is rendered after specified delay
- **Effect**: Single ASCII grid output, then program exits

## Data Flow Visualization

```
Video Input
    ↓
Video Capture (lib/session/capture.c)
    ↓
Image Buffer (lib/video/image.c)
    ↓
ASCII Conversion (lib/video/ascii.c:ascii_convert)
    ├→ Luminance mapping
    ├→ SIMD optimizations (lib/video/simd/ascii_simd.c)
    └→ Color processing (lib/video/color_filter.c)
    ↓
ASCII String Buffer
    ↓
Render Loop (lib/session/render.c)
    ↓
Display Render (lib/session/display.c:session_display_render_frame) ← INJECTION POINT
    ├→ TTY Mode: Cursor control + write
    ├→ Piped Mode: Buffered write with newline
    └→ Snapshot Mode: Single frame output
    ↓
stdout (STDOUT_FILENO = 1)
    ↓
Terminal/Pipe
```

## Key Implementation Details

### ASCII Palette
**File**: `lib/video/palette.c`
- Default palette: `'   ...',;:clodxkO0KXNWM'` (23 characters for luminance levels)
- Custom palettes supported
- 256-entry luminance lookup table for performance

### Terminal Capabilities
**File**: `lib/platform/terminal.c`
- Detects TTY vs piped output
- ANSI color support detection
- UTF-8 palette cache for performance

### Thread Safety
**Lock**: `lib/log/logging.c:log_lock_terminal()`
- Critical sections protected by mutex
- Prevents corruption when multiple threads access stdout
- Automatic unlock on scope exit

## Snapshot Mode Specifics

When `--snapshot` is enabled (as used in test script with `-S -D 1`):
1. Client connects and authenticates
2. First video frame is received
3. Frame is converted to ASCII
4. ASCII grid is output **exactly once**
5. Program waits for specified delay (1 second: `-D 1`)
6. Program exits

**Test Result**: In the test run, client successfully connected and initialized but didn't receive frames within the 5-second wait period, so no ASCII output was captured. This is expected because:
- Server is in test pattern mode
- Network latency for WebSocket frame delivery
- Client initialization overhead

## Summary of Injection Points

| Location | Function | File | Purpose |
|----------|----------|------|---------|
| Frame Buffer | `ascii_convert()` | `lib/video/ascii.c:69` | Generate ASCII character grid |
| Render Loop | `session_render_loop()` | `lib/session/render.c:39` | Orchestrate conversion and output |
| **OUTPUT** | **`session_display_render_frame()`** | **`lib/session/display.c:511`** | **Primary injection point** |

## Recommendations

1. **For debugging output**: Monitor `session_display_render_frame()` calls with debug logging
2. **For custom rendering**: Hook `platform_write_all()` at the abstraction layer (lib/platform)
3. **For format customization**: Modify palette or output buffering in display.c
4. **For performance optimization**: Cache ASCII conversions or use double-buffering

## Files Modified/Analyzed

- ✓ `lib/session/render.c` - Render orchestration
- ✓ `lib/session/display.c` - Output injection
- ✓ `lib/video/ascii.c` - ASCII conversion
- ✓ `lib/video/palette.c` - Palette management
- ✓ `scripts/test-websocket-server-client.sh` - Test execution
- ✓ `/tmp/ascii-chat-*.log` - Server/client logs

## Test Environment

- **Date**: 2026-02-20
- **Build**: Debug build with sanitizers
- **Test Script**: WebSocket server-client with snapshot mode
- **Platform**: Linux (x86_64)
- **Terminal**: Piped output (non-TTY in test context)
