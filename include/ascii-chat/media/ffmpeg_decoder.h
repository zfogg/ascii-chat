#pragma once

/**
 * @file media/ffmpeg_decoder.h
 * @brief 🎞️ FFmpeg-based media decoder for video and audio streams
 * @ingroup media
 * @addtogroup media
 * @{
 *
 * This header provides FFmpeg integration for decoding various media formats.
 * It wraps FFmpeg's complex API into a simple interface for extracting RGB
 * video frames and float audio samples.
 *
 * CORE FEATURES:
 * ==============
 * - Multi-format container support (mp4, avi, mkv, webm, etc.)
 * - Video decoding to RGB24 format
 * - Audio decoding to 48kHz mono float
 * - Stdin input support via custom AVIOContext
 * - Seeking for loop support
 * - Automatic stream detection
 *
 * FFMPEG LIBRARIES USED:
 * ======================
 * - libavformat: Container demuxing
 * - libavcodec: Video/audio codec decoding
 * - libavutil: Utilities and error handling
 * - libswscale: Video format conversion (YUV -> RGB)
 * - libswresample: Audio resampling (any rate -> 48kHz mono)
 *
 * SUPPORTED CODECS:
 * =================
 * - Video: H.264, H.265, VP8, VP9, AV1, MPEG-4, etc.
 * - Audio: AAC, MP3, Opus, Vorbis, FLAC, PCM, etc.
 * - Containers: MP4, AVI, MKV, WebM, MOV, FLV, GIF, etc.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stdbool.h>
#include <stddef.h>
#include "../common.h"
#include "../video/image.h"

/* ============================================================================
 * FFmpeg Decoder Handle
 * ============================================================================ */

/**
 * @brief Opaque FFmpeg decoder handle
 *
 * Forward declaration of FFmpeg decoder structure. Use ffmpeg_decoder_create()
 * to create and ffmpeg_decoder_destroy() to cleanup.
 *
 * @ingroup media
 */
typedef struct ffmpeg_decoder_t ffmpeg_decoder_t;

/* ============================================================================
 * Decoder Lifecycle
 * ============================================================================ */

/**
 * @brief Create FFmpeg decoder from file path
 * @param path File path to media file (must not be NULL)
 * @return Pointer to decoder, or NULL on failure
 *
 * Creates and initializes an FFmpeg decoder for the specified file.
 * Opens the file, detects streams, and initializes codecs.
 *
 * **Initialization process:**
 * 1. Open format context (avformat_open_input)
 * 2. Find stream information (avformat_find_stream_info)
 * 3. Detect video and audio streams
 * 4. Open codec contexts for detected streams
 * 5. Initialize swscale/swresample contexts
 *
 * @note Sets errno context on failure
 * @note Call ffmpeg_decoder_destroy() to cleanup
 *
 * @ingroup media
 */
ffmpeg_decoder_t *ffmpeg_decoder_create(const char *path);

/**
 * @brief Create FFmpeg decoder from stdin
 * @return Pointer to decoder, or NULL on failure
 *
 * Creates an FFmpeg decoder that reads from stdin using a custom AVIOContext.
 * This allows piping media data directly into ascii-chat.
 *
 * **Usage:**
 * @code
 * cat video.mp4 | ascii-chat client --file -
 * ffmpeg -i input.avi -f matroska - | ascii-chat client --file -
 * @endcode
 *
 * **Limitations:**
 * - Cannot seek (no loop support)
 * - Some formats may not work well with stdin (require seekable input)
 * - Recommend using formats designed for streaming (matroska, mpegts)
 *
 * @note Sets errno context on failure
 * @note Call ffmpeg_decoder_destroy() to cleanup
 *
 * @ingroup media
 */
ffmpeg_decoder_t *ffmpeg_decoder_create_stdin(void);

/**
 * @brief Destroy FFmpeg decoder and free resources
 * @param decoder Decoder to destroy (can be NULL)
 *
 * Cleans up FFmpeg decoder and releases all resources. Safe to call with NULL.
 * Automatically stops the background prefetch thread if running.
 *
 * **Cleanup process:**
 * 1. Stop prefetch thread if running
 * 2. Free video frame ringbuffer
 * 3. Free swscale/swresample contexts
 * 4. Close codec contexts
 * 5. Close format context
 * 6. Free frame and packet structures
 * 7. Free decoder structure
 *
 * @ingroup media
 */
