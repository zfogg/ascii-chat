/**
 * @file mixer.c
 * @ingroup audio
 * @brief 🎚️ Real-time audio mixer with ducking, gain control, and multi-stream blending
 */

#include "audio/audio.h"
#include "audio/mixer.h"
#include "common.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include "util/time.h"       // For timing instrumentation
#include <math.h>
#include <string.h>
#include <stdint.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

// Portable bit manipulation: Count trailing zeros (find first set bit)
static inline int find_first_set_bit(uint64_t mask) {
#if defined(__GNUC__) || defined(__clang__)
  // GCC/Clang builtin (fast hardware instruction)
  return __builtin_ctzll(mask);
#elif defined(_MSC_VER) && defined(_M_X64)
  // Microsoft Visual C++ x64 intrinsic
  unsigned long index;
  _BitScanForward64(&index, mask);
  return (int)index;
#else
  // Portable fallback using De Bruijn multiplication (production-tested)
  // This is O(1) with ~4-5 CPU cycles - very fast and reliable
  static const int debruijn_table[64] = {0,  1,  2,  53, 3,  7,  54, 27, 4,  38, 41, 8,  34, 55, 48, 28,
                                         62, 5,  39, 46, 44, 42, 22, 9,  24, 35, 59, 56, 49, 18, 29, 11,
                                         63, 52, 6,  26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
                                         51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12};

  if (mask == 0)
    return 64; // No bits set

  // De Bruijn sequence method: isolate rightmost bit and hash
  return debruijn_table[((mask & -mask) * 0x022fdd63cc95386dULL) >> 58];
#endif
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Utility functions
float db_to_linear(float db) {
  return powf(10.0f, db / 20.0f);
}

float linear_to_db(float linear) {
  return 20.0f * log10f(fmaxf(linear, 1e-12f));
}

float clamp_float(float value, float min, float max) {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

// Compressor implementation
void compressor_init(compressor_t *comp, float sample_rate) {
  comp->sample_rate = sample_rate;
  comp->envelope = 0.0f;
  comp->gain_lin = 1.0f;

  // Set default parameters
  compressor_set_params(comp, -10.0f, 4.0f, 10.0f, 100.0f, 3.0f);
}

void compressor_set_params(compressor_t *comp, float threshold_dB, float ratio, float attack_ms, float release_ms,
                           float makeup_dB) {
  comp->threshold_dB = threshold_dB;
  comp->ratio = ratio;
  comp->attack_ms = attack_ms;
  comp->release_ms = release_ms;
  comp->makeup_dB = makeup_dB;
  comp->knee_dB = 2.0f; // Fixed soft knee

  // Calculate time constants
  float attack_tau = attack_ms / 1000.0f;
  float release_tau = release_ms / 1000.0f;
  comp->attack_coeff = expf(-1.0f / (attack_tau * comp->sample_rate + 1e-12f));
  comp->release_coeff = expf(-1.0f / (release_tau * comp->sample_rate + 1e-12f));
}

static float compressor_gain_reduction_db(const compressor_t *comp, float level_dB) {
  float over = level_dB - comp->threshold_dB;
  float knee = comp->knee_dB;

  if (knee > 0.0f) {
    if (over <= -knee * 0.5f)
      return 0.0f;
    if (over >= knee * 0.5f)
      return (1.0f / comp->ratio - 1.0f) * over;
    float x = over + knee * 0.5f;
    return (1.0f / comp->ratio - 1.0f) * (x * x) / (2.0f * knee);
  }
  if (over <= 0.0f) {
    return 0.0f;
  }
  return (1.0f / comp->ratio - 1.0f) * over;
}

float compressor_process_sample(compressor_t *comp, float sidechain) {
  float x = fabsf(sidechain);

  // Update envelope with attack/release
  if (x > comp->envelope)
    comp->envelope = comp->attack_coeff * comp->envelope + (1.0f - comp->attack_coeff) * x;
  else
    comp->envelope = comp->release_coeff * comp->envelope + (1.0f - comp->release_coeff) * x;

  // Calculate gain reduction
  float level_dB = linear_to_db(comp->envelope);
  float gr_dB = compressor_gain_reduction_db(comp, level_dB);
  float target_lin = db_to_linear(gr_dB + comp->makeup_dB);

  // Smooth gain changes
  if (target_lin < comp->gain_lin)
    comp->gain_lin = comp->attack_coeff * comp->gain_lin + (1.0f - comp->attack_coeff) * target_lin;
  else
    comp->gain_lin = comp->release_coeff * comp->gain_lin + (1.0f - comp->release_coeff) * target_lin;

  return comp->gain_lin;
}

// Ducking implementation
asciichat_error_t ducking_init(ducking_t *duck, int num_sources, float sample_rate) {
  if (!duck) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ducking_init: duck is NULL");
  }

  // Set default parameters
  duck->threshold_dB = -40.0f;
  duck->leader_margin_dB = 3.0f;
  duck->atten_dB = -12.0f;
  duck->attack_ms = 5.0f;
  duck->release_ms = 100.0f;
  duck->envelope = NULL;
  duck->gain = NULL;

  // Calculate time constants
  float attack_tau = duck->attack_ms / 1000.0f;
  float release_tau = duck->release_ms / 1000.0f;
  duck->attack_coeff = expf(-1.0f / (attack_tau * sample_rate + 1e-12f));
  duck->release_coeff = expf(-1.0f / (release_tau * sample_rate + 1e-12f));

  // Allocate arrays
  duck->envelope = SAFE_MALLOC((size_t)num_sources * sizeof(float), float *);
  if (!duck->envelope) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate ducking envelope array");
  }

  duck->gain = SAFE_MALLOC((size_t)num_sources * sizeof(float), float *);
  if (!duck->gain) {
    SAFE_FREE(duck->envelope);
    duck->envelope = NULL;
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate ducking gain array");
  }

  // Initialize
  SAFE_MEMSET(duck->envelope, (size_t)num_sources * sizeof(float), 0, (size_t)num_sources * sizeof(float));
  for (int i = 0; i < num_sources; i++) {
    duck->gain[i] = 1.0f;
  }

  return ASCIICHAT_OK;
}

