/**
 * @file client/capture.h
 * @ingroup client_capture
 * @brief ascii-chat Client Media Capture Management Interface
 *
 * Defines the interface for video capture and transmission management in the
 * ascii-chat client. Abstracts over multiple media sources (webcam, files,
 * stdin, test patterns) and handles frame processing, resizing, and FPS sync.
 *
 * The capture subsystem:
 * - Manages platform-specific webcam access via abstraction layer
 * - Supports multiple media sources (webcam, local files, stdin, test patterns)
 * - Performs frame resizing and format conversion for network efficiency
 * - Synchronizes frame transmission with configurable FPS rates
 * - Handles periodic frame transmission to the server
 *
 * ## Media Source Types
 *
 * The capture subsystem supports four media source types via the media_source_t
 * abstraction (see @ref media):
 *
 * - **WEBCAM**: Hardware camera device (webcam/camera)
 *   - Platform-specific access (V4L2 on Linux, AVFoundation on macOS)
 *   - Real-time capture with variable frame rate
 *   - Selected via `--webcam-index <N>` (default: 0, first camera)
 *
 * - **FILE**: Local media file or network stream
 *   - Supported formats: mp4, avi, mkv, webm, mov, flv, wmv, gif
 *   - Specified via `--file <path>` option
 *   - Playback at configurable frame rate
 *   - Loop support via `--media-loop` option
 *
 * - **STDIN**: Piped or redirected media data
 *   - Specified via `--file -` (dash indicates stdin)
 *   - Any format supported by FFmpeg (detected automatically)
 *   - Useful for streaming pipes: `ffmpeg ... | ascii-chat client --file -`
 *   - Does not support seeking/rewinding
 *
 * - **TEST**: Procedural test pattern generator
 *   - Enabled via `--test-pattern` option
 *   - Generates animated test pattern without hardware
 *   - Useful for testing without webcam access
 *   - Always available (no hardware requirements)
 *
 * Media source selection is automatic based on command-line options:
 * @code
 * ./ascii-chat client --test-pattern           # Use test pattern
 * ./ascii-chat client --file video.mp4         # Play file
 * ./ascii-chat client --file -                 # Read from stdin
 * ./ascii-chat client --webcam-index 0         # Use default camera
 * ./ascii-chat client                          # Auto-detect (webcam or test)
 * @endcode
 *
 * ## Frame Format and Negotiation
 *
 * All media sources are normalized to a common frame format:
 *
 * - **Color Space**: RGB (24-bit) - three bytes per pixel (R, G, B)
 * - **Byte Order**: Platform-native (little-endian on x86/ARM)
 * - **Frame Dimensions**: Variable (see Resize Behavior below)
 * - **Pixel Layout**: Continuous row-major array (no padding between rows)
 *
 * Frame format is opaque to the caller - the media source abstraction handles
 * all conversions. Consumers always receive RGB frames ready to use:
 *
 * @code
 * // Get frame from any media source (format already normalized)
 * image_t *frame = media_source_read_video(source);
 *
 * // Frame is guaranteed to be:
 * // - RGB format (8-bit per channel)
 * // - Dimensions: frame->width x frame->height pixels
 * // - Memory: frame->data[y * frame->width * 3 + x * 3 + channel]
 * @endcode
 *
 * Different input types undergo conversion:
 * - **WEBCAM**: V4L2/AVFoundation -> RGB (platform layer conversion)
 * - **FILE/STDIN**: FFmpeg decoder -> RGB (FFmpeg handles all codecs)
 * - **TEST**: Generated directly as RGB (no conversion needed)
 *
 * ## Frame Resizing and Aspect Ratio
 *
 * Captured frames are resized for network efficiency while preserving aspect ratio:
 *
 * - **Maximum Dimensions**: 800x600 pixels (default) - reasonable bandwidth
 * - **Aspect Ratio**: Original aspect ratio preserved via fit-to-bounds
 * - **Scaling Algorithm**: Letterboxing - frame fits within bounds with padding
 * - **Padding Color**: Black (0, 0, 0) for empty regions
 *
 * Resize process:
 * 1. Calculate scale factor to fit frame within 800x600
 * 2. Compute new dimensions maintaining aspect ratio
 * 3. Scale frame using resizing algorithm (maintains quality)
 * 4. Add black padding to fill remaining space (if needed)
 *
 * Example resizes:
 * - 1920x1440 (4:3) → 800x600 (fits exactly)
 * - 1280x720 (16:9) → 800x450 (with 75px black bars top/bottom)
 * - 640x480 (4:3) → 640x480 (no resize, smaller than max)
 * - 3840x2160 (16:9) → 800x450 (same 16:9 aspect)
 *
 * Frame resizing options (future):
 * - `--resize-width <W>` - Custom max width (default: 800)
 * - `--resize-height <H>` - Custom max height (default: 600)
 * - `--no-resize` - Disable resizing (use original dimensions)
 *
 * ## Frame Rate Synchronization (FPS Control)
 *
 * The capture system implements frame rate limiting for bandwidth efficiency
 * and smooth playback:
 *
 * - **Capture Rate**: Up to 144 FPS (high refresh rate support)
 * - **Target FPS**: Configurable via `--fps <rate>` (default: 60)
 * - **Range**: 1 to 144 FPS (1 = slowest, 144 = fastest)
 * - **Timing Method**: Monotonic clock for accuracy
 * - **Adaptive Delay**: Dynamic sleep adjustment for consistent frame intervals
 *
 * FPS configuration:
 * @code
 * ./ascii-chat client --fps 30               # 30 FPS (slower, less bandwidth)
 * ./ascii-chat client --fps 60               # 60 FPS (default, balanced)
 * ./ascii-chat client --fps 144              # 144 FPS (max, high refresh)
 * @endcode
 *
 * FPS behavior by source type:
 * - **WEBCAM**: Captures at hardware rate, then throttles to target FPS
 * - **FILE**: Plays at media file's native FPS, then throttles to target
 * - **STDIN**: Decodes frames, then throttles to target FPS
 * - **TEST**: Generates at target FPS directly (no throttling needed)
 *
 * Frame rate calculation:
 * 1. Calculate frame interval: interval_ms = 1000 / fps
 * 2. Record capture start time (monotonic clock)
 * 3. Capture and process frame
 * 4. Calculate elapsed time
 * 5. Sleep for remaining interval to achieve consistent rate
 *
 * Example timing (60 FPS target = 16.67ms per frame):
 * - Frame start: 0ms
 * - Capture: 5ms
 * - Process: 2ms
 * - Elapsed: 7ms
 * - Sleep: 9.67ms (to reach 16.67ms total)
 * - Frame complete: 16.67ms ✓ (on schedule)
 *
 * ## Capture Lifecycle
 *
 * 1. **Initialization** (capture_init): Set up platform-specific webcam drivers
 * 2. **Thread Start** (capture_start_thread): Spawn capture thread in connection loop
 * 3. **Capture Loop**: Continuously grab frames and send to server
 * 4. **Thread Stop** (capture_stop_thread): Signal thread to exit and wait
 * 5. **Cleanup** (capture_cleanup): Release platform resources
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 *
 * @see topic_client_capture "Video Capture Architecture"
 * @see media/source.h "Media Source Abstraction"
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Initialize capture subsystem
 *
 * Initializes the media source and capture resources. Determines media source type
 * from command-line options, then sets up platform-specific drivers and buffers.
 * This must be called before any other capture functions.
 *
 * The initialization process:
 * 1. Determines media source type from options (test pattern, file, stdin, or webcam)
 * 2. Creates appropriate media source (media_source_create())
 * 3. For WEBCAM: Initializes platform driver (V4L2/AVFoundation/Media Foundation)
 * 4. For FILE/STDIN: Opens FFmpeg decoder with format detection
 * 5. For TEST: Initializes procedural test pattern generator
 * 6. Initializes frame buffers and capture parameters
 *
 * **Media source selection** (in order):
 * 1. If `--test-pattern` → MEDIA_SOURCE_TEST
 * 2. Else if `--file -` → MEDIA_SOURCE_STDIN
 * 3. Else if `--file <path>` → MEDIA_SOURCE_FILE
 * 4. Else if `--url <url>` → MEDIA_SOURCE_FILE (network URL)
 * 5. Else → MEDIA_SOURCE_WEBCAM (with --webcam-index)
 *
 * **Options affecting initialization**:
 * - `--test-pattern`: Use synthetic test pattern (any platform, no hardware needed)
 * - `--file <path>`: Media file to play (mp4, avi, mkv, gif, etc.)
 * - `--file -`: Read media from stdin (any FFmpeg format)
 * - `--url <url>`: Network stream (HTTP/HTTPS/RTSP/YouTube/etc.)
 * - `--webcam-index <N>`: Which camera to open (default: 0, first camera)
 * - `--media-loop`: Loop file playback when EOF reached
 * - `--webcam-flip`: Flip video vertically (webcam and test patterns only)
 * - `--fps <rate>`: Target frame rate in FPS (1-144, default: 60)
 *
 * **Error handling**:
 * - WEBCAM: Device not found, driver error, device busy
 * - FILE: File not found, unsupported format, FFmpeg error
 * - STDIN: FFmpeg format detection failure
 * - TEST: Rarely fails (only if display dimensions unavailable)
 *
 * @return ASCIICHAT_OK on success, error code on failure:
 *         - ERROR_WEBCAM: Device not found or driver error
 *         - ERROR_WEBCAM_IN_USE: Device busy (another app using it)
 *         - ERROR_FILE_NOT_FOUND: Media file not found
 *         - ERROR_FORMAT: Unsupported media format
 *         - ERROR_NOT_SUPPORTED: Platform doesn't support this source type
 *
 * @note This is called during client initialization, before entering the connection loop.
 * @note The actual frame capture happens in the capture thread (capture_start_thread).
 * @note Frame resizing to 800x600 (max) happens during capture, not initialization.
 *
 * @ingroup client_capture
 *
 * @see media_source_create "Media source creation"
 * @see capture_start_thread "Start capture thread"
 * @see capture_cleanup "Cleanup resources"
 */
