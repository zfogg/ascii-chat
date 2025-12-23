/**
 * @file client/audio.h
 * @ingroup client_audio
 * @brief ascii-chat Client Audio Processing Management Interface
 *
 * Defines the interface for audio system initialization, capture thread
 * management, and audio sample processing in the ascii-chat client.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "audio/client_audio_pipeline.h"

/**
 * @brief Process received audio samples from server
 * @param samples Audio sample data from server
 * @param num_samples Number of samples in buffer
 *
 * @ingroup client_audio
 */
void audio_process_received_samples(const float *samples, int num_samples);

/**
 * @brief Initialize audio subsystem
 * @return 0 on success, negative on error
 *
 * @ingroup client_audio
 */
int audio_client_init(void);

/**
 * @brief Start audio capture thread
 * @return 0 on success, negative on error
 *
 * @ingroup client_audio
 */
int audio_start_thread(void);

/**
 * @brief Stop audio capture thread
 *
 * @ingroup client_audio
 */
void audio_stop_thread(void);

/**
 * @brief Check if audio capture thread has exited
 * @return true if thread exited, false otherwise
 *
 * @ingroup client_audio
 */
bool audio_thread_exited(void);

/**
 * @brief Cleanup audio subsystem
 *
 * @ingroup client_audio
 */
void audio_cleanup(void);

/**
 * @brief Get the audio pipeline (for advanced usage)
 * @return Pointer to the audio pipeline, or NULL if not initialized
 *
 * @ingroup client_audio
 */
client_audio_pipeline_t *audio_get_pipeline(void);

/**
 * @brief Decode Opus packet using the audio pipeline
 * @param opus_data Opus packet data
 * @param opus_len Opus packet length
 * @param output Output buffer for decoded samples
 * @param max_samples Maximum samples output buffer can hold
 * @return Number of decoded samples, or negative on error
 *
 * @ingroup client_audio
 */
int audio_decode_opus(const uint8_t *opus_data, size_t opus_len, float *output, int max_samples);
