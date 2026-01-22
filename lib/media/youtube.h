/**
 * @file youtube.h
 * @brief YouTube URL extraction and stream URL resolution
 * @ingroup media
 * @addtogroup media
 * @{
 *
 * This module provides YouTube URL detection and extraction of direct
 * stream URLs using libytdl's ytdlcore library. YouTube URLs are detected,
 * parsed, and converted to direct playable stream URLs that can be passed
 * to FFmpeg.
 *
 * ## Features
 *
 * - YouTube URL pattern detection
 * - Video ID extraction from various YouTube URL formats
 * - Direct stream URL extraction using libytdl
 * - Error handling for age-restricted, geo-blocked content
 * - HTTP watch page fetching via FFmpeg
 *
 * ## Usage
 *
 * @code{.c}
 * if (youtube_is_youtube_url("https://youtube.com/watch?v=dQw4w9WgXcQ")) {
 *     char stream_url[2048];
 *     asciichat_error_t err = youtube_extract_stream_url(
 *         "https://youtube.com/watch?v=dQw4w9WgXcQ",
 *         stream_url, sizeof(stream_url)
 *     );
 *     if (err == ASCIICHAT_OK) {
 *         // stream_url now contains direct playable URL
 *         // Valid for ~6 hours, can be passed to FFmpeg
 *         ffmpeg_decoder_create(stream_url);
 *     }
 * }
 * @endcode
 *
 * ## URL Format Support
 *
 * - https://www.youtube.com/watch?v=VIDEO_ID
 * - https://youtube.com/watch?v=VIDEO_ID
 * - https://m.youtube.com/watch?v=VIDEO_ID
 * - https://youtu.be/VIDEO_ID
 * - https://youtube.com/watch?v=VIDEO_ID&t=TIMESTAMP
 * - https://youtube.com/watch?v=VIDEO_ID&list=PLAYLIST_ID (first video)
 *
 * ## Error Handling
 *
 * Possible error codes:
 * - `ASCIICHAT_OK` - Success, stream URL extracted
 * - `ERROR_YOUTUBE_NOT_SUPPORTED` - YouTube support not compiled
 * - `ERROR_YOUTUBE_EXTRACT_FAILED` - General extraction failure
 * - `ERROR_YOUTUBE_UNPLAYABLE` - Video is age-restricted, geo-blocked, etc.
 * - `ERROR_YOUTUBE_NETWORK` - Failed to fetch watch page
 * - `ERROR_YOUTUBE_INVALID_URL` - Invalid YouTube URL format
 * - `ERROR_INVALID_PARAM` - NULL pointer or invalid buffer size
 * - `ERROR_MEMORY` - Memory allocation failure
 *
 * @note libytdl is a required dependency for all YouTube functionality
 * @note Stream URLs are valid for approximately 6 hours
 * @note URLs can be passed directly to FFmpeg for playback
 * @note Uses QuickJS for JavaScript cipher decryption of sig parameter
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "asciichat_errno.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if a URL is a YouTube URL
 *
 * Detects if the given URL matches YouTube URL patterns.
 * Supports various YouTube domain formats (youtube.com, youtu.be, m.youtube.com, etc.)
 *
 * @param url URL to check (must not be NULL)
 * @return true if URL is a YouTube URL, false otherwise
 *
 * @note Does not validate video ID format, only checks domain
 * @note Thread-safe: Uses no static state
 *
 * @ingroup media
 */
bool youtube_is_youtube_url(const char *url);

/**
 * @brief Extract YouTube video ID from a URL
 *
 * Parses various YouTube URL formats and extracts the video ID.
 * Supports:
 * - youtube.com/watch?v=ID
 * - youtu.be/ID
 * - m.youtube.com/watch?v=ID
 *
 * @param url YouTube URL (must not be NULL, must be YouTube URL)
 * @param output_id Output buffer for video ID
 * @param id_size Size of output_id buffer (should be at least 16 bytes)
 * @return ASCIICHAT_OK on success, ERROR_YOUTUBE_INVALID_URL on failure
 *
 * @note Video IDs are typically 11 characters long (alphanumeric, -, _)
 * @note Output is null-terminated
 * @note If buffer is too small, returns ERROR_YOUTUBE_INVALID_URL
 *
 * @ingroup media
 */
asciichat_error_t youtube_extract_video_id(const char *url, char *output_id, size_t id_size);

/**
 * @brief Extract direct stream URL from YouTube video URL
 *
 * Main function for YouTube URL extraction. This function:
 * 1. Extracts video ID from YouTube URL
 * 2. Fetches watch page HTML via FFmpeg HTTP support
 * 3. Parses HTML with libytdl's ytdlcore
 * 4. Extracts video info and format information
 * 5. Selects best available video format
 * 6. Returns direct stream URL that can be passed to FFmpeg
 *
 * @param youtube_url YouTube URL (must not be NULL, must be YouTube URL)
 * @param output_url Output buffer for stream URL
 * @param output_size Size of output_url buffer (should be at least 2048 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @retval ASCIICHAT_OK Successfully extracted stream URL
 * @retval ERROR_YOUTUBE_INVALID_URL Invalid YouTube URL format
 * @retval ERROR_YOUTUBE_NETWORK Failed to fetch watch page from YouTube
 * @retval ERROR_YOUTUBE_UNPLAYABLE Video is age-restricted, geo-blocked, deleted, etc.
 * @retval ERROR_YOUTUBE_EXTRACT_FAILED Failed to parse HTML or extract formats
 * @retval ERROR_INVALID_PARAM output_url is NULL or output_size too small
 * @retval ERROR_MEMORY Memory allocation failure
 *
 * @note Output stream URL is valid for approximately 6 hours
 * @note Stream URL can be passed directly to FFmpeg via ffmpeg_decoder_create()
 * @note This function blocks while fetching the watch page (typically 1-2 seconds)
 * @note Prefers video format when available, falls back to audio-only
 * @note Thread-safe: Uses no static state (requires libytdl thread safety)
 *
 * @ingroup media
 */
asciichat_error_t youtube_extract_stream_url(const char *youtube_url, char *output_url, size_t output_size);

#ifdef __cplusplus
}
#endif

/** @} */ /* media */
