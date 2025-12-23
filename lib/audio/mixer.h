#pragma once

/**
 * @file mixer.h
 * @brief Multi-Source Audio Mixing and Processing System
 * @ingroup audio
 * @addtogroup audio
 * @{
 *
 * This header provides professional-quality audio mixing for ascii-chat's
 * multi-client audio chat functionality. The mixer combines audio from multiple
 * clients with advanced processing including compression, ducking, noise gating,
 * and high-pass filtering.
 *
 * CORE FEATURES:
 * ==============
 * - Multi-source audio mixing (up to 10 simultaneous sources)
 * - Active speaker detection and ducking
 * - Dynamic range compression
 * - Noise gate with hysteresis
 * - High-pass filtering for noise reduction
 * - Crowd scaling (automatic volume adjustment based on participant count)
 * - Optimized O(1) source exclusion using bitsets
 * - Reader-writer lock synchronization for concurrent access
 *
 * AUDIO PROCESSING PIPELINE:
 * =========================
 * The mixer processes audio through the following stages:
 * 1. Source Reading: Reads samples from client audio ring buffers
 * 2. Ducking: Identifies active speaker and attenuates background sources
 * 3. Mixing: Combines all active sources with crowd scaling
 * 4. Compression: Applies dynamic range compression to prevent clipping
 * 5. Noise Gate: Suppresses background noise below threshold
 * 6. High-Pass Filter: Removes low-frequency noise and rumble
 * 7. Soft Clipping: Prevents hard clipping artifacts
 *
 * DUCKING SYSTEM:
 * ===============
 * The ducking system automatically:
 * - Detects the loudest active speaker
 * - Identifies sources within a margin of the loudest (also considered active)
 * - Attenuates non-active sources to prevent echo and feedback
 * - Uses smooth attack/release curves for natural transitions
 *
 * COMPRESSION:
 * ============
 * The compressor provides:
 * - Threshold-based compression (acts above threshold)
 * - Configurable compression ratio (e.g., 4:1)
 * - Soft knee for smooth compression curves
 * - Independent attack and release times
 * - Automatic makeup gain compensation
 *
 * OPTIMIZATIONS:
 * ===============
 * - Bitset-based active source tracking (O(1) exclusion vs O(n))
 * - Hash table for client ID to mixer index mapping
 * - Reader-writer locks for concurrent source management
 * - Pre-allocated mix buffer to avoid allocations in hot path
 *
 * THREAD SAFETY:
 * ==============
 * - Source arrays protected by reader-writer locks
 * - Bitset operations are atomic
 * - Mix processing is thread-safe once sources are locked
 *
 * @note The mixer processes audio in fixed-size frames (256 samples) for
 *       consistent latency and processing behavior.
 * @note Ducking parameters should be tuned based on room acoustics and
 *       expected participant count for optimal performance.
 * @note Crowd scaling automatically reduces per-source volume as more
 *       participants join, preventing audio overload.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdbool.h>
#include <stdint.h>

// Include ringbuffer.h to get the audio_ring_buffer_t type
#include "ringbuffer.h"
#include "platform/rwlock.h"

/**
 * @name Audio Mixing Configuration
 * @{
 * @ingroup audio
 */

/**
 * @brief Maximum number of simultaneous audio sources
 *
 * Limits the maximum number of clients that can provide audio simultaneously.
 * Each client requires one source slot in the mixer.
 *
 * @ingroup audio
 */
#define MIXER_MAX_SOURCES 32 // Increased to match MAX_CLIENTS

/**
 * @brief Number of samples processed per audio frame
 *
 * Fixed frame size for consistent latency and processing behavior.
 * 256 samples at 48kHz = ~5.3ms per frame.
 *
 * @ingroup audio
 */
#define MIXER_FRAME_SIZE 256

/** @} */

