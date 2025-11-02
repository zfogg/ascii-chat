#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "platform/abstraction.h"

// Include ringbuffer.h to get the audio_ring_buffer_t type
#include "ringbuffer.h"

// Audio mixing parameters
#define MIXER_MAX_SOURCES 10
#define MIXER_FRAME_SIZE 256 // Samples per frame to process

// Compressor settings
typedef struct {
  float threshold_dB; // Threshold in dB (e.g., -10.0)
  float knee_dB;      // Knee width in dB (e.g., 2.0 for soft knee)
  float ratio;        // Compression ratio (e.g., 4.0 for 4:1)
  float attack_ms;    // Attack time in milliseconds
  float release_ms;   // Release time in milliseconds
  float makeup_dB;    // Makeup gain in dB

  // Internal state
  float sample_rate;
  float envelope;
  float gain_lin;
  float attack_coeff;
  float release_coeff;
} compressor_t;

// Noise gate settings
typedef struct {
  float threshold;  // Linear threshold (e.g., 0.01f)
  float attack_ms;  // Attack time in milliseconds
  float release_ms; // Release time in milliseconds
  float hysteresis; // Hysteresis factor to prevent chatter (e.g., 0.9)

  // Internal state
  float sample_rate;
  float envelope;
  float attack_coeff;
  float release_coeff;
  bool gate_open;
} noise_gate_t;

// High-pass filter settings
typedef struct {
  float cutoff_hz; // Cutoff frequency in Hz
  float sample_rate;

  // Internal state
  float alpha;
  float prev_input;
  float prev_output;
} highpass_filter_t;

// Ducking settings (for active speaker detection)
typedef struct {
  float threshold_dB;     // Below this, source isn't "speaking"
  float leader_margin_dB; // Within this of loudest = leader (not ducked)
  float atten_dB;         // Attenuation for non-leaders
  float attack_ms;        // Ducking attack time
  float release_ms;       // Ducking release time

  // Internal state
  float attack_coeff;
  float release_coeff;
  float *envelope; // Per-source envelope (linear)
  float *gain;     // Per-source ducking gain (linear)
} ducking_t;

// Main mixer structure
typedef struct {
  int num_sources; // Number of active audio sources
  int max_sources; // Maximum number of sources
  int sample_rate;

  // Source management
  audio_ring_buffer_t **source_buffers; // Array of pointers to client audio buffers
  uint32_t *source_ids;                 // Client IDs for each source
  bool *source_active;                  // Whether each source is active

  // OPTIMIZATION 1: Bitset-based source exclusion (O(1) vs O(n))
  uint64_t active_sources_mask;    // Bitset of active sources (bit i = source i active)
  uint8_t source_id_to_index[256]; // Hash table: client_id → mixer source index

  // OPTIMIZATION 2: Reader-writer synchronization
  rwlock_t source_lock; // Protects source arrays and bitset

  // Crowd scaling (loud with few, quiet with many)
  float crowd_alpha; // Scale ~ 1 / active^alpha (typically 0.5)
  float base_gain;   // Base gain before crowd scaling

  // Processing
  ducking_t ducking;
  compressor_t compressor;

  // Mix buffer
  float *mix_buffer; // Temporary buffer for mixing
} mixer_t;

// Mixer functions
mixer_t *mixer_create(int max_sources, int sample_rate);
void mixer_destroy(mixer_t *mixer);

// Source management
int mixer_add_source(mixer_t *mixer, uint32_t client_id, audio_ring_buffer_t *buffer);
void mixer_remove_source(mixer_t *mixer, uint32_t client_id);
void mixer_set_source_active(mixer_t *mixer, uint32_t client_id, bool active);

// Processing
int mixer_process(mixer_t *mixer, float *output, int num_samples);
int mixer_process_excluding_source(mixer_t *mixer, float *output, int num_samples, uint32_t exclude_client_id);

// Utility functions
float db_to_linear(float db);
float linear_to_db(float linear);
float clamp_float(float value, float min, float max);

// Compressor functions
void compressor_init(compressor_t *comp, float sample_rate);
void compressor_set_params(compressor_t *comp, float threshold_dB, float ratio, float attack_ms, float release_ms,
                           float makeup_dB);
float compressor_process_sample(compressor_t *comp, float sidechain);

// Ducking functions
void ducking_init(ducking_t *duck, int num_sources, float sample_rate);
void ducking_free(ducking_t *duck);
void ducking_set_params(ducking_t *duck, float threshold_dB, float leader_margin_dB, float atten_dB, float attack_ms,
                        float release_ms);
void ducking_process_frame(ducking_t *duck, float *envelopes, float *gains, int num_sources);

// Noise gate functions
void noise_gate_init(noise_gate_t *gate, float sample_rate);
void noise_gate_set_params(noise_gate_t *gate, float threshold, float attack_ms, float release_ms, float hysteresis);
float noise_gate_process_sample(noise_gate_t *gate, float input, float peak_amplitude);
void noise_gate_process_buffer(noise_gate_t *gate, float *buffer, int num_samples);
bool noise_gate_is_open(const noise_gate_t *gate);

// High-pass filter functions
void highpass_filter_init(highpass_filter_t *filter, float cutoff_hz, float sample_rate);
void highpass_filter_reset(highpass_filter_t *filter);
float highpass_filter_process_sample(highpass_filter_t *filter, float input);
void highpass_filter_process_buffer(highpass_filter_t *filter, float *buffer, int num_samples);

// Soft clipping function
float soft_clip(float sample, float threshold);
void soft_clip_buffer(float *buffer, int num_samples, float threshold);