void ducking_free(ducking_t *duck) {
  if (duck->envelope) {
    SAFE_FREE(duck->envelope);
  }
  if (duck->gain) {
    SAFE_FREE(duck->gain);
  }
}

void ducking_set_params(ducking_t *duck, float threshold_dB, float leader_margin_dB, float atten_dB, float attack_ms,
                        float release_ms) {
  duck->threshold_dB = threshold_dB;
  duck->leader_margin_dB = leader_margin_dB;
  duck->atten_dB = atten_dB;
  duck->attack_ms = attack_ms;
  duck->release_ms = release_ms;
}

void ducking_process_frame(ducking_t *duck, float *envelopes, float *gains, int num_sources) {
  // Find the loudest source
  float max_dB = -120.0f;
  float env_dB[MIXER_MAX_SOURCES];

  for (int i = 0; i < num_sources; i++) {
    env_dB[i] = linear_to_db(envelopes[i]);
    if (env_dB[i] > max_dB)
      max_dB = env_dB[i];
  }

  // Calculate ducking gain for each source
  float leader_cut = db_to_linear(duck->atten_dB);

  for (int i = 0; i < num_sources; i++) {
    bool is_speaking = env_dB[i] > duck->threshold_dB;
    bool is_leader = is_speaking && (env_dB[i] >= max_dB - duck->leader_margin_dB);

    float target;
    if (is_speaking && !is_leader) {
      target = leader_cut;
    } else {
      target = 1.0f;
    }

    // Smooth gain transitions
    if (target < gains[i])
      gains[i] = duck->attack_coeff * gains[i] + (1.0f - duck->attack_coeff) * target;
    else
      gains[i] = duck->release_coeff * gains[i] + (1.0f - duck->release_coeff) * target;
  }
}

