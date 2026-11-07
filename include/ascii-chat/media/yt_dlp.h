/**
 * @file yt_dlp.h
 * @brief yt-dlp stream URL extraction
 * @ingroup media
 *
 * Encapsulates yt-dlp subprocess logic for extracting playable stream URLs
 * from URLs that yt-dlp supports (YouTube, Twitch, etc.)
 */

#pragma once

#include "../asciichat_errno.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extract stream URL using yt-dlp with optional parameters
 *
 * Prefers a combined stream, falling back to separate video and audio streams.
 * Results are cached for 30 seconds.
 *
 * @param url Input URL (YouTube, Twitch, direct streams, etc.)
 * @param yt_dlp_options Arbitrary yt-dlp options string
 *                       e.g., "--cookies-from-browser=chrome --no-warnings"
 *                       Quotes group arguments; no shell expansion is performed.
 *                       Pass NULL or "" for no extra options.
 * @param output_url Output buffer for extracted stream URL
 * @param output_size Size of output buffer (recommend 8192+ bytes)
 * @param audio_url Output buffer for a separate audio URL, or an empty string
 *                  when the first URL contains both streams
 * @param audio_size Size of the audio URL buffer
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note On failure, logs error context via log_error()
 * @note Results are cached to avoid redundant yt-dlp calls
 *
 * @ingroup media
 */
asciichat_error_t yt_dlp_extract_stream_urls(const char *url, const char *yt_dlp_options, char *output_url,
                                             size_t output_size, char *audio_url, size_t audio_size);

/** Extract the first playable stream URL. Prefer the two-output API for audio. */
asciichat_error_t yt_dlp_extract_stream_url(const char *url, const char *yt_dlp_options, char *output_url,
                                            size_t output_size);

/**
 * @brief Check if yt-dlp is installed and accessible
 *
 * Runs `yt-dlp --version` to verify yt-dlp is available in PATH.
 *
 * @return true if yt-dlp --version succeeds, false otherwise
 *
 * @note Thread-safe: No static state
 *
 * @ingroup media
 */
bool yt_dlp_is_available(void);

#ifdef __cplusplus
}
#endif

/** @} */ /* media */
