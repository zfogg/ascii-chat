/**
 * @file echo_cancel.h
 * @brief Acoustic Echo Cancellation (AEC) using Speex DSP
 *
 * Prevents feedback loops when speakers and microphone are in the same room.
 * The AEC learns the acoustic path from speaker to microphone and subtracts
 * the echo from captured audio.
 *
 * Usage:
 * 1. Call echo_cancel_init() at startup
 * 2. Call echo_cancel_playback() for every buffer sent to speakers
 * 3. Call echo_cancel_capture() for every buffer from microphone
 * 4. Call echo_cancel_destroy() at shutdown
 *
 * The AEC needs synchronized playback and capture - it correlates the
 * speaker output with mic input to identify and remove echoes.
 */

#ifndef ECHO_CANCEL_H
#define ECHO_CANCEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the acoustic echo canceller
 * @param sample_rate Audio sample rate in Hz (e.g., 48000)
 * @param frame_size Number of samples per frame (e.g., 480 for 10ms at 48kHz)
 * @param filter_length_ms Echo tail length in milliseconds (100-500ms typical)
 * @return true on success, false on failure
 *
 * The filter_length should cover the longest expected echo path.
 * Larger values handle more reverberant rooms but use more CPU.
 * 200-300ms is a good default for most rooms.
 */
bool echo_cancel_init(int sample_rate, int frame_size, int filter_length_ms);

/**
 * @brief Feed playback samples to the AEC (call from playback callback)
 * @param samples Audio samples being played through speakers
 * @param num_samples Number of samples
 *
 * This provides the "reference" signal - what the speakers are outputting.
 * Must be called for every buffer before it goes to the speakers.
 */
void echo_cancel_playback(const float *samples, int num_samples);

/**
 * @brief Process captured audio to remove echo (call from capture callback)
 * @param input Microphone input samples (may contain echo)
 * @param output Buffer for echo-cancelled output (same size as input)
 * @param num_samples Number of samples
 *
 * Removes speaker echo from microphone input using the reference signal
 * provided by echo_cancel_playback(). Output contains clean audio.
 */
void echo_cancel_capture(const float *input, float *output, int num_samples);

/**
 * @brief Check if echo cancellation is available and initialized
 * @return true if AEC is active, false otherwise
 */
bool echo_cancel_is_active(void);

/**
 * @brief Reset the AEC state (use after audio glitches or long pauses)
 *
 * Clears the learned echo path. The AEC will need to re-learn
 * the acoustic characteristics of the room.
 */
void echo_cancel_reset(void);

/**
 * @brief Destroy the echo canceller and free resources
 */
void echo_cancel_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* ECHO_CANCEL_H */
