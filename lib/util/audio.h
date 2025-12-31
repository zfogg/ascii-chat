#pragma once
/**
 * @file util/audio.h
 * @brief 🔊 Audio packet parsing utilities
 * @ingroup util
 *
 * Provides utility functions and macros for parsing audio batch packets.
 * These helpers are used in both server and client protocol handlers to
 * parse audio batch headers with proper validation and error handling.
 *
 * Audio Batch Packet Format:
 * The audio batch packet contains multiple audio frames in a single packet
 * to improve bandwidth efficiency. The packet structure includes:
 * - batch_count: Number of individual audio frames in this batch
 * - total_samples: Total number of audio samples across all frames
 * - sample_rate: Sample rate (Hz) for this batch
 * - channels: Number of audio channels (mono=1, stereo=2)
 * - Followed by: Packed audio frames
 *
 * Common Pattern:
 * Both src/server/protocol.c and src/client/protocol.c have nearly identical
 * code for unpacking audio batch headers:
 *
 * Before (duplicated in 2 files):
 * @code
 * const audio_batch_packet_t *batch_header = (const audio_batch_packet_t *)data;
 * uint32_t batch_count = ntohl(batch_header->batch_count);
 * uint32_t total_samples = ntohl(batch_header->total_samples);
 * uint32_t sample_rate = ntohl(batch_header->sample_rate);
 * uint32_t channels = ntohl(batch_header->channels);
 * @endcode
 *
 * After (using helpers):
 * @code
 * audio_batch_info_t batch_info;
 * asciichat_error_t result = audio_parse_batch_header(data, len, &batch_info);
 * if (result != ASCIICHAT_OK) {
 *     log_error("Invalid audio batch header");
 *     return;
 * }
 * @endcode
 *
 * Usage:
 * @code
 * void handle_audio_batch_packet(const void *data, size_t len) {
 *     audio_batch_info_t batch;
 *     asciichat_error_t result = audio_parse_batch_header(data, len, &batch);
 *     if (result != ASCIICHAT_OK) {
 *         log_error("Bad audio batch header");
 *         return;
 *     }
 *
 *     if (batch.batch_count > MAX_BATCH_SIZE) {
 *         log_error("Batch count too large: %u", batch.batch_count);
 *         return;
 *     }
 *
 *     // Process batch
 *     for (uint32_t i = 0; i < batch.batch_count; i++) {
 *         // Process individual frame i
 *     }
 * }
 * @endcode
 */

#pragma once

#include <stdint.h>
#include <string.h>
#include "asciichat_errno.h"

/**
 * Parsed audio batch packet header information.
 * Contains unpacked and validated audio batch metadata.
 */
typedef struct {
  uint32_t batch_count;   ///< Number of audio frames in this batch
  uint32_t total_samples; ///< Total number of samples across all frames
  uint32_t sample_rate;   ///< Sample rate in Hz (e.g., 48000)
  uint32_t channels;      ///< Number of channels (1=mono, 2=stereo)
} audio_batch_info_t;

/**
 * Parse an audio batch packet header from raw packet data.
 *
 * Performs the following validations:
 * 1. Checks that the data pointer is not NULL
 * 2. Checks that len is at least sizeof(audio_batch_header_t)
 * 3. Verifies that batch_count and channels are within reasonable bounds
 * 4. Unpacks all 32-bit values from network byte order to host byte order
 *
 * @param data Pointer to packet data (must not be NULL)
 * @param len Length of packet data in bytes
 * @param[out] info Pointer to audio_batch_info_t struct to fill with parsed data
 *
 * @return ASCIICHAT_OK if parsing succeeded
 * @return ERROR_INVALID_PARAM if data is NULL or info is NULL
 * @return ERROR_INVALID_PARAM if len is too small for the header
 *
 * Usage:
 * @code
 * audio_batch_info_t batch;
 * asciichat_error_t result = audio_parse_batch_header(packet_data, packet_len, &batch);
 * if (result != ASCIICHAT_OK) {
 *     log_error("Failed to parse audio batch: %d", result);
 *     return;
 * }
 *
 * // Now batch.batch_count, batch.sample_rate, etc. are ready to use
 * log_debug("Audio batch: %u frames at %u Hz, %u channels",
 *           batch.batch_count, batch.sample_rate, batch.channels);
 * @endcode
 */
asciichat_error_t audio_parse_batch_header(const void *data, size_t len, audio_batch_info_t *info);

/**
 * Validate audio batch parameters for sanity.
 * Checks that parsed batch parameters are within acceptable ranges.
 *
 * Performs these checks:
 * - batch_count > 0 and <= MAX_BATCH_SIZE
 * - sample_rate is a standard rate (8000, 16000, 24000, 48000, etc.)
 * - channels is 1 (mono) or 2 (stereo)
 * - total_samples is consistent with batch_count
 *
 * @param batch Pointer to audio_batch_info_t struct to validate
 *
 * @return ASCIICHAT_OK if batch parameters are valid
 * @return ERROR_INVALID_PARAM if any parameter is out of range
 *
 * Usage:
 * @code
 * audio_batch_info_t batch;
 * audio_parse_batch_header(data, len, &batch);
 *
 * if (audio_validate_batch_params(&batch) != ASCIICHAT_OK) {
 *     log_error("Audio batch parameters invalid");
 *     return;
 * }
 * @endcode
 */
asciichat_error_t audio_validate_batch_params(const audio_batch_info_t *batch);

/**
 * Check if a sample rate is a standard/supported rate.
 *
 * Supported rates: 8000, 16000, 24000, 32000, 44100, 48000, 96000, 192000
 *
 * @param sample_rate Sample rate in Hz
 * @return true if the sample rate is supported, false otherwise
 *
 * Usage:
 * @code
 * if (!audio_is_supported_sample_rate(batch.sample_rate)) {
 *     log_error("Unsupported sample rate: %u", batch.sample_rate);
 *     return;
 * }
 * @endcode
 */
bool audio_is_supported_sample_rate(uint32_t sample_rate);