// Mixer implementation
mixer_t *mixer_create(int max_sources, int sample_rate) {
  // Validate parameters
  if (max_sources <= 0 || max_sources > MIXER_MAX_SOURCES) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid max_sources: %d (must be 1-%d)", max_sources, MIXER_MAX_SOURCES);
    return NULL;
  }

  if (sample_rate <= 0 || sample_rate > 192000) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid sample_rate: %d (must be 1-192000)", sample_rate);
    return NULL;
  }

  mixer_t *mixer;
  mixer = SAFE_MALLOC(sizeof(mixer_t), mixer_t *);
  if (!mixer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate mixer structure");
    return NULL;
  }

  mixer->num_sources = 0;
  mixer->max_sources = max_sources;
  mixer->sample_rate = sample_rate;

  // Allocate source management arrays
  mixer->source_buffers = SAFE_MALLOC((size_t)max_sources * sizeof(audio_ring_buffer_t *), audio_ring_buffer_t **);
  if (!mixer->source_buffers) {
    SAFE_FREE(mixer);
    return NULL;
  }

  mixer->source_ids = SAFE_MALLOC((size_t)max_sources * sizeof(uint32_t), uint32_t *);
  if (!mixer->source_ids) {
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer);
    return NULL;
  }

  mixer->source_active = SAFE_MALLOC((size_t)max_sources * sizeof(bool), bool *);
  if (!mixer->source_active) {
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer);
    return NULL;
  }

  // Initialize arrays
  SAFE_MEMSET((void *)mixer->source_buffers, (size_t)max_sources * sizeof(audio_ring_buffer_t *), 0,
              (size_t)max_sources * sizeof(audio_ring_buffer_t *));
  SAFE_MEMSET(mixer->source_ids, (size_t)max_sources * sizeof(uint32_t), 0, (size_t)max_sources * sizeof(uint32_t));
  SAFE_MEMSET(mixer->source_active, (size_t)max_sources * sizeof(bool), 0, (size_t)max_sources * sizeof(bool));

  // OPTIMIZATION 1: Initialize bitset optimization structures
  mixer->active_sources_mask = 0ULL; // No sources active initially
  SAFE_MEMSET(mixer->source_id_to_index, sizeof(mixer->source_id_to_index), 0xFF,
              sizeof(mixer->source_id_to_index)); // 0xFF = invalid index

  // Allocate mix buffer BEFORE rwlock_init so cleanup path is correct
  mixer->mix_buffer = SAFE_MALLOC(MIXER_FRAME_SIZE * sizeof(float), float *);
  if (!mixer->mix_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate mix buffer");
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer->source_active);
    SAFE_FREE(mixer);
    return NULL;
  }

  // OPTIMIZATION 2: Initialize reader-writer lock
  if (rwlock_init(&mixer->source_lock) != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize mixer source lock");
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer->source_active);
    SAFE_FREE(mixer->mix_buffer);
    SAFE_FREE(mixer);
    return NULL;
  }

  // Set crowd scaling parameters
  mixer->crowd_alpha = 0.5f; // Square root scaling
  mixer->base_gain = 1.0f;   // Unity gain - prevents clipping (soft_clip handles peaks)

  // Initialize processing
  if (ducking_init(&mixer->ducking, max_sources, (float)sample_rate) != ASCIICHAT_OK) {
    rwlock_destroy(&mixer->source_lock);
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer->source_active);
    SAFE_FREE(mixer);
    return NULL;
  }
  compressor_init(&mixer->compressor, (float)sample_rate);

  // Allocate mix buffer
  mixer->mix_buffer = SAFE_MALLOC(MIXER_FRAME_SIZE * sizeof(float), float *);
  if (!mixer->mix_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate mix buffer");
    ducking_free(&mixer->ducking);
    rwlock_destroy(&mixer->source_lock);
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer->source_active);
    SAFE_FREE(mixer);
    return NULL;
  }

  log_info("Audio mixer created: max_sources=%d, sample_rate=%d", max_sources, sample_rate);

  return mixer;
}

void mixer_destroy(mixer_t *mixer) {
  if (!mixer)
    return;

  // OPTIMIZATION 2: Destroy reader-writer lock
  rwlock_destroy(&mixer->source_lock);

  ducking_free(&mixer->ducking);

  SAFE_FREE(mixer->source_buffers);
  SAFE_FREE(mixer->source_ids);
  SAFE_FREE(mixer->source_active);
  SAFE_FREE(mixer->mix_buffer);
  SAFE_FREE(mixer);
  log_info("Audio mixer destroyed");
}

int mixer_add_source(mixer_t *mixer, uint32_t client_id, audio_ring_buffer_t *buffer) {
  if (!mixer || !buffer)
    return -1;

  // OPTIMIZATION 2: Acquire write lock for source modification
  rwlock_wrlock(&mixer->source_lock);

  // Find an empty slot
  int slot = -1;
  for (int i = 0; i < mixer->max_sources; i++) {
    if (mixer->source_ids[i] == 0) {
      slot = i;
      break;
    }
  }

  if (slot == -1) {
    rwlock_wrunlock(&mixer->source_lock);
    log_warn("Mixer: No available slots for client %u", client_id);
    return -1;
  }

  mixer->source_buffers[slot] = buffer;
  mixer->source_ids[slot] = client_id;
  mixer->source_active[slot] = true;
  mixer->num_sources++;

  // OPTIMIZATION 1: Update bitset optimization structures
  mixer->active_sources_mask |= (1ULL << slot);                // Set bit for this slot
  mixer->source_id_to_index[client_id & 0xFF] = (uint8_t)slot; // Hash table: client_id → slot

  rwlock_wrunlock(&mixer->source_lock);

  log_info("Mixer: Added source for client %u at slot %d", client_id, slot);
  return slot;
}