void ffmpeg_decoder_destroy(ffmpeg_decoder_t *decoder);

/**
 * @brief Start background frame prefetching thread
 * @param decoder Decoder (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Starts a background thread that continuously reads and decodes video frames,
 * storing them in an internal ringbuffer. The render loop then pulls pre-decoded
 * frames from this buffer without blocking on network I/O.
 *
 * **Purpose:** Solves YouTube HTTP streaming performance issues by decoupling
 * the blocking av_read_frame() calls from the render loop. Without this, frames
 * take 93ms to arrive instead of the expected 41.7ms at 24 FPS.
 *
 * @note Should be called after decoder creation for FILE/HTTP sources
 * @note Automatically called when media source is created
 * @note Thread is stopped in ffmpeg_decoder_destroy()
 *
 * @ingroup media
 */
asciichat_error_t ffmpeg_decoder_start_prefetch(ffmpeg_decoder_t *decoder);

/**
 * @brief Stop background frame prefetching thread
 * @param decoder Decoder (must not be NULL)
 *
 * Stops the background prefetch thread. Automatically called by
 * ffmpeg_decoder_destroy(). Safe to call even if thread isn't running.
 *
 * @ingroup media
 */
void ffmpeg_decoder_stop_prefetch(ffmpeg_decoder_t *decoder);

/**
 * @brief Check if background frame prefetching thread is running
 * @param decoder Decoder (must not be NULL)
 * @return true if prefetch thread is running, false otherwise
 *
 * Utility function to check if the prefetch thread is currently active.
 * Used during seeking to determine if thread needs to be stopped/restarted.
 *
 * @ingroup media
 */
bool ffmpeg_decoder_is_prefetch_running(ffmpeg_decoder_t *decoder);

/* ============================================================================
 * Video Operations
 * ============================================================================ */

/**
 * @brief Decode next video frame
 * @param decoder Decoder (must not be NULL)
 * @return Pointer to image_t frame, or NULL on error/EOF
 *
 * Decodes the next video frame from the media stream. Returns an image_t
 * structure in RGB24 format.
 *
 * **Frame format:**
 * - Pixel format: RGB24 (8 bits per channel)
 * - Dimensions: Original video dimensions
 * - Memory: Owned by decoder (do NOT free)
 *
 * **Return values:**
 * - Non-NULL: Valid frame pointer
 * - NULL: Error or end of stream (check ffmpeg_decoder_at_end())
 *
 * **Decoding process:**
 * 1. Read packet from format context
 * 2. Send packet to video decoder
 * 3. Receive decoded frame
 * 4. Convert frame to RGB24 using swscale
 * 5. Wrap in image_t structure
 *
 * @note Returned frame is valid until next call or decoder destruction
 * @note Skips non-video packets automatically
 *
 * @ingroup media
 */
image_t *ffmpeg_decoder_read_video_frame(ffmpeg_decoder_t *decoder);

/**
 * @brief Check if decoder has video stream
 * @param decoder Decoder (must not be NULL)
 * @return true if video stream exists, false otherwise
 *
 * @ingroup media
 */
bool ffmpeg_decoder_has_video(ffmpeg_decoder_t *decoder);

/**
 * @brief Get video dimensions
 * @param decoder Decoder (must not be NULL)
 * @param width Output pointer for width (can be NULL)
 * @param height Output pointer for height (can be NULL)
 * @return ASCIICHAT_OK on success, error code if no video stream
 *
 * @ingroup media
 */
asciichat_error_t ffmpeg_decoder_get_video_dimensions(ffmpeg_decoder_t *decoder, int *width, int *height);

/**
 * @brief Get video frame rate
 * @param decoder Decoder (must not be NULL)
 * @return Frame rate in FPS, or -1.0 if unknown/no video
 *
 * Returns the average frame rate of the video stream.
 *
 * @ingroup media
 */
