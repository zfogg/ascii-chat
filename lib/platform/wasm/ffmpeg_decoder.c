/**
 * @file media/ffmpeg_decoder_wasm.c
 * @brief WASM bridge to ffmpeg.wasm for video/audio decoding
 *
 * This implementation bridges the native ffmpeg_decoder_t interface to ffmpeg.wasm,
 * allowing WASM builds to decode media files and streams using the same C API.
 *
 * ffmpeg.wasm is loaded as a JavaScript library and called via EM_JS.
 * @ingroup media
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <stdlib.h>
#include <string.h>

/* Full struct definition for the opaque ffmpeg_decoder_t */
struct ffmpeg_decoder_t {
    int decoder_id;           // ffmpeg.wasm decoder instance ID
    image_t *cached_frame;    // Cached RGB frame for reuse
    bool at_end;              // EOF flag
    double position;          // Current playback position in seconds
};

/* ============================================================================
 * JavaScript Bridges (EM_JS)
 * ============================================================================ */

/**
 * Initialize ffmpeg.wasm library (must be called once on page load)
 * @return 1 on success, 0 on failure
 */
EM_JS(int, js_ffmpeg_init, (), {
    if (typeof Module !== 'undefined' && Module.FFmpeg) {
        console.log('[WASM] ffmpeg.wasm already initialized');
        return 1;
    }
    console.log('[WASM] Initializing ffmpeg.wasm');
    // ffmpeg.wasm must be loaded by JavaScript before calling decoder functions
    return 0; // Will be initialized by JavaScript
});

/**
 * Create decoder from file path
 * @return decoder ID (> 0 on success, 0 on failure)
 */
EM_JS(int, js_ffmpeg_decoder_create, (const char *path_cstr), {
    if (!Module.FFmpeg || !Module.FFmpeg.isLoaded()) {
        console.error('[WASM] ffmpeg.wasm not loaded');
        return 0;
    }

    const path = UTF8ToString(path_cstr);
    console.log('[WASM] Creating decoder for: ' + path);

    try {
        // This would be implemented by JavaScript ffmpeg.wasm wrapper
        // Returns a decoder ID for future operations
        if (typeof Module.ffmpegDecoderCreate === 'function') {
            return Module.ffmpegDecoderCreate(path);
        }
        return 0;
    } catch (err) {
        console.error('[WASM] Decoder creation failed:', err);
        return 0;
    }
});

/**
 * Destroy decoder
 */
EM_JS(void, js_ffmpeg_decoder_destroy, (int decoder_id), {
    if (!Module.FFmpeg || !Module.FFmpeg.isLoaded()) {
        return;
    }

    try {
        if (typeof Module.ffmpegDecoderDestroy === 'function') {
            Module.ffmpegDecoderDestroy(decoder_id);
        }
    } catch (err) {
        console.error('[WASM] Decoder destruction failed:', err);
    }
});

/**
 * Read next video frame
 * Returns frame data in RGBA format (pointer to WASM heap, size in bytes)
 */
EM_JS(int, js_ffmpeg_decoder_read_frame, (int decoder_id, uint8_t *out_rgba, int width, int height), {
    if (!Module.FFmpeg || !Module.FFmpeg.isLoaded()) {
        return 0;
    }

    try {
        if (typeof Module.ffmpegDecoderReadFrame === 'function') {
            const rgba_data = Module.ffmpegDecoderReadFrame(decoder_id, width, height);
            if (rgba_data) {
                // Copy RGBA data to WASM heap
                const byteOffset = out_rgba;
                for (let i = 0; i < rgba_data.length; i++) {
                    HEAPU8[byteOffset + i] = rgba_data[i];
                }
                return 1;
            }
        }
        return 0;
    } catch (err) {
        console.error('[WASM] Frame read failed:', err);
        return 0;
    }
});

/**
 * Check if at end of file
 */
EM_JS(int, js_ffmpeg_decoder_at_end, (int decoder_id), {
    try {
        if (typeof Module.ffmpegDecoderAtEnd === 'function') {
            return Module.ffmpegDecoderAtEnd(decoder_id) ? 1 : 0;
        }
    } catch (err) {
        console.error('[WASM] at_end check failed:', err);
    }
    return 1;
});

/**
 * Rewind to start
 */
EM_JS(int, js_ffmpeg_decoder_rewind, (int decoder_id), {
    try {
        if (typeof Module.ffmpegDecoderRewind === 'function') {
            return Module.ffmpegDecoderRewind(decoder_id) ? 1 : 0;
        }
    } catch (err) {
        console.error('[WASM] Rewind failed:', err);
    }
    return 0;
});

/**
 * Check if has audio
 */
EM_JS(int, js_ffmpeg_decoder_has_audio, (int decoder_id), {
    try {
        if (typeof Module.ffmpegDecoderHasAudio === 'function') {
            return Module.ffmpegDecoderHasAudio(decoder_id) ? 1 : 0;
        }
    } catch (err) {
        console.error('[WASM] has_audio check failed:', err);
    }
    return 0;
});

/**
 * Get current position in seconds
 */
EM_JS(double, js_ffmpeg_decoder_get_position, (int decoder_id), {
    try {
        if (typeof Module.ffmpegDecoderGetPosition === 'function') {
            return Module.ffmpegDecoderGetPosition(decoder_id);
        }
    } catch (err) {
        console.error('[WASM] get_position failed:', err);
    }
    return 0.0;
});

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

