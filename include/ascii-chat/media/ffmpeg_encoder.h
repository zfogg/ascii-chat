/**
 * @file media/ffmpeg_encoder.h
 * @ingroup media
 * @brief FFmpeg video/image file encoder — codec selected from file extension
 *
 * @section ffmpeg_encoder_overview Overview
 *
 * The FFmpeg encoder provides a simple interface for encoding video frames or images
 * to disk. The encoder automatically detects the output format and codec from the file
 * extension, handling all necessary color space conversions and encoder configuration.
 *
 * **Input format:** RGB24 (8-bit per channel, 24-bit per pixel)
 * **Output:** File on disk with automatic format/codec detection
 *
 * @section ffmpeg_encoder_formats Supported Formats & Codecs
 *
 * | File Extension | Container | Video Codec | Pixel Format | Use Case | Characteristics |
 * |---|---|---|---|---|---|
 * | .mp4, .mov | MP4 | H.264 (libx264) | YUV420P | General streaming | Wide compatibility, moderate compression |
 * | .webm | WebM | VP9 (libvpx-vp9) | YUV420P | Web streaming | Better quality/bitrate ratio, larger file size |
 * | .avi | AVI | MPEG-4 | YUV420P | Legacy video | Larger files, older compatibility |
 * | .gif | GIF | GIF | Palette (8-bit) | Animation loops | Lossless, limited colors, good for short clips |
 * | .png | Image | PNG | RGB24 | Still images | Lossless, no video stream, single frame output |
 * | .jpg, .jpeg | Image | MJPEG | YUVJ420P | Still images | Lossy compression, single frame output |
 *
 * **Default format:** MP4 (if extension not recognized)
 *
 * @section ffmpeg_encoder_options Encoder Options & Quality Tradeoffs
 *
 * **Automatic Bitrate Calculation:**
 * - Based on resolution: ~1 Mbps per megapixel
 * - Range: 500 kbps (minimum) to 5000 kbps (maximum)
 * - Formula: `bitrate_kbps = (width × height) / 1024`, clamped to [500, 5000]
 * - Provides reasonable quality for most use cases without manual tuning
 *
 * **Color Space Conversion:**
 * - Input: RGB24 (24-bit color, standard in graphics/video capture)
 * - Conversion method: libswscale with SWS_BICUBIC algorithm
 * - SWS_BICUBIC: Bicubic interpolation offering good quality-to-speed balance
 * - Pixel format selection:
 *   - YUV420P: Video formats (H.264, VP9, MPEG-4) — standard ITU-R BT.601 conversion
 *   - YUVJ420P: JPEG-based formats (MJPEG) — uses full-range YUV (0-255)
 *   - RGB24: Lossless formats (PNG, GIF) — preserves exact RGB values
 *
 * **Quality vs. Performance:**
 * - H.264: Best compression, moderate speed (suitable for streaming)
 * - VP9: Better quality/size ratio, slower encoding (smaller files)
 * - PNG: Lossless, larger files, very slow encoding (use for critical frames)
 * - GIF: Lossless animation, severely limited palette (max 256 colors)
 * - MJPEG: Fast single-frame capture, reasonable compression
 *
 * **Single-Frame Image Handling:**
 * - PNG and JPEG formats (`.png`, `.jpg`, `.jpeg`) output only one frame
 * - Multiple ffmpeg_encoder_write_frame() calls write subsequent frames to the same file
 * - Useful for snapshots or periodic frame capture
 *
 * @section ffmpeg_encoder_usage Usage Examples
 *
 * **Encode video to MP4 (default format):**
 * @code
 * ffmpeg_encoder_t *enc;
 * asciichat_error_t err = ffmpeg_encoder_create("output.mp4", 1920, 1080, 30, &enc);
 * if (err != ASCIICHAT_OK) {
 *     // Handle error
 * }
 *
 * // Write frames (RGB24 format, pitch in bytes per row)
 * uint8_t *frame_rgb = get_frame_data();
 * int pitch = 1920 * 3;  // 1920 pixels × 3 bytes per pixel
 * ffmpeg_encoder_write_frame(enc, frame_rgb, pitch);
 *
 * // Cleanup and finalize file
 * ffmpeg_encoder_destroy(enc);
 * @endcode
 *
 * **High-quality WebM encoding (better compression than H.264):**
 * @code
 * // Simply use .webm extension
 * ffmpeg_encoder_t *enc;
 * ffmpeg_encoder_create("output.webm", 1920, 1080, 24, &enc);
 * // ... write frames ...
 * ffmpeg_encoder_destroy(enc);
 * @endcode
 *
 * **Capture single frame to PNG (lossless snapshot):**
 * @code
 * ffmpeg_encoder_t *enc;
 * ffmpeg_encoder_create("snapshot.png", 1920, 1080, 1, &enc);
 * uint8_t *frame = capture_frame();
 * ffmpeg_encoder_write_frame(enc, frame, 1920 * 3);
 * ffmpeg_encoder_destroy(enc);  // Finalizes and closes file
 * @endcode
 *
 * **Animated GIF from frame sequence:**
 * @code
 * ffmpeg_encoder_t *enc;
 * ffmpeg_encoder_create("animation.gif", 640, 480, 10, &enc);  // 10 FPS
 * for (int i = 0; i < num_frames; i++) {
 *     uint8_t *frame = get_frame(i);
 *     ffmpeg_encoder_write_frame(enc, frame, 640 * 3);
 * }
 * ffmpeg_encoder_destroy(enc);  // Finalizes GIF with palette
 * @endcode
 *
 * @section ffmpeg_encoder_pitfalls Common Pitfalls & Best Practices
 *
 * **1. Pitch/Stride Parameter:**
 * - `pitch` is the byte offset from one row to the next
 * - For tightly-packed RGB: `pitch = width × 3`
 * - For aligned/padded buffers: use actual row size (may be larger)
 * - Must match the actual memory layout of the input buffer
 *
 * **2. Memory Management:**
 * - Input frame data is NOT owned by encoder — caller must manage
 * - Output file is owned by encoder and written/closed by ffmpeg_encoder_destroy()
 * - All resources freed in ffmpeg_encoder_destroy() — always call it
 *
 * **3. Error Recovery:**
 * - Encoder is NOT usable after any error from ffmpeg_encoder_write_frame()
 * - Must call ffmpeg_encoder_destroy() to clean up and finalize file
 * - If file operations fail, file may be partially written
 *
 * **4. File Extension Sensitivity:**
 * - Extension matching is case-sensitive on Unix-like systems
 * - Use lowercase extensions (.mp4 not .MP4) for maximum compatibility
 * - Unknown extension defaults to MP4 H.264
 *
 * **5. Platform Support:**
 * - This encoder is NOT AVAILABLE on Windows (_WIN32)
 * - Only POSIX systems (Linux, macOS) are supported
 *
 * @section ffmpeg_encoder_performance Performance Considerations
 *
 * **Encoding Speed (relative):**
 * - Fastest: GIF, MJPEG (lossless single-frame)
 * - Fast: H.264 with default bitrate
 * - Medium: VP9 (better compression at cost of speed)
 * - Slow: PNG (lossless color preservation)
 *
 * **File Size (for 1080p @ 30fps, 10 seconds):**
 * - MP4 H.264: ~50-100 MB (depends on bitrate)
 * - WebM VP9: ~30-60 MB (better compression)
 * - AVI MPEG-4: ~100-150 MB (less efficient compression)
 * - PNG: ~100+ MB per frame (lossless, single frame only)
 * - GIF: ~200+ MB for 300 frames (palette limited)
 *
 * **Bitrate Impact on Quality:**
 * - Default bitrate is calculated for acceptable quality at various resolutions
 * - For critical content (e.g., document sharing), consider PNG for single frames
 * - For streaming, MP4 H.264 offers best balance of quality, compatibility, and size
 * - VP9 recommended for highest quality requirements when file size isn't critical
 */