void mixer_remove_source(mixer_t *mixer, uint32_t client_id) {
  if (!mixer)
    return;

  // OPTIMIZATION 2: Acquire write lock for source modification
  rwlock_wrlock(&mixer->source_lock);

  for (int i = 0; i < mixer->max_sources; i++) {
    if (mixer->source_ids[i] == client_id) {
      mixer->source_buffers[i] = NULL;
      mixer->source_ids[i] = 0;
      mixer->source_active[i] = false;
      mixer->num_sources--;

      // OPTIMIZATION 1: Update bitset optimization structures
      mixer->active_sources_mask &= ~(1ULL << i);         // Clear bit for this slot
      mixer->source_id_to_index[client_id & 0xFF] = 0xFF; // Mark as invalid in hash table

      // Reset ducking state for this source
      mixer->ducking.envelope[i] = 0.0f;
      mixer->ducking.gain[i] = 1.0f;

      rwlock_wrunlock(&mixer->source_lock);

      log_info("Mixer: Removed source for client %u from slot %d", client_id, i);
      return;
    }
  }

  rwlock_wrunlock(&mixer->source_lock);
}

void mixer_set_source_active(mixer_t *mixer, uint32_t client_id, bool active) {
  if (!mixer)
    return;

  // OPTIMIZATION 2: Acquire write lock for source modification
  rwlock_wrlock(&mixer->source_lock);

  for (int i = 0; i < mixer->max_sources; i++) {
    if (mixer->source_ids[i] == client_id) {
      mixer->source_active[i] = active;

      // OPTIMIZATION 1: Update bitset for active state change
      if (active) {
        mixer->active_sources_mask |= (1ULL << i); // Set bit
      } else {
        mixer->active_sources_mask &= ~(1ULL << i); // Clear bit
      }

      rwlock_wrunlock(&mixer->source_lock);
      log_debug("Mixer: Set source %u active=%d", client_id, active);
      return;
    }
  }

  rwlock_wrunlock(&mixer->source_lock);
}

