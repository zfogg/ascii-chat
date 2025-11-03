/**
 * @file client/audio.h
 * @ingroup client_audio
 * @brief ASCII-Chat Client Audio Processing Management Interface
 *
 * Defines the interface for audio system initialization, capture thread
 * management, and audio sample processing in the ASCII-Chat client.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <stdbool.h>

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
int audio_client_init();

/**
 * @brief Start audio capture thread
 * @return 0 on success, negative on error
 *
 * @ingroup client_audio
 */
int audio_start_thread();

/**
 * @brief Stop audio capture thread
 *
 * @ingroup client_audio
 */
void audio_stop_thread();

/**
 * @brief Check if audio capture thread has exited
 * @return true if thread exited, false otherwise
 *
 * @ingroup client_audio
 */
bool audio_thread_exited();

/**
 * @brief Cleanup audio subsystem
 *
 * @ingroup client_audio
 */
void audio_cleanup();
