/**
 * @file video/x265/encoder.c
 * @brief x265 HEVC encoder for ASCII art frames
 */

#include <ascii-chat/video/x265/encoder.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/util/time.h>
#include <x265.h>
#include <string.h>
#include <stdlib.h>

typedef struct x265_encoder {
    x265_encoder *handle;
    x265_param *params;

    uint16_t current_width;
    uint16_t current_height;

    uint8_t *yuv_buf;
    size_t yuv_buf_size;

    bool request_keyframe;

    uint64_t total_frames;
    uint64_t keyframes;
    uint32_t avg_bitrate;
} x265_encoder_t;

x265_encoder_t *x265_encoder_create(uint16_t initial_width, uint16_t initial_height) {
    if (initial_width == 0 || initial_height == 0) {
        SET_ERRNO(ERROR_MEDIA_INIT, "Frame dimensions must be non-zero");
        return NULL;
    }

    x265_encoder_t *enc = SAFE_CALLOC(1, sizeof(x265_encoder_t), x265_encoder_t *);
    if (!enc) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate encoder structure");
        return NULL;
    }

    enc->params = x265_param_alloc();
    if (!enc->params) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate x265 parameters");
        SAFE_FREE(enc);
        return NULL;
    }

    if (x265_param_default_preset(enc->params, "ultrafast", "zerolatency") < 0) {
        SET_ERRNO(ERROR_MEDIA_INIT, "Failed to set x265 preset");
        x265_param_free(enc->params);
        SAFE_FREE(enc);
        return NULL;
    }

    enc->params->sourceWidth = initial_width;
    enc->params->sourceHeight = initial_height;
    enc->params->fpsNum = 30;
    enc->params->fpsDenom = 1;
    enc->params->bRepeatHeaders = 1;
    enc->params->logLevel = X265_LOG_ERROR;

    enc->handle = x265_encoder_open(enc->params);
    if (!enc->handle) {
        SET_ERRNO(ERROR_MEDIA_INIT, "Failed to open x265 encoder");
        x265_param_free(enc->params);
        SAFE_FREE(enc);
        return NULL;
    }

    enc->current_width = initial_width;
    enc->current_height = initial_height;

    size_t stride = initial_width;
    enc->yuv_buf_size = stride * initial_height * 3 / 2;
    enc->yuv_buf = SAFE_MALLOC(enc->yuv_buf_size, uint8_t *);
    if (!enc->yuv_buf) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate YUV buffer");
        x265_encoder_close(enc->handle);
        x265_param_free(enc->params);
        SAFE_FREE(enc);
        return NULL;
    }

    log_info("x265 encoder created: %ux%u", initial_width, initial_height);
    return enc;
}

void x265_encoder_destroy(x265_encoder_t *encoder) {
    if (!encoder) return;

    if (encoder->handle) {
        x265_encoder_close(encoder->handle);
    }
    if (encoder->params) {
        x265_param_free(encoder->params);
    }
    if (encoder->yuv_buf) {
        SAFE_FREE(encoder->yuv_buf);
    }
    SAFE_FREE(encoder);
}

static asciichat_error_t x265_encoder_reconfigure(
    x265_encoder_t *encoder,
    uint16_t new_width,
    uint16_t new_height
) {
    if (encoder->current_width == new_width && encoder->current_height == new_height) {
        return ASCIICHAT_OK;
    }

    x265_encoder_close(encoder->handle);

    encoder->params->sourceWidth = new_width;
    encoder->params->sourceHeight = new_height;

    encoder->handle = x265_encoder_open(encoder->params);
    if (!encoder->handle) {
        return SET_ERRNO(ERROR_MEDIA_INIT, "Failed to reopen x265 encoder for %ux%u",
                        new_width, new_height);
    }

    size_t new_yuv_size = new_width * new_height * 3 / 2;
    if (new_yuv_size > encoder->yuv_buf_size) {
        uint8_t *new_buf = SAFE_MALLOC(new_yuv_size, uint8_t *);
        if (!new_buf) {
            return SET_ERRNO(ERROR_MEMORY, "Failed to allocate larger YUV buffer");
        }
        SAFE_FREE(encoder->yuv_buf);
        encoder->yuv_buf = new_buf;
        encoder->yuv_buf_size = new_yuv_size;
    }

    encoder->current_width = new_width;
    encoder->current_height = new_height;
    encoder->request_keyframe = true;

    log_info("x265 encoder reconfigured to %ux%u", new_width, new_height);
    return ASCIICHAT_OK;
}

