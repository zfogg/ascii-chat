/**
 * @file opus_codec.h
 * @brief Opus audio codec wrapper for real-time encoding/decoding
 * @ingroup audio
 * @addtogroup audio
 * @{
 *
 * Provides high-level interface to libopus for encoding and decoding
 * audio frames with configurable bitrate and frame sizes.
 *
 * OPUS CODEC FEATURES:
 * ====================
 * - Real-time audio compression with minimal latency
 * - Flexible bitrate from 6 kbps (voice) to 128+ kbps (music)
 * - Adaptive frame sizes (10ms, 20ms, 40ms, 60ms)
 * - Automatic detection of voice vs music content
 * - Graceful handling of packet loss
 *
 * USAGE EXAMPLE:
 * ==============
 * @code
 * // Create encoder for voice at 24 kbps
 * opus_codec_t *encoder = opus_codec_create(OPUS_APPLICATION_VOIP, 44100, 24000);
 *
 * // Encode audio (882 samples = 20ms at 44.1kHz)
 * float samples[882];
 * uint8_t compressed[250];
 * size_t encoded_bytes = opus_codec_encode(encoder, samples, 882, compressed, 250);
 *
 * // Send over network...
 *
 * // Create decoder
 * opus_codec_t *decoder = opus_codec_create_decoder(44100);
 *
 * // Decode received audio
 * float decoded[882];
 * int decoded_samples = opus_codec_decode(decoder, compressed, encoded_bytes, decoded, 882);
 *
 * // Cleanup
 * opus_codec_destroy(encoder);
 * opus_codec_destroy(decoder);
 * @endcode
 *
 * @note Thread-safety: Each codec instance must not be accessed from multiple threads
 *       simultaneously. Create separate encoder and decoder instances per thread if needed.
 *
 * @note Frame sizes: Opus works with fixed frame sizes. Recommended:
 *       - 20ms (882 samples @ 44.1kHz) for voice
 *       - 40ms (1764 samples @ 44.1kHz) for low-latency music
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "common.h"

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct OpusEncoder OpusEncoder;
typedef struct OpusDecoder OpusDecoder;

/* ============================================================================
 * Opus Application Modes
 * ============================================================================ */

/**
 * @brief Application mode for opus encoder
 * @ingroup audio
 */
typedef enum {
  OPUS_APPLICATION_VOIP = 2048,      ///< Voice over IP (optimized for speech)
  OPUS_APPLICATION_AUDIO = 2049,     ///< General audio (optimized for music)
  OPUS_APPLICATION_RESTRICTED_LOWDELAY = 2051  ///< Low-latency mode
} opus_application_t;

/* ============================================================================
 * Codec Context Structure
 * ============================================================================ */

/**
 * @brief Opus codec context for encoding or decoding
 *
 * Opaque structure that holds the state of an Opus encoder or decoder.
 * Use opus_codec_create() or opus_codec_create_decoder() to initialize.
 *
 * @note This is an opaque type. Do not access fields directly.
 * @ingroup audio
 */
typedef struct {
  OpusEncoder *encoder;      ///< Encoder state (NULL if decoder)
  OpusDecoder *decoder;      ///< Decoder state (NULL if encoder)
  int sample_rate;           ///< Sample rate in Hz (e.g., 44100)
  int bitrate;               ///< Bitrate in bits per second (encoder only)
  uint8_t *tmp_buffer;       ///< Temporary buffer for internal use
} opus_codec_t;

/* ============================================================================
 * Encoder Functions
 * ============================================================================ */

/**
 * @brief Create an Opus encoder
 * @param application Application mode (OPUS_APPLICATION_VOIP for voice)
 * @param sample_rate Sample rate in Hz (8000, 12000, 16000, 24000, or 48000)
 * @param bitrate Target bitrate in bits per second (6000-128000 typical)
 * @return Pointer to new encoder, NULL on error
 *
 * Creates a new Opus encoder instance for compressing audio.
 *
 * @note Common bitrates:
 *   - 16 kbps: Good quality voice
 *   - 24 kbps: Excellent quality voice
 *   - 64 kbps: High quality audio
 *
 * @warning Returned pointer must be freed with opus_codec_destroy()
 *
 * @ingroup audio
 */