int capture_init();

/**
 * @brief Start capture thread
 *
 * Spawns the capture thread in the global client worker thread pool.
 * The thread runs the main capture loop, continuously acquiring frames from
 * the media source, processing them (resize, format conversion), and transmitting
 * to the server with FPS rate limiting.
 *
 * The capture loop implements this pipeline:
 *
 * **Frame Acquisition:**
 * 1. Read frame from media source (media_source_read_video())
 * 2. Verify frame is valid (non-null, valid dimensions)
 * 3. Measure acquisition time for timing/debug
 *
 * **Frame Processing (Resize & Format):**
 * 4. Calculate optimal resize dimensions (fit-to-bounds, aspect ratio preserved)
 * 5. Scale frame to max 800x600 using high-quality resizing
 * 6. Format frame as RGB (already normalized by media source)
 * 7. Add black padding if frame smaller than target area
 *
 * **FPS Synchronization:**
 * 8. Calculate frame interval from target FPS (e.g., 60 FPS = 16.67ms)
 * 9. Measure actual processing time (acquisition + resize)
 * 10. Sleep for remaining interval to maintain target FPS
 * 11. Account for monotonic clock drift and OS scheduling
 *
 * **Network Transmission:**
 * 12. Optionally compress frame if --compress is enabled
 * 13. Serialize frame to PACKET_TYPE_IMAGE_FRAME format (width + height + pixels)
 * 14. Send packet to server via send_packet_to_server()
 * 15. Log transmission metrics (size, latency, dropped frames)
 *
 * **Loop Control:**
 * 16. Check if in snapshot mode and should exit (--snapshot mode)
 * 17. Check for shutdown signal (capture_stop_thread() called)
 * 18. Check for connection loss (reconnect trigger)
 * 19. Loop back to step 1
 *
 * **FPS Behavior:**
 * - Target FPS from `--fps` option (default: 60)
 * - Achieved via sleep-based rate limiting (monotonic clock)
 * - Maintains consistent frame intervals even if processing takes variable time
 * - Adapts to system load and media source frame availability
 *
 * Must be called after server connection is established and protocol thread started.
 *
 * @return ASCIICHAT_OK on success, error code on failure:
 *         - ERROR_THREAD: Thread pool full or system thread creation failed
 *         - ERROR_NO_MEMORY: Insufficient memory for thread resources
 *
 * @note Must be paired with capture_stop_thread() to properly clean up.
 * @note Frame resizing and FPS limiting are handled transparently by the capture thread.
 * @note Rate limiting is frame-by-frame, not based on network round-trip time.
 *
 * @ingroup client_capture
 *
 * @see capture_stop_thread "Stop capture thread"
 * @see capture_thread_exited "Check if thread has exited"
 * @see media_source_read_video "Frame acquisition from media source"
 */