#pragma once
#ifndef _WIN32
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct ffmpeg_encoder_s ffmpeg_encoder_t;

/**
 * Create and initialize a video/image encoder.
 *
 * The output format and codec are automatically determined from the file extension.
 * See @ref ffmpeg_encoder_formats for supported formats.
 *
 * **Bitrate:** Automatically calculated from resolution (~1 Mbps per megapixel, clamped to 500-5000 kbps).
 *
 * **Color Space:** Input is expected in RGB24 format. Internal conversion to the appropriate
 * output pixel format (YUV420P for video, RGB24 for PNG, YUVJ420P for JPEG) is handled automatically.
 *
 * @param output_path Path to the output file (extension determines format and codec)
 * @param width_px    Video width in pixels (must be > 0)
 * @param height_px   Video height in pixels (must be > 0)
 * @param fps         Frames per second (must be > 0)
 * @param[out] out    Pointer to encoder handle (set on success)
 *
 * @return ASCIICHAT_OK on success, error code otherwise
 *
 * @note The encoder acquires resources (file handles, memory buffers) that must be
 *       released with ffmpeg_encoder_destroy(). Failure to do so may leak file handles
 *       and memory.
 *
 * @see ffmpeg_encoder_write_frame, ffmpeg_encoder_destroy, ffmpeg_encoder_formats
 */