static void x265_encoder_ascii_to_yuv420(
    const uint8_t *ascii_data,
    uint16_t width,
    uint16_t height,
    uint8_t *yuv_buf
) {
    uint8_t *y_plane = yuv_buf;
    uint8_t *u_plane = y_plane + width * height;
    uint8_t *v_plane = u_plane + (width / 2) * (height / 2);

    memcpy(y_plane, ascii_data, width * height);

    memset(u_plane, 128, (width / 2) * (height / 2));
    memset(v_plane, 128, (width / 2) * (height / 2));
}

asciichat_error_t x265_encoder_encode(
    x265_encoder_t *encoder,
    uint16_t width,
    uint16_t height,
    const uint8_t *ascii_data,
    uint8_t *output_buf,
    size_t *output_size
) {
    if (!encoder || !ascii_data || !output_buf || !output_size) {
        return SET_ERRNO(ERROR_INTERNAL, "Invalid encoder arguments");
    }

    if (*output_size < 5) {
        return SET_ERRNO(ERROR_NETWORK_SIZE, "Output buffer too small (minimum 5 bytes)");
    }

    asciichat_error_t result = x265_encoder_reconfigure(encoder, width, height);
    if (result != ASCIICHAT_OK) {
        return result;
    }

    x265_encoder_ascii_to_yuv420(ascii_data, width, height, encoder->yuv_buf);

    x265_picture pic_in;
    x265_picture pic_out;
    x265_picture_init(encoder->params, &pic_in);

    pic_in.planes[0] = encoder->yuv_buf;
    pic_in.planes[1] = encoder->yuv_buf + width * height;
    pic_in.planes[2] = encoder->yuv_buf + width * height + (width / 2) * (height / 2);
    pic_in.stride[0] = width;
    pic_in.stride[1] = width / 2;
    pic_in.stride[2] = width / 2;

    if (encoder->request_keyframe) {
        pic_in.sliceType = X265_TYPE_IDR;
        encoder->request_keyframe = false;
    }

    x265_nal *nal_out;
    uint32_t nal_count;

    int frame_size = x265_encoder_encode(encoder->handle, &pic_in, &pic_out, &nal_out, &nal_count);
    if (frame_size < 0) {
        return SET_ERRNO(ERROR_MEDIA_DECODE, "x265 encoding failed");
    }

    uint8_t flags = 0;
    if (pic_out.sliceType == X265_TYPE_IDR || pic_out.sliceType == X265_TYPE_I) {
        flags |= X265_ENCODER_FLAG_KEYFRAME;
        encoder->keyframes++;
    }

    if (width != encoder->current_width || height != encoder->current_height) {
        flags |= X265_ENCODER_FLAG_SIZE_CHANGE;
    }

    size_t header_size = 5;
    size_t required_size = header_size + (size_t)frame_size;

    if (required_size > *output_size) {
        return SET_ERRNO(ERROR_NETWORK_SIZE, "Output buffer too small: need %zu, have %zu",
                        required_size, *output_size);
    }

    output_buf[0] = flags;
    output_buf[1] = (width >> 8) & 0xFF;
    output_buf[2] = width & 0xFF;
    output_buf[3] = (height >> 8) & 0xFF;
    output_buf[4] = height & 0xFF;

    if (frame_size > 0) {
        uint8_t *nal_data = (uint8_t *)nal_out[0].payload;
        memcpy(output_buf + header_size, nal_data, frame_size);
    }

    *output_size = required_size;
    encoder->total_frames++;

    return ASCIICHAT_OK;
}

void x265_encoder_request_keyframe(x265_encoder_t *encoder) {
    if (encoder) {
        encoder->request_keyframe = true;
    }
}

void x265_encoder_get_stats(
    x265_encoder_t *encoder,
    uint64_t *total_frames,
    uint64_t *keyframes,
    uint32_t *avg_bitrate
) {
    if (!encoder) return;

    if (total_frames) *total_frames = encoder->total_frames;
    if (keyframes) *keyframes = encoder->keyframes;
    if (avg_bitrate) *avg_bitrate = encoder->avg_bitrate;
}
