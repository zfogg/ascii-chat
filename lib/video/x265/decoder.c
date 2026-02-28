/**
 * @file video/x265/decoder.c
 * @brief libde265 HEVC decoder for ASCII art frames
 */

#include <ascii-chat/video/x265/decoder.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <libde265/de265.h>
#include <string.h>
#include <stdlib.h>

typedef struct x265_decoder {
    de265_decoder_context *context;

    uint16_t last_width;
    uint16_t last_height;

    uint64_t total_frames;
    uint64_t keyframes;
} x265_decoder_t;

x265_decoder_t *x265_decoder_create(void) {
    x265_decoder_t *dec = SAFE_CALLOC(1, sizeof(x265_decoder_t), x265_decoder_t *);
    if (!dec) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate decoder structure");
        return NULL;
    }

    dec->context = de265_new_decoder();
    if (!dec->context) {
        SET_ERRNO(ERROR_MEDIA_INIT, "Failed to create libde265 decoder");
        SAFE_FREE(dec);
        return NULL;
    }

    de265_set_parameter_bool(dec->context, DE265_DECODER_PARAM_DISABLE_DEBLOCKING, 0);

    log_info("libde265 decoder created");
    return dec;
}

void x265_decoder_destroy(x265_decoder_t *decoder) {
    if (!decoder) return;

    if (decoder->context) {
        de265_free_decoder(decoder->context);
    }
    SAFE_FREE(decoder);
}

asciichat_error_t x265_decode(
    x265_decoder_t *decoder,
    const uint8_t *encoded_packet,
    size_t packet_size,
    uint16_t *output_width,
    uint16_t *output_height,
    uint8_t *output_buf,
    size_t *output_size
) {
    if (!decoder || !encoded_packet || !output_width || !output_height || !output_buf || !output_size) {
        return SET_ERRNO(ERROR_INTERNAL, "Invalid decoder arguments");
    }

    if (packet_size < 5) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Packet too small (minimum 5 bytes)");
    }

    uint8_t flags = encoded_packet[0];
    uint16_t width = ((uint16_t)encoded_packet[1] << 8) | encoded_packet[2];
    uint16_t height = ((uint16_t)encoded_packet[3] << 8) | encoded_packet[4];

    if (width == 0 || height == 0) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid frame dimensions: %ux%u", width, height);
    }

    size_t required_output = width * height;
    if (*output_size < required_output) {
        *output_size = required_output;
        return SET_ERRNO(ERROR_NETWORK_SIZE, "Output buffer too small: need %zu, have %zu",
                        required_output, *output_size);
    }

    if (packet_size > 5) {
        const uint8_t *nal_data = encoded_packet + 5;
        size_t nal_size = packet_size - 5;

        de265_error err = de265_push_NAL(decoder->context, nal_data, nal_size, 0, NULL);
        if (err != DE265_OK) {
            return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to push NAL: %s", de265_get_error_text(err));
        }

        err = de265_decode(decoder->context, NULL);
        if (err != DE265_OK && err != DE265_ERROR_WAITING_FOR_INPUT_DATA) {
            return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to decode: %s", de265_get_error_text(err));
        }
    }

    const de265_image *img = de265_get_next_picture(decoder->context);
    if (!img) {
        return SET_ERRNO(ERROR_MEDIA_DECODE, "No decoded image available");
    }

    int img_width = de265_get_image_width(img, 0);
    int img_height = de265_get_image_height(img, 0);

    if (img_width != (int)width || img_height != (int)height) {
        log_warn("Image dimensions mismatch: expected %ux%u, got %dx%d", width, height, img_width, img_height);
    }

    const uint8_t *y_plane = de265_get_image_plane(img, 0, NULL);
    int y_stride = de265_get_image_stride(img, 0);

    for (int y = 0; y < img_height && y < (int)height; y++) {
        memcpy(output_buf + y * width, y_plane + y * y_stride, width);
    }

    *output_width = width;
    *output_height = height;
    *output_size = width * height;

    decoder->last_width = width;
    decoder->last_height = height;
    decoder->total_frames++;

    if (flags & X265_DECODER_FLAG_KEYFRAME) {
        decoder->keyframes++;
    }

    return ASCIICHAT_OK;
}

void x265_decoder_get_stats(
    x265_decoder_t *decoder,
    uint64_t *total_frames,
    uint64_t *keyframes,
    uint16_t *last_width,
    uint16_t *last_height
) {
    if (!decoder) return;

    if (total_frames) *total_frames = decoder->total_frames;
    if (keyframes) *keyframes = decoder->keyframes;
    if (last_width) *last_width = decoder->last_width;
    if (last_height) *last_height = decoder->last_height;
}
