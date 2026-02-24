/**
 * @file mirror/main.h
 * @ingroup mirror
 * @brief ascii-chat Mirror Mode Interface
 *
 * Defines the entry point for local media mirror mode, which allows users to view
 * webcams and media files as ASCII art directly in the terminal. Mirror mode
 * requires no server connection or network setup - it's a self-contained video viewer.
 *
 * ## Overview
 *
 * Mirror mode uses a unified session library that abstracts common initialization
 * patterns (media source selection, terminal capability detection, audio setup, etc.).
 * The mode-specific code only needs to:
 * - Define a run callback that delegates to the unified render loop
 * - Provide a keyboard handler for interactive controls
 * - Let the session library handle all initialization and cleanup
 *
 * ## Media Source Support
 *
 * Mirror mode can play from any source:
 * - **Webcam**: Live capture with automatic device detection
 * - **Local Files**: MP4, MKV, WebM, AVI, MOV, FLV, GIF, JPEG, PNG, etc.
 * - **Streaming URLs**: HTTP(S), RTSP, RTMP, HLS, DASH streams
 * - **Streaming Sites**: YouTube, TikTok, Twitch, Reddit, Instagram (via yt-dlp)
 * - **Standard Input**: Custom frame input for programmatic use
 * - **Test Patterns**: Synthetic frames for debugging without media
 *
 * ## Frame Processing
 *
 * Each frame follows this pipeline:
 * 1. **Capture**: Raw video from media source
 * 2. **ASCII Conversion**: Luminance mapping to ASCII palette with color codes
 * 3. **Terminal Rendering**: Optimized output with cursor management
 * 4. **Frame Rate Control**: Adaptive sleep to maintain target FPS
 * 5. **Keyboard Input**: Optional interactive controls
 *
 * ## Usage
 *
 * Mirror mode is invoked as a standalone binary mode:
 *
 * ```bash
 * # Webcam (live)
 * ascii-chat mirror
 *
 * # Video file
 * ascii-chat mirror --file video.mp4 --loop
 *
 * # Streaming URL
 * ascii-chat mirror --url "https://www.youtube.com/watch?v=..."
 *
 * # With custom settings
 * ascii-chat mirror --file video.mp4 --fps 30 --palette compact --enable-audio
 * ```
 *
 * ## Architecture
 *
 * ```
 * mirror_main()
 *   ↓
 * session_client_like_run()
 *   ├─ Initialize media source
 *   ├─ Probe FPS
 *   ├─ Setup terminal display
 *   ├─ Initialize audio (optional)
 *   ├─ Show splash screen
 *   └─ Call mirror_run() callback
 *       ↓
 *       session_render_loop()
 *         ├─ Capture frames
 *         ├─ Convert to ASCII
 *         ├─ Render to terminal
 *         ├─ Handle keyboard input
 *         └─ Maintain frame rate
 * ```
 *
 * ## Keyboard Controls
 *
 * When keyboard support is enabled:
 * - `q` / `Ctrl+C`: Quit
 * - `h`: Toggle help screen
 * - `p` / `Space`: Pause/resume playback
 * - `n`: Next frame (when paused)
 * - Arrow keys: Seek in video (if supported)
 *
 * ## Color Support
 *
 * Terminal color capabilities are automatically detected:
 * - **True Color** (24-bit): Best visual quality on modern terminals
 * - **256-Color**: Good quality on most SSH sessions and older terminals
 * - **16-Color**: Basic color mode on limited terminals
 * - **Monochrome**: Grayscale rendering on color-disabled terminals
 *
 * ## Audio Support
 *
 * Audio can come from:
 * - Media file (if video has audio track)
 * - Microphone (fallback if file has no audio)
 * - External audio device (configured via platform audio subsystem)
 *
 * Audio playback is optional and can be disabled with `--no-audio`.
 *
 * ## Performance Tuning
 *
 * Control performance with:
 * - `--fps N`: Target frame rate (default 60)
 * - `--palette compact`: Use more efficient ASCII palette
 * - `--snapshot`: Single frame only (exits immediately)
 * - `--grep`: Log filtering to reduce console overhead
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 *
 * @see src/mirror/main.c - Implementation details
 * @see src/common/session/render.h - Frame rendering pipeline
 * @see src/common/session/capture.h - Media source abstraction
 * @see src/common/session/display.h - Terminal output
 * @see lib/media/source.h - Media source types
 */

#pragma once

/**
 * @brief Run mirror mode main loop
 *
 * Entry point for mirror mode. Initializes media source (webcam or file),
 * sets up terminal display, and then continuously captures frames, converts
 * them to ASCII art, and displays them locally. The function runs until the
 * user presses Ctrl+C, the media reaches EOF, or an error occurs.
 *
 * The function uses the unified session library for initialization:
 * - Detects and opens media source (webcam, file, URL, test pattern)
 * - Probes frame rate automatically
 * - Initializes terminal with capability detection
 * - Sets up audio if available and enabled
 * - Shows splash screen animation
 * - Runs the main render loop (frame capture → ASCII conversion → display)
 * - Performs proper cleanup on exit
 *
 * All settings are read from command-line options (e.g., --file, --url, --fps, etc.).
 * No parameters are required - configuration is global.
 *
 * ## Return Value
 *
 * - `0`: Success (Ctrl+C, EOF, or snapshot complete)
 * - Non-zero: Error code (ASCIICHAT_OK is 0, other codes indicate failure)
 *
 * ## Example Flow
 *
 * ```
 * $ ascii-chat mirror --file video.mp4 --enable-audio
 *
 * 1. Media source created (FFmpeg decoder for video.mp4)
 * 2. FPS detected (e.g., 24 FPS)
 * 3. Terminal initialized (e.g., 256-color palette)
 * 4. Audio initialized (from video.mp4)
 * 5. Splash screen shown (5 seconds with animation)
 * 6. Render loop starts:
 *    - Read frame from FFmpeg
 *    - Convert to ASCII (luminance + color codes)
 *    - Render to terminal
 *    - Sleep to maintain 24 FPS
 *    - Poll keyboard for 'q' to quit
 * 7. On EOF or Ctrl+C:
 *    - Destroy display, audio, media source
 *    - Return 0
 * ```
 *
 * @return 0 on success (clean exit or Ctrl+C)
 * @return Non-zero error code on failure
 *
 * @ingroup mirror
 *
 * @see src/mirror/main.c - Full implementation
 * @see src/common/session/client_like.h - Shared initialization
 */
int mirror_main(void);
