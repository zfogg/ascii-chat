/**
 * @file media.c
 * @brief Media file and stream options
 * @ingroup options
 *
 * Options for streaming from media files, URLs, seeking, looping,
 * and cookie handling for streaming services.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/options/registry/common.h>
#include <ascii-chat/options/registry/metadata.h>

#include <ascii-chat/options/parsers.h>

// ============================================================================
// MEDIA CATEGORY - Media file and stream options
// ============================================================================
const registry_entry_t g_media_entries[] = {
    // Media File Streaming Options
    {"file",
     'f',
     OPTION_TYPE_STRING,
     offsetof(options_t, media_file),
     "",
     0,
     "Stream from media file or stdin (use '-' for stdin). Supported formats: see man ffmpeg-formats; codecs: see man "
     "ffmpeg-codecs",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_FILE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0},
     NULL},
    {"url",
     'u',
     OPTION_TYPE_STRING,
     offsetof(options_t, media_url),
     "",
     0,
     "Stream from network URL. Direct HTTP/HTTPS/RTSP streams use FFmpeg; complex sites (YouTube, TikTok, etc.) use "
     "yt-dlp. Supported formats: see man ffmpeg-formats; codecs: see man ffmpeg-codecs; sites: yt-dlp "
     "https://github.com/yt-dlp/yt-dlp/blob/master/README.md#supported-sites",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_URL",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0},
     NULL},
    {"loop",
     'l',
     OPTION_TYPE_BOOL,
     offsetof(options_t, media_loop),
     &default_media_loop_value,
     sizeof(bool),
     "Loop media file playback (not supported for --url).",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_LOOP",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0},
     NULL},
    {"pause",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, pause),
     &default_pause_value,
     sizeof(bool),
     "Start playback paused (toggle with spacebar, requires --file or --url).",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_PAUSE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0},
     NULL},
    {"seek",
     's',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, media_seek_timestamp),
     &default_media_seek_value,
     sizeof(double),
     "Seek to timestamp before playback (format: seconds, MM:SS, or HH:MM:SS.ms).",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_SEEK",
     NULL,
     parse_timestamp,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.examples = g_seek_examples, .input_type = OPTION_INPUT_STRING},
     NULL},
    {"yt-dlp-options",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, yt_dlp_options),
     NULL,
     sizeof(((options_t *)0)->yt_dlp_options),
     "Arbitrary yt-dlp options passed to the extraction subprocess for URL resolution. "
     "Examples: \"--no-warnings\" or \"--proxy socks5://127.0.0.1:1080\" or \"--cookies-from-browser=firefox\"",
     "MEDIA",
     NULL,
     false,
     "ASCII_CHAT_YT_DLP_OPTIONS",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0},
     NULL},

    REGISTRY_TERMINATOR()};