/**
 * @brief Dynamic range compressor settings and state
 *
 * Implements professional-quality dynamic range compression to prevent
 * clipping and maintain consistent output levels. Uses sidechain input
 * for gain reduction calculation.
 *
 * COMPRESSION BEHAVIOR:
 * - Acts above threshold_dB (no compression below threshold)
 * - Applies compression ratio (e.g., 4:1 reduces 4dB above threshold to 1dB)
 * - Soft knee provides smooth compression curve transition
 * - Independent attack/release times for natural sound
 * - Makeup gain compensates for gain reduction
 *
 * @note All time-based parameters (attack_ms, release_ms) are converted
 *       to coefficients internally for sample-by-sample processing.
 *
 * @ingroup audio
 */
typedef struct {
  /** @brief Compression threshold in dB (e.g., -10.0) */
  float threshold_dB;
  /** @brief Knee width in dB for soft knee (e.g., 2.0) */
  float knee_dB;
  /** @brief Compression ratio (e.g., 4.0 for 4:1 compression) */
  float ratio;
  /** @brief Attack time in milliseconds (how fast compression kicks in) */
  float attack_ms;
  /** @brief Release time in milliseconds (how fast compression releases) */
  float release_ms;
  /** @brief Makeup gain in dB (compensates for gain reduction) */
  float makeup_dB;

  /** @brief Sample rate in Hz (set during initialization) */
  float sample_rate;
  /** @brief Current envelope follower state (linear, 0-1) */
  float envelope;
  /** @brief Current gain multiplier (linear, calculated from envelope) */
  float gain_lin;
  /** @brief Attack coefficient (converted from attack_ms) */
  float attack_coeff;
  /** @brief Release coefficient (converted from release_ms) */
  float release_coeff;
} compressor_t;

/**
 * @brief Noise gate settings and state
 *
 * Implements noise gate to suppress background noise below threshold.
 * Uses hysteresis to prevent gate chatter (rapid opening/closing).
 *
 * GATE BEHAVIOR:
 * - Closes (mutes) when input is below threshold
 * - Opens (allows through) when input is above threshold
 * - Hysteresis prevents rapid on/off cycling
 * - Smooth attack/release prevents clicks and pops
 *
 * @note Hysteresis means gate opens at threshold but closes at
 *       threshold * hysteresis (lower level), preventing chatter.
 *
 * @ingroup audio
 */
typedef struct {
  /** @brief Gate threshold in linear units (e.g., 0.01f for -40dB) */
  float threshold;
  /** @brief Attack time in milliseconds (how fast gate opens) */
  float attack_ms;
  /** @brief Release time in milliseconds (how fast gate closes) */
  float release_ms;
  /** @brief Hysteresis factor (0-1, prevents gate chatter) */
  float hysteresis;

  /** @brief Sample rate in Hz (set during initialization) */
  float sample_rate;
  /** @brief Current envelope follower state (linear, 0-1) */
  float envelope;
  /** @brief Attack coefficient (converted from attack_ms) */
  float attack_coeff;
  /** @brief Release coefficient (converted from release_ms) */
  float release_coeff;
  /** @brief True if gate is currently open (allowing audio through) */
  bool gate_open;
} noise_gate_t;

/**
 * @brief High-pass filter settings and state
 *
 * Implements first-order IIR high-pass filter to remove low-frequency
 * noise, rumble, and DC offset. Useful for cleaning up microphone input.
 *
 * FILTER BEHAVIOR:
 * - Attenuates frequencies below cutoff_hz
 * - Passes frequencies above cutoff_hz with minimal phase shift
 * - First-order design provides gentle rolloff (-6dB/octave)
 *
 * @note Filter state (prev_input, prev_output) must be preserved
 *       between samples for proper operation.
 *
 * @ingroup audio
 */
typedef struct {
  /** @brief Cutoff frequency in Hz (frequencies below this are attenuated) */
  float cutoff_hz;
  /** @brief Sample rate in Hz (set during initialization) */
  float sample_rate;

  /** @brief Filter coefficient alpha (calculated from cutoff_hz) */
  float alpha;
  /** @brief Previous input sample (filter state) */
  float prev_input;
  /** @brief Previous output sample (filter state) */
  float prev_output;
} highpass_filter_t;

/**
 * @brief Low-pass filter state
 *
 * First-order IIR low-pass filter for removing high-frequency noise
 * (hiss, electronic interference) while preserving voice clarity.
 * Combined with high-pass filter creates voice-focused band-pass effect.
 *
 * @ingroup audio
 */