int mixer_process(mixer_t *mixer, float *output, int num_samples) {
  if (!mixer || !output || num_samples <= 0)
    return -1;

  // THREAD SAFETY: Acquire read lock to protect against concurrent source add/remove
  // This prevents race conditions where source_buffers[i] could be set to NULL while we read it
  rwlock_rdlock(&mixer->source_lock);

  // Clear output buffer
  SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));

  // Count active sources
  int active_count = 0;
  for (int i = 0; i < mixer->max_sources; i++) {
    if (mixer->source_ids[i] != 0 && mixer->source_active[i] && mixer->source_buffers[i]) {
      active_count++;
    }
  }

  if (active_count == 0) {
    // No active sources, output silence
    return 0;
  }

  // Process in frames for efficiency
  for (int frame_start = 0; frame_start < num_samples; frame_start += MIXER_FRAME_SIZE) {
    int frame_size = (frame_start + MIXER_FRAME_SIZE > num_samples) ? (num_samples - frame_start) : MIXER_FRAME_SIZE;

    // Clear mix buffer
    SAFE_MEMSET(mixer->mix_buffer, frame_size * sizeof(float), 0, frame_size * sizeof(float));

    // Temporary buffers for source audio
    float source_samples[MIXER_MAX_SOURCES][MIXER_FRAME_SIZE];
    int source_count = 0;
    int source_map[MIXER_MAX_SOURCES]; // Maps source index to slot

    // Read from each active source
    for (int i = 0; i < mixer->max_sources; i++) {
      if (mixer->source_ids[i] != 0 && mixer->source_active[i] && mixer->source_buffers[i]) {
        // Read samples from this source's ring buffer
        size_t samples_read_size =
            audio_ring_buffer_read(mixer->source_buffers[i], source_samples[source_count], frame_size);
        int samples_read = (int)samples_read_size;

        // Accept partial frames - pad with silence if needed
        // This prevents audio dropouts when ring buffers are temporarily under-filled
        if (samples_read > 0) {
          // Pad remaining samples with silence if we got a partial frame
          if (samples_read < frame_size) {
            SAFE_MEMSET(&source_samples[source_count][samples_read], (frame_size - samples_read) * sizeof(float), 0,
                        (frame_size - samples_read) * sizeof(float));
          }

          source_map[source_count] = i;
          source_count++;

          if (source_count >= MIXER_MAX_SOURCES)
            break;
        }
      }
    }

    // OPTIMIZATION: Batch envelope calculation per-frame instead of per-sample
    // Calculate peak amplitude for each source over the entire frame
    int speaking_count = 0;

    for (int i = 0; i < source_count; i++) {
      int slot = source_map[i];
      float peak = 0.0f;

      // Find peak amplitude in frame (much faster than per-sample envelope)
      for (int s = 0; s < frame_size; s++) {
        float abs_sample = fabsf(source_samples[i][s]);
        if (abs_sample > peak)
          peak = abs_sample;
      }

      // Update envelope using frame peak (one update per frame instead of per-sample)
      if (peak > mixer->ducking.envelope[slot]) {
        mixer->ducking.envelope[slot] =
            mixer->ducking.attack_coeff * mixer->ducking.envelope[slot] + (1.0f - mixer->ducking.attack_coeff) * peak;
      } else {
        mixer->ducking.envelope[slot] =
            mixer->ducking.release_coeff * mixer->ducking.envelope[slot] + (1.0f - mixer->ducking.release_coeff) * peak;
      }

      // Count speaking sources
      if (mixer->ducking.envelope[slot] > db_to_linear(-60.0f))
        speaking_count++;
    }

    // Apply ducking ONCE per frame (not per-sample)
    ducking_process_frame(&mixer->ducking, mixer->ducking.envelope, mixer->ducking.gain, mixer->max_sources);

    // Calculate crowd scaling ONCE per frame
    float crowd_gain = (speaking_count > 0) ? (1.0f / powf((float)speaking_count, mixer->crowd_alpha)) : 1.0f;
    float pre_bus = mixer->base_gain * crowd_gain;

    // Pre-calculate combined gains for each source (ducking * pre_bus)
    float combined_gains[MIXER_MAX_SOURCES];
    for (int i = 0; i < source_count; i++) {
      int slot = source_map[i];
      combined_gains[i] = mixer->ducking.gain[slot] * pre_bus;
    }

    // Fast mixing loop - simple multiply-add with pre-calculated gains
    for (int s = 0; s < frame_size; s++) {
      float mix = 0.0f;
      for (int i = 0; i < source_count; i++) {
        mix += source_samples[i][s] * combined_gains[i];
      }

      // Apply bus compression (still per-sample for smooth dynamics)
      float comp_gain = compressor_process_sample(&mixer->compressor, mix);
      mix *= comp_gain;

      // Soft clip to prevent harsh digital clipping artifacts (threshold 0.8)
      output[frame_start + s] = soft_clip(mix, 0.8f);
    }
  }

  rwlock_rdunlock(&mixer->source_lock);
  return num_samples;
}

