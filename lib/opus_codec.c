/**
 * @file opus_codec.c
 * @brief Opus audio codec implementation
 * @ingroup audio
 */

#include "opus_codec.h"
#include "common.h"
#include "asciichat_errno.h"
#include <opus/opus.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Encoder Creation
 * ============================================================================ */

opus_codec_t *opus_codec_create(opus_application_t application, int sample_rate, int bitrate) {
  if (sample_rate <= 0 || bitrate <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid codec parameters: sample_rate=%d, bitrate=%d", sample_rate, bitrate);
    return NULL;
  }

  opus_codec_t *codec = SAFE_MALLOC(sizeof(opus_codec_t), opus_codec_t *);
  if (!codec) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate opus codec context");
    return NULL;
  }

  // Create encoder
  int error = OPUS_OK;
  codec->encoder = opus_encoder_create(sample_rate, 1, (int)application, &error);
  if (error != OPUS_OK || !codec->encoder) {
    SET_ERRNO(ERROR_AUDIO, "Failed to create Opus encoder: %s", opus_strerror(error));
    SAFE_FREE(codec, sizeof(opus_codec_t));
    return NULL;
  }

  // Set bitrate
  error = opus_encoder_ctl(codec->encoder, OPUS_SET_BITRATE(bitrate));
  if (error != OPUS_OK) {
    SET_ERRNO(ERROR_AUDIO, "Failed to set Opus bitrate: %s", opus_strerror(error));
    opus_encoder_destroy(codec->encoder);
    SAFE_FREE(codec, sizeof(opus_codec_t));
    return NULL;
  }

  codec->decoder = NULL;
  codec->sample_rate = sample_rate;
  codec->bitrate = bitrate;
  codec->tmp_buffer = NULL;

  log_debug("Opus encoder created: sample_rate=%d, bitrate=%d bps", sample_rate, bitrate);

  return codec;
}

/* ============================================================================
 * Decoder Creation
 * ============================================================================ */

opus_codec_t *opus_codec_create_decoder(int sample_rate) {
  if (sample_rate <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid sample rate: %d", sample_rate);
    return NULL;
  }

  opus_codec_t *codec = SAFE_MALLOC(sizeof(opus_codec_t), opus_codec_t *);
  if (!codec) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate opus codec context");
    return NULL;
  }

  // Create decoder
  int error = OPUS_OK;
  codec->decoder = opus_decoder_create(sample_rate, 1, &error);
  if (error != OPUS_OK || !codec->decoder) {
    SET_ERRNO(ERROR_AUDIO, "Failed to create Opus decoder: %s", opus_strerror(error));
    SAFE_FREE(codec, sizeof(opus_codec_t));
    return NULL;
  }

  codec->encoder = NULL;
  codec->sample_rate = sample_rate;
  codec->bitrate = 0;  // N/A for decoder
  codec->tmp_buffer = NULL;

  log_debug("Opus decoder created: sample_rate=%d", sample_rate);

  return codec;
}

/* ============================================================================
 * Encoding
 * ============================================================================ */

size_t opus_codec_encode(opus_codec_t *codec, const float *samples, int num_samples,
                         uint8_t *out_data, size_t out_size) {
  if (!codec || !codec->encoder || !samples || num_samples <= 0 || !out_data || out_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "Invalid encode parameters: codec=%p, encoder=%p, samples=%p, num_samples=%d, out_data=%p, "
              "out_size=%zu",
              (void *)codec, (void *)(codec ? codec->encoder : NULL), (const void *)samples, num_samples,
              (void *)out_data, out_size);
    return 0;
  }

  // Encode frame
  opus_int32 encoded_bytes = opus_encode_float(codec->encoder, samples, num_samples, out_data, (opus_int32)out_size);

  if (encoded_bytes < 0) {
    SET_ERRNO(ERROR_AUDIO, "Opus encoding failed: %s", opus_strerror((int)encoded_bytes));
    return 0;
  }

  if (encoded_bytes == 0) {
    // DTX frame (silence) - encoder produced zero bytes
    log_debug_every(100000, "Opus DTX frame (silence detected)");
  }

  return (size_t)encoded_bytes;
}