typedef struct {
  /** @brief Cutoff frequency in Hz (frequencies above this are attenuated) */
  float cutoff_hz;
  /** @brief Sample rate in Hz (set during initialization) */
  float sample_rate;

  /** @brief Filter coefficient alpha (calculated from cutoff_hz) */
  float alpha;
  /** @brief Previous output sample (filter state) */
  float prev_output;
} lowpass_filter_t;

/**
 * @brief Ducking system settings and state
 *
 * Implements active speaker detection and automatic ducking (attenuation)
 * of background sources. Prevents echo and feedback in multi-client
 * audio scenarios.
 *
 * DUCKING BEHAVIOR:
 * - Detects loudest active speaker (above threshold_dB)
 * - Identifies sources within leader_margin_dB of loudest (also active)
 * - Attenuates non-active sources by atten_dB
 * - Uses smooth attack/release for natural transitions
 *
 * @note Ducking gain is applied per-source, allowing multiple active
 *       speakers to be heard while attenuating background noise.
 *
 * @ingroup audio
 */
typedef struct {
  /** @brief Speaking threshold in dB (sources below this are not "speaking") */
  float threshold_dB;
  /** @brief Leader margin in dB (sources within this of loudest are leaders) */
  float leader_margin_dB;
  /** @brief Attenuation in dB for non-leader sources */
  float atten_dB;
  /** @brief Ducking attack time in milliseconds */
  float attack_ms;
  /** @brief Ducking release time in milliseconds */
  float release_ms;

  /** @brief Attack coefficient (converted from attack_ms) */
  float attack_coeff;
  /** @brief Release coefficient (converted from release_ms) */
  float release_coeff;
  /** @brief Per-source envelope follower state (linear, allocated per source) */
  float *envelope;
  /** @brief Per-source ducking gain (linear, calculated from envelope) */
  float *gain;
} ducking_t;

/**
 * @brief Main mixer structure for multi-source audio processing
 *
 * Manages multiple audio sources (clients) and processes them through
 * a professional audio processing pipeline. Supports up to MIXER_MAX_SOURCES
 * simultaneous sources with automatic crowd scaling and active speaker detection.
 *
 * MIXER ARCHITECTURE:
 * - Source Management: Array of audio ring buffers, one per client
 * - Optimized Lookup: Bitset + hash table for O(1) source exclusion
 * - Thread Safety: Reader-writer locks for concurrent access
 * - Crowd Scaling: Automatic volume adjustment based on participant count
 * - Audio Processing: Ducking, compression, noise gate, high-pass filter
 *
 * PERFORMANCE OPTIMIZATIONS:
 * - Bitset-based active source tracking (O(1) exclusion)
 * - Hash table for client ID to mixer index mapping
 * - Pre-allocated mix buffer (no allocations in hot path)
 * - Reader-writer locks for concurrent reads
 *
 * @note Each source is indexed by position in arrays (0 to max_sources-1).
 *       Client IDs are mapped to indices via source_id_to_index hash table.
 *
 * @note Active sources are tracked in two ways:
 *       - source_active[] array for per-source status
 *       - active_sources_mask bitset for O(1) iteration exclusion
 *
 * @warning Source arrays must be locked (read or write) during iteration
 *          to prevent concurrent modifications.
 *
 * @ingroup audio
 */