int mixer_process_excluding_source(mixer_t *mixer, float *output, int num_samples, uint32_t exclude_client_id) {
  if (!mixer || !output || num_samples <= 0)
    return -1;

  START_TIMER("mixer_total");

  // THREAD SAFETY: Acquire read lock to protect against concurrent source add/remove
  // This prevents race conditions where source_buffers[i] could be set to NULL while we read it
  rwlock_rdlock(&mixer->source_lock);

  // Clear output buffer
  SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));

  // OPTIMIZATION 1: O(1) exclusion using bitset and hash table
  uint8_t exclude_index = mixer->source_id_to_index[exclude_client_id & 0xFF];
  uint64_t active_mask = mixer->active_sources_mask;

  // BUGFIX: Validate exclude_index before using in bitshift
  // - exclude_index == 0xFF means "not found" (sentinel value from initialization)
  // - exclude_index >= MIXER_MAX_SOURCES would cause undefined behavior in bitshift
  // - Also verify hash table lookup actually matched the client_id (collision detection)
  if (exclude_index < MIXER_MAX_SOURCES && exclude_index != 0xFF &&
      mixer->source_ids[exclude_index] == exclude_client_id) {
    active_mask &= ~(1ULL << exclude_index);
  }

  // Fast check: any sources to process?
  if (active_mask == 0) {
    rwlock_rdunlock(&mixer->source_lock);
    STOP_TIMER("mixer_total");
    return 0; // No active sources (excluding the specified client), output silence
  }

  // Process in frames for efficiency
  for (int frame_start = 0; frame_start < num_samples; frame_start += MIXER_FRAME_SIZE) {
    int frame_size = (frame_start + MIXER_FRAME_SIZE > num_samples) ? (num_samples - frame_start) : MIXER_FRAME_SIZE;

    // Clear mix buffer
    SAFE_MEMSET(mixer->mix_buffer, frame_size * sizeof(float), 0, frame_size * sizeof(float));

    // Temporary buffers for source audio
    float source_samples[MIXER_MAX_SOURCES][MIXER_FRAME_SIZE];
    int source_count = 0;
    int source_map[MIXER_MAX_SOURCES]; // Maps source index to slot

    START_TIMER("mixer_read_sources");
    // OPTIMIZATION 1: Iterate only over active sources using bitset
    uint64_t current_mask = active_mask;
    while (current_mask && source_count < MIXER_MAX_SOURCES) {
      int i = find_first_set_bit(current_mask); // Portable: find first set bit
      current_mask &= current_mask - 1;         // Clear lowest set bit

      // Verify source is valid (defensive programming)
      if (i < mixer->max_sources && mixer->source_ids[i] != 0 && mixer->source_buffers[i]) {
        // Read samples from this source's ring buffer
        size_t samples_read_size =
            audio_ring_buffer_read(mixer->source_buffers[i], source_samples[source_count], frame_size);
        int samples_read = (int)samples_read_size;

        // DEBUG: Log what we read from ring buffer
        if (samples_read > 0) {
          float peak = 0.0f, rms = 0.0f;
          for (int s = 0; s < samples_read && s < 10; s++) {
            float abs_val = fabsf(source_samples[source_count][s]);
            if (abs_val > peak)
              peak = abs_val;
            rms += source_samples[source_count][s] * source_samples[source_count][s];
          }
          rms = sqrtf(rms / (samples_read > 10 ? 10 : samples_read));
          log_debug_every(1000000, "Mixer: Source %u read %d samples - Peak=%.6f, RMS=%.6f, First3=[%.6f,%.6f,%.6f]",
                          mixer->source_ids[i], samples_read, peak, rms,
                          samples_read > 0 ? source_samples[source_count][0] : 0.0f,
                          samples_read > 1 ? source_samples[source_count][1] : 0.0f,
                          samples_read > 2 ? source_samples[source_count][2] : 0.0f);
        }

        // Accept partial frames - pad with silence if needed
        // This prevents audio dropouts when ring buffers are temporarily under-filled
        if (samples_read > 0) {
          // Pad remaining samples with silence if we got a partial frame
          if (samples_read < frame_size) {
            SAFE_MEMSET(&source_samples[source_count][samples_read], (frame_size - samples_read) * sizeof(float), 0,
                        (frame_size - samples_read) * sizeof(float));
          }

          source_map[source_count] = i;
          source_count++;
        }
      }
    }
    STOP_TIMER("mixer_read_sources");

    START_TIMER("mixer_per_sample_loop");

    // OPTIMIZATION: Batch envelope calculation per-frame instead of per-sample
    // Calculate peak amplitude for each source over the entire frame
    int speaking_count = 0;

    for (int i = 0; i < source_count; i++) {
      int slot = source_map[i];
      float peak = 0.0f;

      // Find peak amplitude in frame (much faster than per-sample envelope)
      for (int s = 0; s < frame_size; s++) {
        float abs_sample = fabsf(source_samples[i][s]);
        if (abs_sample > peak)
          peak = abs_sample;
      }

      // Update envelope using frame peak (one update per frame instead of per-sample)
      if (peak > mixer->ducking.envelope[slot]) {
        mixer->ducking.envelope[slot] =
            mixer->ducking.attack_coeff * mixer->ducking.envelope[slot] + (1.0f - mixer->ducking.attack_coeff) * peak;
      } else {
        mixer->ducking.envelope[slot] =
            mixer->ducking.release_coeff * mixer->ducking.envelope[slot] + (1.0f - mixer->ducking.release_coeff) * peak;
      }

      // Count speaking sources
      if (mixer->ducking.envelope[slot] > db_to_linear(-60.0f))
        speaking_count++;
    }

    // Apply ducking ONCE per frame (not per-sample)
    ducking_process_frame(&mixer->ducking, mixer->ducking.envelope, mixer->ducking.gain, mixer->max_sources);

    // Calculate crowd scaling ONCE per frame
    float crowd_gain = (speaking_count > 0) ? (1.0f / powf((float)speaking_count, mixer->crowd_alpha)) : 1.0f;
    float pre_bus = mixer->base_gain * crowd_gain;

    // Pre-calculate combined gains for each source (ducking * pre_bus)
    float combined_gains[MIXER_MAX_SOURCES];
    for (int i = 0; i < source_count; i++) {
      int slot = source_map[i];
      combined_gains[i] = mixer->ducking.gain[slot] * pre_bus;
    }

    // Fast mixing loop - simple multiply-add with pre-calculated gains
    float output_peak = 0.0f, output_rms = 0.0f;
    for (int s = 0; s < frame_size; s++) {
      float mix = 0.0f;
      for (int i = 0; i < source_count; i++) {
        mix += source_samples[i][s] * combined_gains[i];
      }

      // Apply bus compression (still per-sample for smooth dynamics)
      float comp_gain = compressor_process_sample(&mixer->compressor, mix);
      mix *= comp_gain;

      // Soft clip to prevent harsh digital clipping artifacts (threshold 0.8)
      float clipped = soft_clip(mix, 0.8f);
      output[frame_start + s] = clipped;

      // DEBUG: Track output stats
      float abs_val = fabsf(clipped);
      if (abs_val > output_peak)
        output_peak = abs_val;
      output_rms += clipped * clipped;
    }
    output_rms = sqrtf(output_rms / frame_size);

    log_debug_every(1000000, "Mixer output frame (size=%d): Peak=%.6f, RMS=%.6f, sources=%d, speaking=%d", frame_size,
                    output_peak, output_rms, source_count, speaking_count);

    STOP_TIMER("mixer_per_sample_loop");
  }

  rwlock_rdunlock(&mixer->source_lock);

  double total_ns = STOP_TIMER("mixer_total");
  if (total_ns > 2000000) { // > 2ms
    char duration_str[32];
    format_duration_ns(total_ns, duration_str, sizeof(duration_str));
    log_warn("Slow mixer: total=%s, num_samples=%d", duration_str, num_samples);
  }

  return num_samples;
}

