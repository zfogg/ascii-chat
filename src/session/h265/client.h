#pragma once

/**
 * @file session/h265/client.h
 * @brief Client-side H.265 media capture and encoding pipeline
 *
 * The client encodes media from various sources into H.265 and sends
 * encoded frames to the server:
 *
 * - Test pattern: Procedurally generated color grids
 * - File: Local video files (MP4, MKV, AVI, etc.) via FFmpeg
 * - URL: Remote streams (HTTP, RTSP, HLS, DASH, YouTube, etc.) via FFmpeg + yt-dlp
 * - Webcam: Live camera input via PortAudio/libwebsockets
 *
 * Each media source is decoded to RGBA, then:
 * 1. Converted to ASCII art via color-to-character mapping
 * 2. Encoded to H.265 with h265_encoder
 * 3. Sent to server in H.265 packets
 */

#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>
#include <stddef.h>

typedef struct h265_client_context h265_client_context_t;
typedef struct h265_client_encoder h265_client_encoder_t;

/**
 * Media source type
 */
typedef enum {
    H265_SOURCE_TEST_PATTERN = 0,   // Procedural test pattern
    H265_SOURCE_FILE = 1,           // Local file
    H265_SOURCE_URL = 2,            // Remote URL (with yt-dlp support)
    H265_SOURCE_WEBCAM = 3,         // Live camera
} h265_media_source_t;

/**
 * Create a client H.265 encoding context
 * Initializes the encoder and media pipeline
 *
 * @param width Initial frame width (characters)
 * @param height Initial frame height (characters)
 * @return Context handle, or NULL on error
 */
h265_client_context_t *h265_client_context_create(uint16_t width, uint16_t height);

/**
 * Destroy the client context and encoder
 *
 * @param ctx Context to destroy (may be NULL)
 */
void h265_client_context_destroy(h265_client_context_t *ctx);

/**
 * Initialize media source for the client encoder
 * This prepares the media pipeline (opens file, connects to stream, etc.)
 *
 * @param ctx Client context
 * @param source_type Type of media source (file, URL, webcam, test pattern)
 * @param source_location File path, URL, or device path (NULL for test pattern)
 * @return Error code (ASCIICHAT_OK on success)
 *
 * Examples:
 *   h265_client_init_media_source(ctx, H265_SOURCE_FILE, "/path/to/video.mp4")
 *   h265_client_init_media_source(ctx, H265_SOURCE_URL, "https://www.youtube.com/watch?v=...")
 *   h265_client_init_media_source(ctx, H265_SOURCE_WEBCAM, "/dev/video0")
 *   h265_client_init_media_source(ctx, H265_SOURCE_TEST_PATTERN, NULL)
 */
asciichat_error_t h265_client_init_media_source(
    h265_client_context_t *ctx,
    h265_media_source_t source_type,
    const char *source_location
);

/**
 * Capture and encode the next frame
 * Reads from the media source, converts to ASCII, and encodes to H.265
 *
 * @param ctx Client context
 * @param output_buf Output buffer for H.265 packet
 * @param output_size Input: buffer size, Output: actual packet size
 * @return Error code (ASCIICHAT_OK on success)
 *
 * The output packet includes:
 *   [flags: u8][width: u16][height: u16][x265_data...]
 * as defined in h265_encoder
 */
asciichat_error_t h265_client_capture_and_encode(
    h265_client_context_t *ctx,
    uint8_t *output_buf,
    size_t *output_size
);

/**
 * Request a keyframe on the next encode
 * Used for error recovery and stream synchronization
 *
 * @param ctx Client context
 */
void h265_client_request_keyframe(h265_client_context_t *ctx);

/**
 * Get encoder statistics
 *
 * @param ctx Client context
 * @param total_frames Output: total frames encoded
 * @param keyframes Output: total keyframes encoded
 * @param avg_bitrate Output: average bitrate in bits per second
 */
void h265_client_get_stats(
    h265_client_context_t *ctx,
    uint64_t *total_frames,
    uint64_t *keyframes,
    uint32_t *avg_bitrate
);