typedef struct {
  /** @brief Current number of active audio sources */
  int num_sources;
  /** @brief Maximum number of sources (allocated array sizes) */
  int max_sources;
  /** @brief Sample rate in Hz (e.g., 44100) */
  int sample_rate;

  /** @brief Array of pointers to client audio ring buffers */
  audio_ring_buffer_t **source_buffers;
  /** @brief Array of client IDs (one per source slot) */
  uint32_t *source_ids;
  /** @brief Array of active flags (true if source is active) */
  bool *source_active;

  /** @brief Bitset of active sources (bit i = source i is active, O(1) iteration) */
  uint64_t active_sources_mask;
  /** @brief Hash table mapping client_id → mixer source index (uses hash function for 32-bit IDs) */
  uint8_t source_id_to_index[256];
  /** @brief Client IDs stored at each hash index for collision detection */
  uint32_t source_id_at_hash[256];

  /** @brief Reader-writer lock protecting source arrays and bitset */
  rwlock_t source_lock;

  /** @brief Crowd scaling exponent (typically 0.5 for sqrt scaling) */
  float crowd_alpha;
  /** @brief Base gain before crowd scaling is applied */
  float base_gain;

  /** @brief Ducking system (active speaker detection and attenuation) */
  ducking_t ducking;
  /** @brief Compressor (dynamic range compression) */
  compressor_t compressor;

  /** @brief Temporary buffer for mixing operations (pre-allocated) */
  float *mix_buffer;
} mixer_t;

/**
 * @name Mixer Lifecycle Functions
 * @{
 * @ingroup audio
 */

/**
 * @brief Create a new audio mixer
 * @param max_sources Maximum number of audio sources to support
 * @param sample_rate Sample rate in Hz (e.g., 44100)
 * @return Pointer to new mixer, or NULL on failure
 *
 * Creates a new mixer with pre-allocated source arrays and processing
 * buffers. All audio processing components are initialized.
 *
 * @note max_sources should not exceed MIXER_MAX_SOURCES.
 *
 * @ingroup audio
 */
mixer_t *mixer_create(int max_sources, int sample_rate);

/**
 * @brief Destroy a mixer and free all resources
 * @param mixer Mixer to destroy (can be NULL)
 *
 * Frees all allocated memory including source arrays, processing buffers,
 * and ducking system allocations.
 *
 * @warning Mixer must not be in use by any threads when destroyed.
 *
 * @ingroup audio
 */
void mixer_destroy(mixer_t *mixer);

/** @} */

/**
 * @name Source Management Functions
 * @{
 * @ingroup audio
 */

/**
 * @brief Add an audio source to the mixer
 * @param mixer Audio mixer
 * @param client_id Client ID for this source
 * @param buffer Pointer to client's audio ring buffer
 * @return Mixer source index on success, -1 on failure (max sources reached)
 *
 * Adds a new audio source to the mixer. Client ID is mapped to a mixer
 * source index for efficient lookup during processing.
 *
 * @note Thread-safe (acquires write lock internally).
 *
 * @note If max_sources is reached, addition fails and -1 is returned.
 *
 * @ingroup audio
 */
int mixer_add_source(mixer_t *mixer, uint32_t client_id, audio_ring_buffer_t *buffer);

/**
 * @brief Remove an audio source from the mixer
 * @param mixer Audio mixer
 * @param client_id Client ID to remove
 *
 * Removes the audio source associated with the given client ID. Source
 * slot is freed for reuse by another client.
 *
 * @note Thread-safe (acquires write lock internally).
 *
 * @note If client_id is not found, operation has no effect.
 *
 * @ingroup audio
 */
void mixer_remove_source(mixer_t *mixer, uint32_t client_id);

/**
 * @brief Set whether a source is active (receiving audio)
 * @param mixer Audio mixer
 * @param client_id Client ID
 * @param active true to mark source as active, false to mark inactive
 *
 * Updates the active status of a source. Inactive sources are excluded
 * from mixing operations for efficiency.
 *
 * @note Thread-safe (acquires write lock internally).
 *
 * @note Source must exist (be added) before calling this function.
 *
 * @ingroup audio
 */
void mixer_set_source_active(mixer_t *mixer, uint32_t client_id, bool active);

/** @} */

/**
 * @name Audio Processing Functions
 * @{
 * @ingroup audio
 */

/**
 * @brief Process audio from all active sources
 * @param mixer Audio mixer
 * @param output Output buffer for mixed audio (must have num_samples elements)
 * @param num_samples Number of samples to process
 * @return Number of samples processed on success, negative on error
 *
 * Reads audio from all active sources, applies processing pipeline
 * (ducking, mixing, compression, noise gate, high-pass filter), and
 * writes mixed output to output buffer.
 *
 * @note Processing includes all stages: ducking, mixing, compression,
 *       noise gating, high-pass filtering, and soft clipping.
 *
 * @note Thread-safe (acquires read lock for source access).
 *
 * @warning Output buffer must have at least num_samples float elements.
 *
 * @ingroup audio
 */