/* ============================================================================
 * Noise Gate Implementation
 * ============================================================================
 */

void noise_gate_init(noise_gate_t *gate, float sample_rate) {
  if (!gate)
    return;

  gate->sample_rate = sample_rate;
  gate->envelope = 0.0f;
  gate->gate_open = false;

  // Default parameters
  noise_gate_set_params(gate, 0.01f, 2.0f, 50.0f, 0.9f);
}

void noise_gate_set_params(noise_gate_t *gate, float threshold, float attack_ms, float release_ms, float hysteresis) {
  if (!gate)
    return;

  gate->threshold = threshold;
  gate->attack_ms = attack_ms;
  gate->release_ms = release_ms;
  gate->hysteresis = hysteresis;

  // Calculate coefficients for envelope follower
  // Using exponential moving average: coeff = 1 - exp(-1 / (time_ms * sample_rate / 1000))
  gate->attack_coeff = 1.0f - expf(-1.0f / (attack_ms * gate->sample_rate / 1000.0f));
  gate->release_coeff = 1.0f - expf(-1.0f / (release_ms * gate->sample_rate / 1000.0f));
}

float noise_gate_process_sample(noise_gate_t *gate, float input, float peak_amplitude) {
  if (!gate)
    return input;

  // Determine target state with hysteresis
  float target;
  if (gate->gate_open) {
    // Gate is open - use lower threshold (hysteresis) to close
    target = (peak_amplitude > gate->threshold * gate->hysteresis) ? 1.0f : 0.0f;
  } else {
    // Gate is closed - use normal threshold to open
    target = (peak_amplitude > gate->threshold) ? 1.0f : 0.0f;
  }

  // Update gate state
  gate->gate_open = (target > 0.5f);

  // Update envelope with appropriate coefficient
  float coeff = (target > gate->envelope) ? gate->attack_coeff : gate->release_coeff;
  gate->envelope += coeff * (target - gate->envelope);

  // Apply gate
  return input * gate->envelope;
}