opus_codec_t *opus_codec_create(opus_application_t application, int sample_rate, int bitrate);

/**
 * @brief Create an Opus decoder
 * @param sample_rate Sample rate in Hz (must match encoder)
 * @return Pointer to new decoder, NULL on error
 *
 * Creates a new Opus decoder instance for decompressing audio.
 *
 * @warning Returned pointer must be freed with opus_codec_destroy()
 *
 * @ingroup audio
 */
opus_codec_t *opus_codec_create_decoder(int sample_rate);

/**
 * @brief Encode audio frame with Opus
 * @param codec Opus encoder (created with opus_codec_create)
 * @param samples Input audio samples (float, -1.0 to 1.0 range)
 * @param num_samples Number of input samples (882 for 20ms @ 44.1kHz)
 * @param out_data Output buffer for compressed audio
 * @param out_size Maximum output buffer size in bytes
 * @return Number of bytes written to out_data, negative on error
 *
 * Encodes a frame of audio samples using Opus compression.
 *
 * @note Input must be exactly the correct frame size (typically 882 samples)
 * @note Output is typically 30-100 bytes depending on bitrate and content
 * @note This function is NOT thread-safe for the same codec instance
 *
 * @ingroup audio
 */
size_t opus_codec_encode(opus_codec_t *codec, const float *samples, int num_samples,
                         uint8_t *out_data, size_t out_size);

/* ============================================================================
 * Decoder Functions
 * ============================================================================ */

/**
 * @brief Decode Opus audio frame
 * @param codec Opus decoder (created with opus_codec_create_decoder)
 * @param data Compressed audio data (or NULL for PLC)
 * @param data_len Length of compressed data in bytes
 * @param out_samples Output buffer for decoded samples (float)
 * @param out_num_samples Maximum number of output samples
 * @return Number of samples decoded, negative on error
 *
 * Decodes a compressed Opus frame back to PCM audio samples.
 *
 * @note To handle packet loss, pass NULL for data to enable PLC (Packet Loss Concealment)
 * @note Output will be exactly the frame size (typically 882 samples)
 * @note This function is NOT thread-safe for the same codec instance
 *
 * @ingroup audio
 */
int opus_codec_decode(opus_codec_t *codec, const uint8_t *data, size_t data_len,
                      float *out_samples, int out_num_samples);

/* ============================================================================
 * Configuration Functions
 * ============================================================================ */

/**
 * @brief Set encoder bitrate
 * @param codec Opus encoder (created with opus_codec_create)
 * @param bitrate New bitrate in bits per second
 * @return 0 on success, negative on error
 *
 * Changes the bitrate of an active encoder. This can be used to dynamically
 * adjust quality based on network conditions.
 *
 * @ingroup audio
 */
asciichat_error_t opus_codec_set_bitrate(opus_codec_t *codec, int bitrate);

/**
 * @brief Get current encoder bitrate
 * @param codec Opus encoder
 * @return Bitrate in bits per second, negative on error
 *
 * @ingroup audio
 */
int opus_codec_get_bitrate(opus_codec_t *codec);

/**
 * @brief Enable/disable DTX (Discontinuous Transmission)
 * @param codec Opus encoder
 * @param enable 1 to enable DTX, 0 to disable
 * @return 0 on success, negative on error
 *
 * DTX allows the encoder to produce zero-byte frames during silence,
 * reducing bandwidth usage significantly for voice communication.
 *
 * @ingroup audio
 */
asciichat_error_t opus_codec_set_dtx(opus_codec_t *codec, int enable);

/* ============================================================================
 * Lifecycle Functions
 * ============================================================================ */

/**
 * @brief Destroy an Opus codec instance
 * @param codec Codec to destroy (can be NULL)
 *
 * Frees all resources associated with the codec. Safe to call multiple times
 * or with NULL pointer.
 *
 * @ingroup audio
 */
void opus_codec_destroy(opus_codec_t *codec);

/** @} */
