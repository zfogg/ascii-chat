/**
 * @file video/h265/decoder.c
 * @brief FFmpeg HEVC decoder for ASCII art frames
 */

#include <ascii-chat/video/h265/decoder.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/platform/system.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>

#include <string.h>
#include <stdlib.h>

typedef struct h265_decoder {
    AVCodecContext *codec_ctx;
    AVFrame        *frame;
    AVPacket       *packet;
    AVCodecParserContext *parser;

    uint16_t last_width;
    uint16_t last_height;

    uint64_t total_frames;
    uint64_t keyframes;
} h265_decoder_t;

h265_decoder_t *h265_decoder_create(void) {
    h265_decoder_t *dec = SAFE_CALLOC(1, sizeof(h265_decoder_t), h265_decoder_t *);
    if (!dec) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate decoder structure");
        return NULL;
    }

    // Find HEVC decoder
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    if (!codec) {
        SET_ERRNO(ERROR_MEDIA_INIT, "HEVC decoder not found");
        SAFE_FREE(dec);
        return NULL;
    }

    dec->codec_ctx = avcodec_alloc_context3(codec);
    if (!dec->codec_ctx) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate codec context");
        SAFE_FREE(dec);
        return NULL;
    }

    // Open decoder (suppress x265 library stderr output during initialization)
    platform_stderr_redirect_handle_t h265_stderr = platform_stdout_stderr_redirect_to_null();
    int codec_open_result = avcodec_open2(dec->codec_ctx, codec, NULL);
    platform_stdout_stderr_restore(h265_stderr);

    if (codec_open_result < 0) {
        SET_ERRNO(ERROR_MEDIA_INIT, "Failed to open HEVC decoder");
        avcodec_free_context(&dec->codec_ctx);
        SAFE_FREE(dec);
        return NULL;
    }

    // Allocate frame and packet
    dec->frame = av_frame_alloc();
    dec->packet = av_packet_alloc();
    dec->parser = av_parser_init(AV_CODEC_ID_HEVC);

    if (!dec->frame || !dec->packet || !dec->parser) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate frame/packet/parser");
        if (dec->frame) av_frame_free(&dec->frame);
        if (dec->packet) av_packet_free(&dec->packet);
        if (dec->parser) av_parser_close(dec->parser);
        avcodec_free_context(&dec->codec_ctx);
        SAFE_FREE(dec);
        return NULL;
    }

    log_info("FFmpeg HEVC decoder created");
    return dec;
}

void h265_decoder_destroy(h265_decoder_t *decoder) {
    if (!decoder) return;

    if (decoder->frame) {
        av_frame_free(&decoder->frame);
    }
    if (decoder->packet) {
        av_packet_free(&decoder->packet);
    }
    if (decoder->parser) {
        av_parser_close(decoder->parser);
    }
    if (decoder->codec_ctx) {
        avcodec_free_context(&decoder->codec_ctx);
    }
    SAFE_FREE(decoder);
}

asciichat_error_t h265_decode(
    h265_decoder_t *decoder,
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

    // Parse header
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

    // Decode HEVC data
    if (packet_size > 5) {
        const uint8_t *hevc_data = encoded_packet + 5;
        size_t hevc_size = packet_size - 5;

        // Send packet to decoder
        av_packet_from_data(decoder->packet, (uint8_t *)hevc_data, hevc_size);

        if (avcodec_send_packet(decoder->codec_ctx, decoder->packet) < 0) {
            return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to send packet to decoder");
        }

        if (avcodec_receive_frame(decoder->codec_ctx, decoder->frame) < 0) {
            return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to receive decoded frame");
        }
    } else {
        // Empty packet - just signal a frame boundary
        if (avcodec_send_packet(decoder->codec_ctx, NULL) < 0) {
            return SET_ERRNO(ERROR_MEDIA_DECODE, "Failed to flush decoder");
        }
    }

    // Extract Y plane (grayscale intensity)
    uint8_t *y_plane = decoder->frame->data[0];
    int y_stride = decoder->frame->linesize[0];
    int img_width = decoder->frame->width;
    int img_height = decoder->frame->height;

    if (img_width != (int)width || img_height != (int)height) {
        log_warn("Image dimensions mismatch: expected %ux%u, got %dx%d", width, height, img_width, img_height);
    }

    // Copy Y plane to output (ASCII intensity values)
    for (int y = 0; y < img_height && y < (int)height; y++) {
        memcpy(output_buf + y * width, y_plane + y * y_stride, width);
    }

    *output_width = width;
    *output_height = height;
    *output_size = width * height;

    decoder->last_width = width;
    decoder->last_height = height;
    decoder->total_frames++;

    if (flags & H265_DECODER_FLAG_KEYFRAME) {
        decoder->keyframes++;
    }

    return ASCIICHAT_OK;
}

void h265_decoder_get_stats(
    h265_decoder_t *decoder,
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
