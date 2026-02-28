/**
 * @file session/h265/server.c
 * @brief Server-side H.265 decoding pipeline
 */

#include "server.h"
#include <ascii-chat/video/h265/decoder.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <string.h>
#include <stdlib.h>

#define MAX_CLIENTS 64

typedef struct {
    uint32_t client_id;
    h265_decoder_t *decoder;
    uint16_t last_width;
    uint16_t last_height;
    bool in_use;
} h265_client_decoder_t;

typedef struct h265_server_context {
    h265_client_decoder_t clients[MAX_CLIENTS];
    uint32_t active_client_count;
} h265_server_context_t;

h265_server_context_t *h265_server_context_create(void) {
    h265_server_context_t *ctx = SAFE_CALLOC(1, sizeof(h265_server_context_t), h265_server_context_t *);
    if (!ctx) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate server context");
        return NULL;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        ctx->clients[i].in_use = false;
    }
    ctx->active_client_count = 0;

    log_info("H.265 server context created (max %d clients)", MAX_CLIENTS);
    return ctx;
}

void h265_server_context_destroy(h265_server_context_t *ctx) {
    if (!ctx) return;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (ctx->clients[i].in_use && ctx->clients[i].decoder) {
            h265_decoder_destroy(ctx->clients[i].decoder);
        }
    }
    SAFE_FREE(ctx);
}

h265_server_client_t *h265_server_get_client_decoder(
    h265_server_context_t *ctx,
    uint32_t client_id
) {
    if (!ctx) {
        SET_ERRNO(ERROR_INTERNAL, "Invalid server context");
        return NULL;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (ctx->clients[i].in_use && ctx->clients[i].client_id == client_id) {
            return (h265_server_client_t *)&ctx->clients[i];
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!ctx->clients[i].in_use) {
            h265_decoder_t *decoder = h265_decoder_create();
            if (!decoder) {
                SET_ERRNO(ERROR_MEDIA_INIT, "Failed to create decoder for client %u", client_id);
                return NULL;
            }

            ctx->clients[i].client_id = client_id;
            ctx->clients[i].decoder = decoder;
            ctx->clients[i].in_use = true;
            ctx->clients[i].last_width = 0;
            ctx->clients[i].last_height = 0;
            ctx->active_client_count++;

            log_debug("Created H.265 decoder for client %u", client_id);
            return (h265_server_client_t *)&ctx->clients[i];
        }
    }

    SET_ERRNO(ERROR_INTERNAL, "No free decoder slots (max %d clients)", MAX_CLIENTS);
    return NULL;
}

asciichat_error_t h265_server_decode_and_convert(
    h265_server_client_t *client,
    const uint8_t *h265_packet,
    size_t packet_size,
    uint8_t *output_rgba,
    uint16_t *output_width,
    uint16_t *output_height,
    size_t *output_size
) {
    if (!client || !h265_packet || !output_rgba || !output_width || !output_height || !output_size) {
        return SET_ERRNO(ERROR_INTERNAL, "Invalid arguments to decode_and_convert");
    }

    h265_client_decoder_t *client_dec = (h265_client_decoder_t *)client;
    if (!client_dec->decoder || !client_dec->in_use) {
        return SET_ERRNO(ERROR_INTERNAL, "Invalid client decoder state");
    }

    uint8_t ascii_buf[256 * 64];
    size_t ascii_size = sizeof(ascii_buf);

    asciichat_error_t result = h265_decode(
        client_dec->decoder,
        h265_packet,
        packet_size,
        output_width,
        output_height,
        ascii_buf,
        &ascii_size
    );

    if (result != ASCIICHAT_OK) {
        return result;
    }

    size_t required_rgba_size = (size_t)(*output_width) * (*output_height) * 4;
    if (*output_size < required_rgba_size) {
        *output_size = required_rgba_size;
        return SET_ERRNO(ERROR_NETWORK_SIZE, "Output buffer too small: need %zu, have %zu",
                        required_rgba_size, *output_size);
    }

    for (size_t i = 0; i < ascii_size && i < (size_t)(*output_width) * (*output_height); i++) {
        uint8_t val = ascii_buf[i];
        output_rgba[i * 4 + 0] = val;
        output_rgba[i * 4 + 1] = val;
        output_rgba[i * 4 + 2] = val;
        output_rgba[i * 4 + 3] = 255;
    }

    *output_size = required_rgba_size;
    client_dec->last_width = *output_width;
    client_dec->last_height = *output_height;

    return ASCIICHAT_OK;
}

void h265_server_remove_client(h265_server_context_t *ctx, uint32_t client_id) {
    if (!ctx) return;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (ctx->clients[i].in_use && ctx->clients[i].client_id == client_id) {
            if (ctx->clients[i].decoder) {
                h265_decoder_destroy(ctx->clients[i].decoder);
                ctx->clients[i].decoder = NULL;
            }
            ctx->clients[i].in_use = false;
            ctx->active_client_count--;
            log_debug("Removed H.265 decoder for client %u", client_id);
            return;
        }
    }
}

void h265_server_client_get_stats(
    h265_server_client_t *client,
    uint64_t *total_frames,
    uint64_t *keyframes,
    uint16_t *last_width,
    uint16_t *last_height
) {
    if (!client) return;

    h265_client_decoder_t *client_dec = (h265_client_decoder_t *)client;
    if (!client_dec->decoder) return;

    h265_decoder_get_stats(client_dec->decoder, total_frames, keyframes, last_width, last_height);
}