double ffmpeg_decoder_get_video_fps(ffmpeg_decoder_t *decoder);

/* ============================================================================
 * Audio Operations
 * ============================================================================ */

/**
 * @brief Decode audio samples
 * @param decoder Decoder (must not be NULL)
 * @param buffer Output buffer for samples (must not be NULL)
 * @param num_samples Number of samples to read
 * @return Number of samples actually read (0 to num_samples)
 *
 * Decodes audio samples from the media stream. Samples are resampled to
 * 48kHz mono float format, compatible with ascii-chat's Opus encoding.
 *
 * **Audio format:**
 * - Format: 32-bit float
 * - Channels: Mono (1 channel)
 * - Sample rate: 48000 Hz
 * - Range: -1.0 to 1.0
 *
 * **Decoding process:**
 * 1. Read packet from format context
 * 2. Send packet to audio decoder
 * 3. Receive decoded frame
 * 4. Resample to 48kHz mono using swresample
 * 5. Copy to output buffer
 *
 * @note Skips non-audio packets automatically
 * @note May return fewer samples than requested at EOF
 *
 * @ingroup media
 */
size_t ffmpeg_decoder_read_audio_samples(ffmpeg_decoder_t *decoder, float *buffer, size_t num_samples);

/**
 * @brief Check if decoder has audio stream
 * @param decoder Decoder (must not be NULL)
 * @return true if audio stream exists, false otherwise
 *
 * @ingroup media
 */
bool ffmpeg_decoder_has_audio(ffmpeg_decoder_t *decoder);

/* ============================================================================
 * Playback Control
 * ============================================================================ */

/**
 * @brief Seek to beginning of media
 * @param decoder Decoder (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Seeks to the beginning of the media file. Used for loop implementation.
 *
 * **Seek process:**
 * 1. Flush codec buffers (avcodec_flush_buffers)
 * 2. Seek to timestamp 0 (av_seek_frame)
 * 3. Clear decoder state
 *
 * @note Stdin decoders cannot seek (returns ERROR_NOT_SUPPORTED)
 * @note Some formats may not support seeking
 *
 * @ingroup media
 */
asciichat_error_t ffmpeg_decoder_rewind(ffmpeg_decoder_t *decoder);

/**
 * @brief Seek to specific timestamp in media
 * @param decoder Decoder (must not be NULL)
 * @param timestamp_sec Seek target in seconds
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Seeks to the specified timestamp in the media file. Used for audio/video
 * synchronization in multi-decoder scenarios.
 *
 * **Seek process:**
 * 1. Flush codec buffers (avcodec_flush_buffers)
 * 2. Seek to timestamp (av_seek_frame with AV_TIME_BASE conversion)
 * 3. Clear decoder state
 *
 * @note Stdin decoders cannot seek (returns ERROR_NOT_SUPPORTED)
 * @note Some formats may not support seeking
 * @note Seeks approximately - may land before/after exact timestamp
 *
 * @ingroup media
 */
asciichat_error_t ffmpeg_decoder_seek_to_timestamp(ffmpeg_decoder_t *decoder, double timestamp_sec);

/**
 * @brief Check if decoder reached end of stream
 * @param decoder Decoder (must not be NULL)
 * @return true if EOF reached, false otherwise
 *
 * @ingroup media
 */
bool ffmpeg_decoder_at_end(ffmpeg_decoder_t *decoder);

/**
 * @brief Get media duration in seconds
 * @param decoder Decoder (must not be NULL)
 * @return Duration in seconds, or -1.0 if unknown
 *
 * Returns the total duration from the container metadata.
 *
 * @ingroup media
 */
double ffmpeg_decoder_get_duration(ffmpeg_decoder_t *decoder);

/**
 * @brief Get current playback position in seconds
 * @param decoder Decoder (must not be NULL)
 * @return Current position in seconds, or -1.0 if unknown
 *
 * Returns the presentation timestamp of the last decoded frame.
 *
 * @ingroup media
 */
double ffmpeg_decoder_get_position(ffmpeg_decoder_t *decoder);

/** @} */