ffmpeg_decoder_t *ffmpeg_decoder_create(const char *path) {
    if (!path) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "Path must not be NULL"), NULL;
    }

    int decoder_id = js_ffmpeg_decoder_create(path);
    if (decoder_id <= 0) {
        return SET_ERRNO(ERROR_MEDIA_OPEN, "Failed to create ffmpeg decoder"), NULL;
    }

    ffmpeg_decoder_t *decoder = malloc(sizeof(ffmpeg_decoder_t));
    if (!decoder) {
        js_ffmpeg_decoder_destroy(decoder_id);
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate decoder"), NULL;
    }

    decoder->decoder_id = decoder_id;
    decoder->cached_frame = NULL;
    decoder->at_end = false;
    decoder->position = 0.0;

    log_debug("WASM FFmpeg decoder created for: %s", path);
    return decoder;
}

ffmpeg_decoder_t *ffmpeg_decoder_create_stdin(void) {
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "stdin decoder not supported in WASM"), NULL;
}

void ffmpeg_decoder_destroy(ffmpeg_decoder_t *decoder) {
    if (!decoder) return;

    js_ffmpeg_decoder_destroy(decoder->decoder_id);

    if (decoder->cached_frame) {
        image_destroy(decoder->cached_frame);
    }

    free(decoder);
    log_debug("WASM FFmpeg decoder destroyed");
}

asciichat_error_t ffmpeg_decoder_start_prefetch(ffmpeg_decoder_t *decoder) {
    if (!decoder) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "Decoder must not be NULL");
    }
    // No prefetching in WASM - frames are read on-demand
    return ASCIICHAT_OK;
}

void ffmpeg_decoder_stop_prefetch(ffmpeg_decoder_t *decoder) {
    (void)decoder;
}

void ffmpeg_decoder_set_exit_callback(ffmpeg_decoder_t *decoder,
                                      bool (*should_exit)(void *user_data),
                                      void *user_data) {
    (void)decoder;
    (void)should_exit;
    (void)user_data;
}

image_t *ffmpeg_decoder_read_video_frame(ffmpeg_decoder_t *decoder) {
    if (!decoder) return NULL;

    if (decoder->at_end) return NULL;

    // Allocate cached frame if not already done
    if (!decoder->cached_frame) {
        // TODO: Get actual dimensions from decoder
        decoder->cached_frame = image_new(1920, 1080);
        if (!decoder->cached_frame) {
            return NULL;
        }
    }

    // Allocate RGBA staging buffer
    int width = decoder->cached_frame->w;
    int height = decoder->cached_frame->h;
    uint8_t *rgba_buf = malloc(width * height * 4);
    if (!rgba_buf) {
        return NULL;
    }

    // Read frame from ffmpeg.wasm
    int result = js_ffmpeg_decoder_read_frame(decoder->decoder_id, rgba_buf, width, height);
    if (result <= 0) {
        decoder->at_end = true;
        free(rgba_buf);
        return NULL;
    }

    // Convert RGBA to RGB24
    for (int i = 0; i < width * height; i++) {
        decoder->cached_frame->pixels[i].r = rgba_buf[i * 4 + 0];
        decoder->cached_frame->pixels[i].g = rgba_buf[i * 4 + 1];
        decoder->cached_frame->pixels[i].b = rgba_buf[i * 4 + 2];
    }

    free(rgba_buf);
    decoder->position = js_ffmpeg_decoder_get_position(decoder->decoder_id);
    return decoder->cached_frame;
}

bool ffmpeg_decoder_at_end(ffmpeg_decoder_t *decoder) {
    if (!decoder) return true;

    if (decoder->at_end) return true;

    int result = js_ffmpeg_decoder_at_end(decoder->decoder_id);
    decoder->at_end = (result != 0);
    return decoder->at_end;
}

asciichat_error_t ffmpeg_decoder_rewind(ffmpeg_decoder_t *decoder) {
    if (!decoder) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "Decoder must not be NULL");
    }

    int result = js_ffmpeg_decoder_rewind(decoder->decoder_id);
    if (result <= 0) {
        return SET_ERRNO(ERROR_MEDIA_SEEK, "Failed to rewind decoder");
    }

    decoder->at_end = false;
    decoder->position = 0.0;
    return ASCIICHAT_OK;
}

bool ffmpeg_decoder_has_audio(ffmpeg_decoder_t *decoder) {
    if (!decoder) return false;

    int result = js_ffmpeg_decoder_has_audio(decoder->decoder_id);
    return (result != 0);
}

bool ffmpeg_decoder_has_video(ffmpeg_decoder_t *decoder) {
    if (!decoder) return false;
    return true; // Assume video if decoder was created
}

double ffmpeg_decoder_get_position(ffmpeg_decoder_t *decoder) {
    if (!decoder) return 0.0;

    return js_ffmpeg_decoder_get_position(decoder->decoder_id);
}

size_t ffmpeg_decoder_read_audio_samples(ffmpeg_decoder_t *decoder,
                                         float *buffer, size_t num_samples) {
    (void)decoder;
    (void)buffer;
    (void)num_samples;
    // Audio decoding not yet implemented in ffmpeg.wasm bridge
    return 0;
}

asciichat_error_t ffmpeg_decoder_seek_to_timestamp(ffmpeg_decoder_t *decoder, double timestamp) {
    (void)decoder;
    (void)timestamp;
    return SET_ERRNO(ERROR_MEDIA_SEEK, "Seeking not yet implemented in ffmpeg.wasm");
}

double ffmpeg_decoder_get_duration(ffmpeg_decoder_t *decoder) {
    (void)decoder;
    return 0.0;
}

double ffmpeg_decoder_get_video_fps(ffmpeg_decoder_t *decoder) {
    (void)decoder;
    return 24.0; // Default FPS
}

#endif // __EMSCRIPTEN__