int mixer_process(mixer_t *mixer, float *output, int num_samples);

/**
 * @brief Process audio from all sources except one (for per-client output)
 * @param mixer Audio mixer
 * @param output Output buffer for mixed audio
 * @param num_samples Number of samples to process
 * @param exclude_client_id Client ID to exclude from mixing
 * @return Number of samples processed on success, negative on error
 *
 * Same as mixer_process() but excludes one client's audio from the mix.
 * Used to generate per-client output that doesn't include their own audio
 * (prevents echo and feedback).
 *
 * @note Processing pipeline is identical to mixer_process().
 *
 * @note Thread-safe (acquires read lock for source access).
 *
 * @ingroup audio
 */
int mixer_process_excluding_source(mixer_t *mixer, float *output, int num_samples, uint32_t exclude_client_id);

/** @} */

/**
 * @name Utility Functions
 * @{
 * @ingroup audio
 */

/**
 * @brief Convert decibels to linear gain
 * @param db Decibel value
 * @return Linear gain multiplier (10^(db/20))
 *
 * Converts decibel value to linear gain multiplier for audio processing.
 *
 * @ingroup audio
 */
float db_to_linear(float db);

/**
 * @brief Convert linear gain to decibels
 * @param linear Linear gain multiplier
 * @return Decibel value (20*log10(linear))
 *
 * Converts linear gain multiplier to decibel value for display/logging.
 *
 * @ingroup audio
 */
float linear_to_db(float linear);

/**
 * @brief Clamp a float value to a range
 * @param value Value to clamp
 * @param min Minimum value
 * @param max Maximum value
 * @return Clamped value (min <= result <= max)
 *
 * Clamps a float value to the specified range. Useful for preventing
 * audio overflow and ensuring parameters stay within valid ranges.
 *
 * @ingroup audio
 */
float clamp_float(float value, float min, float max);

/** @} */

/**
 * @name Compressor Functions
 * @{
 * @ingroup audio
 */

/**
 * @brief Initialize a compressor
 * @param comp Compressor structure
 * @param sample_rate Sample rate in Hz
 *
 * Initializes compressor state and converts time-based parameters
 * (attack_ms, release_ms) to coefficients for sample-by-sample processing.
 *
 * @ingroup audio
 */
void compressor_init(compressor_t *comp, float sample_rate);

/**
 * @brief Set compressor parameters
 * @param comp Compressor structure
 * @param threshold_dB Compression threshold in dB
 * @param ratio Compression ratio (e.g., 4.0 for 4:1)
 * @param attack_ms Attack time in milliseconds
 * @param release_ms Release time in milliseconds
 * @param makeup_dB Makeup gain in dB
 *
 * Updates compressor parameters. Time-based parameters are converted
 * to coefficients internally.
 *
 * @ingroup audio
 */
void compressor_set_params(compressor_t *comp, float threshold_dB, float ratio, float attack_ms, float release_ms,
                           float makeup_dB);

/**
 * @brief Process a single sample through compressor
 * @param comp Compressor structure
 * @param sidechain Input level for gain reduction calculation
 * @return Compressed output level
 *
 * Processes sidechain input through compressor to calculate gain reduction.
 * Returns compressed output level (not actual audio sample - use for sidechain).
 *
 * @note This calculates gain reduction, not actual audio compression.
 *       Apply the gain to audio samples separately.
 *
 * @ingroup audio
 */
float compressor_process_sample(compressor_t *comp, float sidechain);

/** @} */

/**
 * @name Ducking Functions
 * @{
 * @ingroup audio
 */

/**
 * @brief Initialize ducking system
 * @param duck Ducking structure
 * @param num_sources Number of sources to support
 * @param sample_rate Sample rate in Hz
 *
 * Allocates per-source arrays (envelope, gain) for ducking system.
 * Time-based parameters are converted to coefficients.
 *
 * @warning Must be paired with ducking_free() to prevent memory leaks.
 *
 * @return ASCIICHAT_OK on success, error code on failure
 * @ingroup audio
 */