int capture_start_thread();

/**
 * @brief Stop capture thread
 *
 * Signals the capture thread to exit and waits for it to join cleanly.
 * Called during connection loss handling or shutdown.
 *
 * The function:
 * 1. Sets shutdown flag
 * 2. Polls capture_thread_exited() until thread exits
 * 3. Releases thread-specific resources
 *
 * Must be called before reconnection to prevent resource leaks.
 *
 * @note This function blocks until the capture thread exits.
 *       Typical wait time: <1 second (one frame interval).
 *
 * @ingroup client_capture
 *
 * @see capture_start_thread "Start capture thread"
 */
void capture_stop_thread();

/**
 * @brief Check if capture thread has exited
 *
 * Atomically checks whether the capture thread has finished execution.
 * The main thread uses this to wait for clean thread shutdown before
 * reconnection or final cleanup.
 *
 * @return true if thread exited, false otherwise
 *
 * @note The main thread typically polls this in a 10-100ms loop until true.
 *
 * @ingroup client_capture
 *
 * @see capture_stop_thread "Stop capture thread"
 */
bool capture_thread_exited();

/**
 * @brief Cleanup capture subsystem
 *
 * Releases all platform resources associated with webcam access.
 * This must be called after capture_stop_thread() and before process exit.
 *
 * The cleanup process:
 * 1. Closes webcam device (platform-specific close)
 * 2. Frees frame buffers
 * 3. Releases platform driver resources
 * 4. Cleans up thread handles
 *
 * Must be called in proper order during shutdown to avoid resource leaks
 * and to restore the platform to a clean state.
 *
 * @ingroup client_capture
 *
 * @see capture_init "Initialize capture subsystem"
 */
void capture_cleanup();