/* ============================================================================
 * Decoding
 * ============================================================================ */

int opus_codec_decode(opus_codec_t *codec, const uint8_t *data, size_t data_len, float *out_samples,
                      int out_num_samples) {
  if (!codec || !codec->decoder || !out_samples || out_num_samples <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "Invalid decode parameters: codec=%p, decoder=%p, out_samples=%p, out_num_samples=%d", (void *)codec,
              (void *)(codec ? codec->decoder : NULL), (void *)out_samples, out_num_samples);
    return -1;
  }

  // If data is NULL, use PLC (Packet Loss Concealment)
  if (!data || data_len == 0) {
    log_debug_every(100000, "Opus PLC (Packet Loss Concealment)");
    int samples = opus_decode_float(codec->decoder, NULL, 0, out_samples, out_num_samples, 0);
    if (samples < 0) {
      SET_ERRNO(ERROR_AUDIO, "Opus PLC failed: %s", opus_strerror(samples));
      return -1;
    }
    return samples;
  }

  // Decode frame
  int samples = opus_decode_float(codec->decoder, data, (opus_int32)data_len, out_samples, out_num_samples, 0);

  if (samples < 0) {
    SET_ERRNO(ERROR_AUDIO, "Opus decoding failed: %s", opus_strerror(samples));
    return -1;
  }

  return samples;
}

/* ============================================================================
 * Configuration
 * ============================================================================ */

asciichat_error_t opus_codec_set_bitrate(opus_codec_t *codec, int bitrate) {
  if (!codec || !codec->encoder || bitrate <= 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid bitrate parameters: codec=%p, encoder=%p, bitrate=%d",
                     (void *)codec, (void *)(codec ? codec->encoder : NULL), bitrate);
  }

  int error = opus_encoder_ctl(codec->encoder, OPUS_SET_BITRATE(bitrate));
  if (error != OPUS_OK) {
    return SET_ERRNO(ERROR_AUDIO, "Failed to set Opus bitrate: %s", opus_strerror(error));
  }

  codec->bitrate = bitrate;
  log_debug("Opus bitrate changed to %d bps", bitrate);

  return ASCIICHAT_OK;
}

int opus_codec_get_bitrate(opus_codec_t *codec) {
  if (!codec || !codec->encoder) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid codec for bitrate query");
    return -1;
  }

  opus_int32 bitrate = 0;
  int error = opus_encoder_ctl(codec->encoder, OPUS_GET_BITRATE(&bitrate));
  if (error != OPUS_OK) {
    SET_ERRNO(ERROR_AUDIO, "Failed to get Opus bitrate: %s", opus_strerror(error));
    return -1;
  }

  return (int)bitrate;
}

asciichat_error_t opus_codec_set_dtx(opus_codec_t *codec, int enable) {
  if (!codec || !codec->encoder) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid codec for DTX configuration");
  }

  int error = opus_encoder_ctl(codec->encoder, OPUS_SET_DTX(enable ? 1 : 0));
  if (error != OPUS_OK) {
    return SET_ERRNO(ERROR_AUDIO, "Failed to set Opus DTX: %s", opus_strerror(error));
  }

  log_debug("Opus DTX %s", enable ? "enabled" : "disabled");

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

void opus_codec_destroy(opus_codec_t *codec) {
  if (!codec) {
    return;
  }

  if (codec->encoder) {
    opus_encoder_destroy(codec->encoder);
    codec->encoder = NULL;
  }

  if (codec->decoder) {
    opus_decoder_destroy(codec->decoder);
    codec->decoder = NULL;
  }

  if (codec->tmp_buffer) {
    SAFE_FREE(codec->tmp_buffer, 0);
    codec->tmp_buffer = NULL;
  }

  SAFE_FREE(codec, sizeof(opus_codec_t));
}