void noise_gate_process_buffer(noise_gate_t *gate, float *buffer, int num_samples) {
  if (!gate || !buffer || num_samples <= 0)
    return;

  // First pass: find peak amplitude
  float peak = 0.0f;
  for (int i = 0; i < num_samples; i++) {
    float abs_sample = fabsf(buffer[i]);
    if (abs_sample > peak) {
      peak = abs_sample;
    }
  }

  // Second pass: apply gate
  for (int i = 0; i < num_samples; i++) {
    buffer[i] = noise_gate_process_sample(gate, buffer[i], peak);
  }
}

bool noise_gate_is_open(const noise_gate_t *gate) {
  return gate ? gate->gate_open : false;
}

/* ============================================================================
 * High-Pass Filter Implementation
 * ============================================================================
 */

void highpass_filter_init(highpass_filter_t *filter, float cutoff_hz, float sample_rate) {
  if (!filter)
    return;

  filter->cutoff_hz = cutoff_hz;
  filter->sample_rate = sample_rate;

  // Calculate filter coefficient
  // alpha = 1 / (1 + 2*pi*fc/fs)
  filter->alpha = 1.0f / (1.0f + 2.0f * M_PI * cutoff_hz / sample_rate);

  highpass_filter_reset(filter);
}

void highpass_filter_reset(highpass_filter_t *filter) {
  if (!filter)
    return;

  filter->prev_input = 0.0f;
  filter->prev_output = 0.0f;
}

float highpass_filter_process_sample(highpass_filter_t *filter, float input) {
  if (!filter)
    return input;

  // First-order high-pass filter
  // y[n] = alpha * (y[n-1] + x[n] - x[n-1])
  float output = filter->alpha * (filter->prev_output + input - filter->prev_input);

  filter->prev_input = input;
  filter->prev_output = output;

  return output;
}

void highpass_filter_process_buffer(highpass_filter_t *filter, float *buffer, int num_samples) {
  if (!filter || !buffer || num_samples <= 0)
    return;

  for (int i = 0; i < num_samples; i++) {
    buffer[i] = highpass_filter_process_sample(filter, buffer[i]);
  }
}

/* ============================================================================
 * Low-Pass Filter Implementation
 * ============================================================================
 */

void lowpass_filter_init(lowpass_filter_t *filter, float cutoff_hz, float sample_rate) {
  if (!filter)
    return;

  filter->cutoff_hz = cutoff_hz;
  filter->sample_rate = sample_rate;

  // Calculate filter coefficient using RC time constant formula
  // alpha = dt / (RC + dt) where RC = 1 / (2 * pi * fc)
  float dt = 1.0f / sample_rate;
  float rc = 1.0f / (2.0f * (float)M_PI * cutoff_hz);
  filter->alpha = dt / (rc + dt);

  lowpass_filter_reset(filter);
}

void lowpass_filter_reset(lowpass_filter_t *filter) {
  if (!filter)
    return;

  filter->prev_output = 0.0f;
}

float lowpass_filter_process_sample(lowpass_filter_t *filter, float input) {
  if (!filter)
    return input;

  // First-order IIR low-pass filter: y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
  float output = filter->alpha * input + (1.0f - filter->alpha) * filter->prev_output;

  filter->prev_output = output;

  return output;
}

void lowpass_filter_process_buffer(lowpass_filter_t *filter, float *buffer, int num_samples) {
  if (!filter || !buffer || num_samples <= 0)
    return;

  for (int i = 0; i < num_samples; i++) {
    buffer[i] = lowpass_filter_process_sample(filter, buffer[i]);
  }
}

/* ============================================================================
 * Soft Clipping Implementation
 * ============================================================================
 */

float soft_clip(float sample, float threshold) {
  if (sample > threshold) {
    // Soft clip positive values
    return threshold + (1.0f - threshold) * tanhf((sample - threshold) * 10.0f);
  }
  if (sample < -threshold) {
    // Soft clip negative values
    return -threshold + (-1.0f + threshold) * tanhf((sample + threshold) * 10.0f);
  }
  return sample;
}

void soft_clip_buffer(float *buffer, int num_samples, float threshold) {
  if (!buffer || num_samples <= 0)
    return;

  for (int i = 0; i < num_samples; i++) {
    buffer[i] = soft_clip(buffer[i], threshold);
  }
}
