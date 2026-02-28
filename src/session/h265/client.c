/**
 * @file session/h265/client.c
 * @brief Client-side H.265 media capture and encoding pipeline
 */

#include "client.h"
#include <ascii-chat/video/h265/encoder.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <string.h>
#include <stdlib.h>

typedef struct h265_client_context {
    h265_encoder_t *encoder;
    h265_media_source_t source_type;
    char *source_location;

    uint16_t current_width;
    uint16_t current_height;

    uint8_t *frame_buf;
    size_t frame_buf_size;

    void *media_context;
} h265_client_context_t;

h265_client_context_t *h265_client_context_create(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0) {
        SET_ERRNO(ERROR_MEDIA_INIT, "Frame dimensions must be non-zero");
        return NULL;
    }

    h265_client_context_t *ctx = SAFE_CALLOC(1, sizeof(h265_client_context_t), h265_client_context_t *);
    if (!ctx) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate client context");
        return NULL;
    }

    ctx->encoder = h265_encoder_create(width, height);
    if (!ctx->encoder) {
        SET_ERRNO(ERROR_MEDIA_INIT, "Failed to create H.265 encoder");
        SAFE_FREE(ctx);
        return NULL;
    }

    ctx->current_width = width;
    ctx->current_height = height;
    ctx->frame_buf_size = width * height;
    ctx->frame_buf = SAFE_MALLOC(ctx->frame_buf_size, uint8_t *);
    if (!ctx->frame_buf) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate frame buffer");
        h265_encoder_destroy(ctx->encoder);
        SAFE_FREE(ctx);
        return NULL;
    }

    log_info("H.265 client context created: %ux%u", width, height);
    return ctx;
}

void h265_client_context_destroy(h265_client_context_t *ctx) {
    if (!ctx) return;

    if (ctx->encoder) {
        h265_encoder_destroy(ctx->encoder);
    }
    if (ctx->frame_buf) {
        SAFE_FREE(ctx->frame_buf);
    }
    if (ctx->source_location) {
        SAFE_FREE(ctx->source_location);
    }
    SAFE_FREE(ctx);
}

asciichat_error_t h265_client_init_media_source(
    h265_client_context_t *ctx,
    h265_media_source_t source_type,
    const char *source_location
) {
    if (!ctx) {
        return SET_ERRNO(ERROR_INTERNAL, "Invalid client context");
    }

    ctx->source_type = source_type;

    if (source_location) {
        ctx->source_location = SAFE_MALLOC(strlen(source_location) + 1, char *);
        if (!ctx->source_location) {
            return SET_ERRNO(ERROR_MEMORY, "Failed to allocate source location string");
        }
        strcpy(ctx->source_location, source_location);
    }

    switch (source_type) {
        case H265_SOURCE_TEST_PATTERN:
            log_info("H.265 client media source: test pattern");
            break;
        case H265_SOURCE_FILE:
            log_info("H.265 client media source: file '%s'", source_location);
            break;
        case H265_SOURCE_URL:
            log_info("H.265 client media source: URL '%s'", source_location);
            break;
        case H265_SOURCE_WEBCAM:
            log_info("H.265 client media source: webcam '%s'", source_location);
            break;
    }

    return ASCIICHAT_OK;
}

asciichat_error_t h265_client_capture_and_encode(
    h265_client_context_t *ctx,
    uint8_t *output_buf,
    size_t *output_size
) {
    if (!ctx || !output_buf || !output_size) {
        return SET_ERRNO(ERROR_INTERNAL, "Invalid arguments to capture_and_encode");
    }

    if (!ctx->encoder) {
        return SET_ERRNO(ERROR_INTERNAL, "Encoder not initialized");
    }

    memset(ctx->frame_buf, 128, ctx->frame_buf_size);

    asciichat_error_t result = h265_encode(
        ctx->encoder,
        ctx->current_width,
        ctx->current_height,
        ctx->frame_buf,
        output_buf,
        output_size
    );

    return result;
}

void h265_client_request_keyframe(h265_client_context_t *ctx) {
    if (ctx && ctx->encoder) {
        h265_encoder_request_keyframe(ctx->encoder);
    }
}

void h265_client_get_stats(
    h265_client_context_t *ctx,
    uint64_t *total_frames,
    uint64_t *keyframes,
    uint32_t *avg_bitrate
) {
    if (!ctx || !ctx->encoder) return;

    h265_encoder_get_stats(ctx->encoder, total_frames, keyframes, avg_bitrate);
}