asciichat_error_t ducking_init(ducking_t *duck, int num_sources, float sample_rate);

/**
 * @brief Free ducking system resources
 * @param duck Ducking structure
 *
 * Frees per-source arrays allocated by ducking_init().
 *
 * @ingroup audio
 */
void ducking_free(ducking_t *duck);

/**
 * @brief Set ducking parameters
 * @param duck Ducking structure
 * @param threshold_dB Speaking threshold in dB
 * @param leader_margin_dB Leader margin in dB
 * @param atten_dB Attenuation in dB for non-leaders
 * @param attack_ms Attack time in milliseconds
 * @param release_ms Release time in milliseconds
 *
 * Updates ducking parameters. Time-based parameters are converted
 * to coefficients internally.
 *
 * @ingroup audio
 */
void ducking_set_params(ducking_t *duck, float threshold_dB, float leader_margin_dB, float atten_dB, float attack_ms,
                        float release_ms);

/**
 * @brief Process a frame of audio through ducking system
 * @param duck Ducking structure
 * @param envelopes Array of per-source envelope levels (input)
 * @param gains Array of per-source ducking gains (output)
 * @param num_sources Number of sources to process
 *
 * Calculates ducking gains for each source based on envelope levels.
 * Outputs per-source gain multipliers to apply to audio samples.
 *
 * @note Gains are calculated from envelope levels, identifying leaders
 *       and applying attenuation to non-leaders.
 *
 * @ingroup audio
 */
void ducking_process_frame(ducking_t *duck, float *envelopes, float *gains, int num_sources);

/** @} */

/**
 * @name Noise Gate Functions
 * @{
 * @ingroup audio
 */

/**
 * @brief Initialize a noise gate
 * @param gate Noise gate structure
 * @param sample_rate Sample rate in Hz
 *
 * Initializes noise gate state and converts time-based parameters
 * to coefficients.
 *
 * @ingroup audio
 */
void noise_gate_init(noise_gate_t *gate, float sample_rate);

/**
 * @brief Set noise gate parameters
 * @param gate Noise gate structure
 * @param threshold Gate threshold in linear units
 * @param attack_ms Attack time in milliseconds
 * @param release_ms Release time in milliseconds
 * @param hysteresis Hysteresis factor (0-1) to prevent chatter
 *
 * Updates noise gate parameters. Time-based parameters are converted
 * to coefficients internally.
 *
 * @ingroup audio
 */
void noise_gate_set_params(noise_gate_t *gate, float threshold, float attack_ms, float release_ms, float hysteresis);

/**
 * @brief Process a single sample through noise gate
 * @param gate Noise gate structure
 * @param input Input sample value
 * @param peak_amplitude Peak amplitude for gate decision
 * @return Gated output sample (input if gate open, 0 if closed)
 *
 * Processes input sample through noise gate. Gate opens/closes based
 * on peak_amplitude compared to threshold (with hysteresis).
 *
 * @note peak_amplitude should be envelope follower output, not raw sample.
 *
 * @ingroup audio
 */
float noise_gate_process_sample(noise_gate_t *gate, float input, float peak_amplitude);

/**
 * @brief Process a buffer of samples through noise gate
 * @param gate Noise gate structure
 * @param buffer Audio buffer (modified in-place)
 * @param num_samples Number of samples to process
 *
 * Processes entire buffer through noise gate. Buffer is modified in-place.
 *
 * @note Peak amplitude is calculated per sample for gate decision.
 *
 * @ingroup audio
 */
void noise_gate_process_buffer(noise_gate_t *gate, float *buffer, int num_samples);

/**
 * @brief Check if noise gate is currently open
 * @param gate Noise gate structure
 * @return true if gate is open, false if closed
 *
 * Returns current gate state. Useful for debugging and monitoring.
 *
 * @ingroup audio
 */
bool noise_gate_is_open(const noise_gate_t *gate);

/** @} */

/**
 * @name High-Pass Filter Functions
 * @{
 * @ingroup audio
 */