asciichat_error_t ffmpeg_encoder_create(const char *output_path,
                                        int width_px, int height_px,
                                        int fps, ffmpeg_encoder_t **out);

/**
 * Write a single video frame to the output file.
 *
 * The frame must be in RGB24 format (8-bit per channel, 24-bit per pixel).
 * Internal color space conversion is performed automatically based on the
 * output format (see @ref ffmpeg_encoder_options for details).
 *
 * **Pitch Parameter:**
 * The `pitch` parameter specifies the byte offset from one row to the next.
 * For tightly-packed RGB data: `pitch = width_px × 3`.
 * For padded/aligned buffers: `pitch` must reflect the actual row size.
 *
 * @param enc   Encoder handle (created with ffmpeg_encoder_create())
 * @param rgb   Pointer to frame data in RGB24 format (not copied, used immediately)
 * @param pitch Byte offset between rows (must match actual buffer layout)
 *
 * @return ASCIICHAT_OK on success, error code otherwise
 *
 * @note Input frame data is NOT owned by the encoder. The caller must ensure
 *       the buffer remains valid throughout the call.
 *
 * @note For image-only formats (PNG, JPEG), only the last frame written is kept.
 *       Previous frames are discarded when the next frame is written.
 *
 * @see ffmpeg_encoder_create, ffmpeg_encoder_destroy
 */
asciichat_error_t ffmpeg_encoder_write_frame(ffmpeg_encoder_t *enc,
                                             const uint8_t *rgb, int pitch);

/**
 * Close and finalize the output file, releasing all encoder resources.
 *
 * This function flushes any pending frames in the encoder, writes the file trailer,
 * and releases all allocated resources (memory buffers, file handles, codecs).
 *
 * **Critical:** This MUST be called to properly finalize the output file.
 * Without this call, the file may be incomplete or unplayable.
 *
 * @param enc Encoder handle (created with ffmpeg_encoder_create())
 *
 * @return ASCIICHAT_OK on success (currently always succeeds)
 *
 * @note Safe to call with NULL pointer (returns ASCIICHAT_OK).
 * @note After this call, `enc` must not be used again.
 * @note The output file is NOT guaranteed to be complete until this function returns.
 *
 * @see ffmpeg_encoder_create
 */
asciichat_error_t ffmpeg_encoder_destroy(ffmpeg_encoder_t *enc);
#endif