/**
 * @brief Initialize a high-pass filter
 * @param filter High-pass filter structure
 * @param cutoff_hz Cutoff frequency in Hz
 * @param sample_rate Sample rate in Hz
 *
 * Initializes filter state and calculates filter coefficient alpha
 * from cutoff frequency.
 *
 * @ingroup audio
 */
void highpass_filter_init(highpass_filter_t *filter, float cutoff_hz, float sample_rate);

/**
 * @brief Reset high-pass filter state
 * @param filter High-pass filter structure
 *
 * Resets filter state (prev_input, prev_output) to zero. Useful
 * for starting fresh processing or removing DC offset.
 *
 * @ingroup audio
 */
void highpass_filter_reset(highpass_filter_t *filter);

/**
 * @brief Process a single sample through high-pass filter
 * @param filter High-pass filter structure
 * @param input Input sample value
 * @return Filtered output sample
 *
 * Processes input sample through first-order IIR high-pass filter.
 * Filter state is updated for next sample.
 *
 * @ingroup audio
 */
float highpass_filter_process_sample(highpass_filter_t *filter, float input);

/**
 * @brief Process a buffer of samples through high-pass filter
 * @param filter High-pass filter structure
 * @param buffer Audio buffer (modified in-place)
 * @param num_samples Number of samples to process
 *
 * Processes entire buffer through high-pass filter. Buffer is modified in-place.
 *
 * @ingroup audio
 */
void highpass_filter_process_buffer(highpass_filter_t *filter, float *buffer, int num_samples);

/** @} */

/**
 * @name Low-Pass Filter Functions
 * @{
 * @ingroup audio
 */

/**
 * @brief Initialize a low-pass filter
 * @param filter Low-pass filter structure
 * @param cutoff_hz Cutoff frequency in Hz
 * @param sample_rate Sample rate in Hz
 *
 * Initializes filter state and calculates filter coefficient alpha
 * from cutoff frequency. Frequencies above cutoff are attenuated.
 *
 * @ingroup audio
 */
void lowpass_filter_init(lowpass_filter_t *filter, float cutoff_hz, float sample_rate);

/**
 * @brief Reset low-pass filter state
 * @param filter Low-pass filter structure
 *
 * Resets filter state (prev_output) to zero.
 *
 * @ingroup audio
 */
void lowpass_filter_reset(lowpass_filter_t *filter);

/**
 * @brief Process a single sample through low-pass filter
 * @param filter Low-pass filter structure
 * @param input Input sample value
 * @return Filtered output sample
 *
 * Processes input sample through first-order IIR low-pass filter.
 * Filter state is updated for next sample.
 *
 * @ingroup audio
 */
float lowpass_filter_process_sample(lowpass_filter_t *filter, float input);

/**
 * @brief Process a buffer of samples through low-pass filter
 * @param filter Low-pass filter structure
 * @param buffer Audio buffer (modified in-place)
 * @param num_samples Number of samples to process
 *
 * Processes entire buffer through low-pass filter. Buffer is modified in-place.
 *
 * @ingroup audio
 */
void lowpass_filter_process_buffer(lowpass_filter_t *filter, float *buffer, int num_samples);

/** @} */

/**
 * @name Soft Clipping Functions
 * @{
 * @ingroup audio
 */

/**
 * @brief Apply soft clipping to a sample
 * @param sample Input sample value
 * @param threshold Clipping threshold (typically 1.0)
 * @return Soft-clipped output sample
 *
 * Applies soft clipping using tanh-like curve to prevent hard clipping
 * artifacts while maintaining loud output.
 *
 * @note Soft clipping is gentler than hard clipping, reducing distortion.
 *
 * @ingroup audio
 */
float soft_clip(float sample, float threshold);

/**
 * @brief Apply soft clipping to a buffer of samples
 * @param buffer Audio buffer (modified in-place)
 * @param num_samples Number of samples to process
 * @param threshold Clipping threshold (typically 1.0)
 *
 * Processes entire buffer through soft clipping. Buffer is modified in-place.
 *
 * @ingroup audio
 */
void soft_clip_buffer(float *buffer, int num_samples, float threshold);

/** @} */
