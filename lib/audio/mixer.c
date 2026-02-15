/**
 * @file mixer.c
 * @ingroup audio
 * @brief 🎚️ Real-time audio mixer with ducking, gain control, and multi-stream blending
 */

#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h> // For asciichat_errno system
#include <ascii-chat/util/time.h>       // For timing instrumentation
#include <ascii-chat/util/bits.h>       // For find_first_set_bit
#include <ascii-chat/util/overflow.h>   // For overflow checking
#include <math.h>
#include <string.h>
#include <stdint.h>

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

  // Set default parameters with 0dB makeup gain (unity)
  // Soft clip at 0.7 provides 3dB headroom to prevent hard clipping
  // Ducking and crowd scaling naturally reduce volume, no compensation needed
  compressor_set_params(comp, -10.0f, 4.0f, 10 * NS_PER_MS_INT, 100 * NS_PER_MS_INT, 0.0f);
}

void compressor_set_params(compressor_t *comp, float threshold_dB, float ratio, uint64_t attack_ns, uint64_t release_ns,
                           float makeup_dB) {
  comp->threshold_dB = threshold_dB;
  comp->ratio = ratio;
  comp->attack_ns = attack_ns;
  comp->release_ns = release_ns;
  comp->makeup_dB = makeup_dB;
  comp->knee_dB = 2.0f; // Fixed soft knee

  // Calculate time constants
  float attack_tau = (float)attack_ns / NS_PER_SEC;
  float release_tau = (float)release_ns / NS_PER_SEC;
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
int ducking_init(ducking_t *duck, int num_sources, float sample_rate) {
  if (!duck) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ducking_init: duck is NULL");
  }

  // Set default parameters
  // threshold_dB: sources below this aren't considered "speaking"
  // leader_margin_dB: sources within this margin of loudest are all "leaders"
  // atten_dB: how much to attenuate non-leaders (was -12dB, too aggressive)
  duck->threshold_dB = -45.0f;            // More lenient threshold
  duck->leader_margin_dB = 6.0f;          // Wider margin = more sources treated as leaders
  duck->atten_dB = -6.0f;                 // Only -6dB attenuation (was -12dB)
  duck->attack_ns = 10 * NS_PER_MS_INT;   // Slower attack (10ms)
  duck->release_ns = 200 * NS_PER_MS_INT; // Slower release (200ms)
  duck->envelope = NULL;
  duck->gain = NULL;

  // Calculate time constants
  float attack_tau = (float)duck->attack_ns / NS_PER_SEC;
  float release_tau = (float)duck->release_ns / NS_PER_SEC;
  duck->attack_coeff = expf(-1.0f / (attack_tau * sample_rate + 1e-12f));
  duck->release_coeff = expf(-1.0f / (release_tau * sample_rate + 1e-12f));

  // Allocate arrays with overflow checking
  size_t envelope_size = 0;
  if (checked_size_mul((size_t)num_sources, sizeof(float), &envelope_size) != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Ducking envelope array size overflow: %d sources", num_sources);
  }
  duck->envelope = SAFE_MALLOC(envelope_size, float *);
  if (!duck->envelope) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate ducking envelope array");
  }

  size_t gain_size = 0;
  if (checked_size_mul((size_t)num_sources, sizeof(float), &gain_size) != ASCIICHAT_OK) {
    SAFE_FREE(duck->envelope);
    duck->envelope = NULL;
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Ducking gain array size overflow: %d sources", num_sources);
  }
  duck->gain = SAFE_MALLOC(gain_size, float *);
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

void ducking_destroy(ducking_t *duck) {
  if (duck->envelope) {
    SAFE_FREE(duck->envelope);
  }
  if (duck->gain) {
    SAFE_FREE(duck->gain);
  }
}

void ducking_set_params(ducking_t *duck, float threshold_dB, float leader_margin_dB, float atten_dB, uint64_t attack_ns,
                        uint64_t release_ns) {
  duck->threshold_dB = threshold_dB;
  duck->leader_margin_dB = leader_margin_dB;
  duck->atten_dB = atten_dB;
  duck->attack_ns = attack_ns;
  duck->release_ns = release_ns;
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

  // Allocate source management arrays with overflow checking
  size_t buffers_size = 0;
  if (checked_size_mul((size_t)max_sources, sizeof(audio_ring_buffer_t *), &buffers_size) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Mixer source buffers array overflow: %d sources", max_sources);
    SAFE_FREE(mixer);
    return NULL;
  }
  mixer->source_buffers = SAFE_MALLOC(buffers_size, audio_ring_buffer_t **);
  if (!mixer->source_buffers) {
    SAFE_FREE(mixer);
    return NULL;
  }

  size_t ids_size = 0;
  if (checked_size_mul((size_t)max_sources, sizeof(uint32_t), &ids_size) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Mixer source IDs array overflow: %d sources", max_sources);
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer);
    return NULL;
  }
  mixer->source_ids = SAFE_MALLOC(ids_size, uint32_t *);
  if (!mixer->source_ids) {
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer);
    return NULL;
  }

  size_t active_size = 0;
  if (checked_size_mul((size_t)max_sources, sizeof(bool), &active_size) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Mixer source active array overflow: %d sources", max_sources);
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer);
    return NULL;
  }
  mixer->source_active = SAFE_MALLOC(active_size, bool *);
  if (!mixer->source_active) {
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer);
    return NULL;
  }

  // Initialize arrays
  SAFE_MEMSET((void *)mixer->source_buffers, buffers_size, 0, buffers_size);
  SAFE_MEMSET(mixer->source_ids, ids_size, 0, ids_size);
  SAFE_MEMSET(mixer->source_active, active_size, 0, active_size);

  // OPTIMIZATION 1: Initialize bitset optimization structures
  mixer->active_sources_mask = 0ULL; // No sources active initially
  for (int i = 0; i < 256; i++) {
    mixer->source_id_to_index[i] = MIXER_HASH_INVALID;
  }

  // Allocate mix buffer BEFORE rwlock_init so cleanup path is correct
  size_t mix_buffer_size = 0;
  if (checked_size_mul(MIXER_FRAME_SIZE, sizeof(float), &mix_buffer_size) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Mixer mix buffer size overflow: %zu samples", (size_t)MIXER_FRAME_SIZE);
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer->source_active);
    SAFE_FREE(mixer);
    return NULL;
  }
  mixer->mix_buffer = SAFE_MALLOC(mix_buffer_size, float *);
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
  mixer->base_gain = 1.0f;   // Unity gain - let compressor handle loudness, avoid clipping

  // Initialize processing
  if (ducking_init(&mixer->ducking, max_sources, (float)sample_rate) != ASCIICHAT_OK) {
    rwlock_destroy(&mixer->source_lock);
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer->source_active);
    SAFE_FREE(mixer->mix_buffer);
    SAFE_FREE(mixer);
    return NULL;
  }
  compressor_init(&mixer->compressor, (float)sample_rate);

  log_debug("Audio mixer created: max_sources=%d, sample_rate=%d", max_sources, sample_rate);

  return mixer;
}

void mixer_destroy(mixer_t *mixer) {
  if (!mixer)
    return;

  // OPTIMIZATION 2: Destroy reader-writer lock
  rwlock_destroy(&mixer->source_lock);

  ducking_destroy(&mixer->ducking);

  SAFE_FREE(mixer->source_buffers);
  SAFE_FREE(mixer->source_ids);
  SAFE_FREE(mixer->source_active);
  SAFE_FREE(mixer->mix_buffer);
  SAFE_FREE(mixer);
  log_debug("Audio mixer destroyed");
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
  mixer->active_sources_mask |= (1ULL << slot);         // Set bit for this slot
  mixer_hash_set_slot(mixer, client_id, (uint8_t)slot); // Hash table: client_id → slot

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
      mixer->active_sources_mask &= ~(1ULL << i); // Clear bit for this slot
      mixer_hash_mark_invalid(mixer, client_id);  // Mark as invalid in hash table

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

    // OPTIMIZATION: Fast mixing loop - simple multiply-add with pre-calculated gains
    // NO per-sample compressor to avoid expensive log/pow calls (480x per iteration)
    for (int s = 0; s < frame_size; s++) {
      float mix = 0.0f;
      for (int i = 0; i < source_count; i++) {
        mix += source_samples[i][s] * combined_gains[i];
      }

      // Store in mix buffer for frame-level compression below
      mixer->mix_buffer[s] = mix;
    }

    // OPTIMIZATION: Apply compression ONCE per frame instead of per-sample
    // This reduces expensive log10f/powf calls from 480x to 1x per iteration
    // Calculate frame peak for compressor sidechain
    float frame_peak = 0.0f;
    for (int s = 0; s < frame_size; s++) {
      float abs_val = fabsf(mixer->mix_buffer[s]);
      if (abs_val > frame_peak)
        frame_peak = abs_val;
    }

    // Process compressor with frame peak (1 call instead of 480)
    float comp_gain = compressor_process_sample(&mixer->compressor, frame_peak);

    // Apply compression gain and soft clipping to all samples
    for (int s = 0; s < frame_size; s++) {
      float mix = mixer->mix_buffer[s] * comp_gain;

      // Soft clip at 0.7 to provide 3dB headroom and prevent hard clipping
      // With +6dB makeup gain, peaks can exceed 1.0, so we need headroom
      // threshold=0.7 maps asymptotically toward 1.0, preventing harsh distortion
      output[frame_start + s] = soft_clip(mix, 0.7f, 3.0f);
    }
  }

  rwlock_rdunlock(&mixer->source_lock);
  return num_samples;
}

int mixer_process_excluding_source(mixer_t *mixer, float *output, int num_samples, uint32_t exclude_client_id) {
  if (!mixer || !output || num_samples <= 0)
    return -1;

  // Only use timing in debug builds - snprintf + hashtable ops are expensive in hot path
#ifndef NDEBUG
  START_TIMER("mixer_total");
#endif

  // THREAD SAFETY: Acquire read lock to protect against concurrent source add/remove
  // This prevents race conditions where source_buffers[i] could be set to NULL while we read it
  rwlock_rdlock(&mixer->source_lock);

  // Clear output buffer
  SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));

  // OPTIMIZATION 1: O(1) exclusion using bitset and hash table
  uint8_t exclude_index = mixer_hash_get_slot(mixer, exclude_client_id);
  uint64_t active_mask = mixer->active_sources_mask;

  // Validate exclude_index before using in bitshift
  // - exclude_index == MIXER_HASH_INVALID means "not found" (sentinel value from initialization)
  // - exclude_index >= MIXER_MAX_SOURCES would cause undefined behavior in bitshift
  // - Also verify hash table lookup actually matched the client_id (collision detection)
  bool valid_exclude = (exclude_index < MIXER_MAX_SOURCES && exclude_index != MIXER_HASH_INVALID &&
                        mixer->source_ids[exclude_index] == exclude_client_id);

  if (valid_exclude) {
    // Apply exclusion to prevent echo feedback
    uint64_t mask_without_excluded = active_mask & ~(1ULL << exclude_index);
    active_mask = mask_without_excluded;

    // DIAGNOSTIC: Log which client was excluded and which remain active
    log_dev_every(
        4500000,
        "MIXER EXCLUSION: exclude_client=%u, exclude_index=%u, active_mask_before=0x%llx, active_mask_after=0x%llx",
        exclude_client_id, exclude_index, (unsigned long long)(active_mask | (1ULL << exclude_index)),
        (unsigned long long)active_mask);
  } else {
    // DIAGNOSTIC: Failed to exclude - log why
    log_dev_every(4500000, "MIXER EXCLUSION FAILED: exclude_client=%u, exclude_index=%u (valid=%d), lookup_id=%u",
                  exclude_client_id, exclude_index, valid_exclude,
                  (exclude_index < MIXER_MAX_SOURCES && exclude_index != 0xFF) ? mixer->source_ids[exclude_index] : 0);
  }

  // Fast check: any sources to mix?
  if (active_mask == 0) {
    rwlock_rdunlock(&mixer->source_lock);
#ifndef NDEBUG
    STOP_TIMER("mixer_total");
#endif
    return 0; // No sources to mix (excluding the specified client), output silence
  }

  // Process in frames for efficiency
  for (int frame_start = 0; frame_start < num_samples; frame_start += MIXER_FRAME_SIZE) {
    int frame_size = (frame_start + MIXER_FRAME_SIZE > num_samples) ? (num_samples - frame_start) : MIXER_FRAME_SIZE;

    uint64_t read_start_ns = time_get_ns();

    // Clear mix buffer
    SAFE_MEMSET(mixer->mix_buffer, frame_size * sizeof(float), 0, frame_size * sizeof(float));

    // Temporary buffers for source audio
    float source_samples[MIXER_MAX_SOURCES][MIXER_FRAME_SIZE];
    int source_count = 0;
    int source_map[MIXER_MAX_SOURCES]; // Maps source index to slot

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

        // Accept partial frames - pad with silence if needed
        // This prevents audio dropouts when ring buffers are temporarily under-filled
        if (samples_read > 0) {
          // Pad remaining samples with silence if we got a partial frame
          if (samples_read < frame_size) {
            SAFE_MEMSET(&source_samples[source_count][samples_read], (frame_size - samples_read) * sizeof(float), 0,
                        (frame_size - samples_read) * sizeof(float));
          }

          // DIAGNOSTIC: Calculate RMS of this source's audio
          float source_rms = 0.0f;
          if (frame_start == 0) { // Only log for first frame to reduce spam
            float sum_squares = 0.0f;
            for (int s = 0; s < samples_read; s++) {
              sum_squares += source_samples[source_count][s] * source_samples[source_count][s];
            }
            source_rms = sqrtf(sum_squares / (float)samples_read);
            log_info_every(1000000, "MIXER SOURCE READ: client_id=%u, slot=%d, samples_read=%d, RMS=%.6f",
                           mixer->source_ids[i], i, samples_read, source_rms);
          }

          source_map[source_count] = i;
          source_count++;
        }
      }
    }

    uint64_t read_end_ns = time_get_ns();
    uint64_t read_time_ns = time_elapsed_ns(read_start_ns, read_end_ns);
    uint64_t read_time_us = time_ns_to_us(read_time_ns);

    if (read_time_ns > 10 * NS_PER_MS_INT) {
      log_warn_every(LOG_RATE_DEFAULT, "Mixer: Slow source reading took %lluus (%.2fms) for %d sources", read_time_us,
                     (float)read_time_us / 1000.0f, source_count);
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

    // OPTIMIZATION: Fast mixing loop - simple multiply-add with pre-calculated gains
    // NO per-sample compressor to avoid expensive log/pow calls (480x per iteration)
    for (int s = 0; s < frame_size; s++) {
      float mix = 0.0f;
      for (int i = 0; i < source_count; i++) {
        mix += source_samples[i][s] * combined_gains[i];
      }

      // Store in mix buffer for frame-level compression below
      mixer->mix_buffer[s] = mix;
    }

    // OPTIMIZATION: Apply compression ONCE per frame instead of per-sample
    // This reduces expensive log10f/powf calls from 480x to 1x per iteration
    // Calculate frame peak for compressor sidechain
    float frame_peak = 0.0f;
    for (int s = 0; s < frame_size; s++) {
      float abs_val = fabsf(mixer->mix_buffer[s]);
      if (abs_val > frame_peak)
        frame_peak = abs_val;
    }

    // Process compressor with frame peak (1 call instead of 480)
    float comp_gain = compressor_process_sample(&mixer->compressor, frame_peak);

    // Apply compression gain and soft clipping to all samples
    for (int s = 0; s < frame_size; s++) {
      float mix = mixer->mix_buffer[s] * comp_gain;

      // Soft clip at 0.7 to provide 3dB headroom and prevent hard clipping
      // With +6dB makeup gain, peaks can exceed 1.0, so we need headroom
      // threshold=0.7 maps asymptotically toward 1.0, preventing harsh distortion
      output[frame_start + s] = soft_clip(mix, 0.7f, 3.0f);
    }
  }

  rwlock_rdunlock(&mixer->source_lock);

#ifndef NDEBUG
  STOP_TIMER_AND_LOG_EVERY(warn, NS_PER_SEC_INT, 2 * NS_PER_MS_INT, "mixer_total", "Mixer took");
#endif

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
  // Slower attack (10ms) prevents clicking artifacts during gate transitions
  // threshold=0.01, attack=10ms, release=50ms, hysteresis=0.9
  noise_gate_set_params(gate, 0.01f, 10 * NS_PER_MS_INT, 50 * NS_PER_MS_INT, 0.9f);
}

void noise_gate_set_params(noise_gate_t *gate, float threshold, uint64_t attack_ns, uint64_t release_ns,
                           float hysteresis) {
  if (!gate)
    return;

  gate->threshold = threshold;
  gate->attack_ns = attack_ns;
  gate->release_ns = release_ns;
  gate->hysteresis = hysteresis;

  // Calculate coefficients for envelope follower
  // Using exponential moving average: coeff = 1 - exp(-1 / (time_s * sample_rate))
  float attack_s = (float)attack_ns / NS_PER_SEC;
  float release_s = (float)release_ns / NS_PER_SEC;
  gate->attack_coeff = 1.0f - expf(-1.0f / (attack_s * gate->sample_rate + 1e-12f));
  gate->release_coeff = 1.0f - expf(-1.0f / (release_s * gate->sample_rate + 1e-12f));
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

float soft_clip(float sample, float threshold, float steepness) {
  if (sample > threshold) {
    // Soft clip positive values using tanh curve
    // Maps samples above threshold asymptotically toward 1.0
    return threshold + (1.0f - threshold) * tanhf((sample - threshold) * steepness);
  }
  if (sample < -threshold) {
    // Soft clip negative values symmetrically
    return -threshold + (-1.0f + threshold) * tanhf((sample + threshold) * steepness);
  }
  return sample;
}

void soft_clip_buffer(float *buffer, int num_samples, float threshold, float steepness) {
  if (!buffer || num_samples <= 0)
    return;

  for (int i = 0; i < num_samples; i++) {
    buffer[i] = soft_clip(buffer[i], threshold, steepness);
  }
}

/* ============================================================================
 * Buffer Utility Functions
 * ============================================================================
 */

float smoothstep(float t) {
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

int16_t float_to_int16(float sample) {
  // Clamp to [-1, 1] then scale to int16 range
  if (sample > 1.0f)
    sample = 1.0f;
  if (sample < -1.0f)
    sample = -1.0f;
  return (int16_t)(sample * 32767.0f);
}

float int16_to_float(int16_t sample) {
  return (float)sample / 32768.0f;
}

void buffer_float_to_int16(const float *src, int16_t *dst, int count) {
  if (!src || !dst || count <= 0)
    return;
  for (int i = 0; i < count; i++) {
    dst[i] = float_to_int16(src[i]);
  }
}

void buffer_int16_to_float(const int16_t *src, float *dst, int count) {
  if (!src || !dst || count <= 0)
    return;
  for (int i = 0; i < count; i++) {
    dst[i] = int16_to_float(src[i]);
  }
}

float buffer_peak(const float *buffer, int count) {
  if (!buffer || count <= 0)
    return 0.0f;

  float peak = 0.0f;
  for (int i = 0; i < count; i++) {
    float abs_sample = fabsf(buffer[i]);
    if (abs_sample > peak)
      peak = abs_sample;
  }
  return peak;
}

void apply_gain_buffer(float *buffer, int count, float gain) {
  if (!buffer || count <= 0)
    return;

  for (int i = 0; i < count; i++) {
    buffer[i] *= gain;
  }
}

void fade_buffer(float *buffer, int count, float start_gain, float end_gain) {
  if (!buffer || count <= 0)
    return;

  float step = (end_gain - start_gain) / (float)count;
  float gain = start_gain;
  for (int i = 0; i < count; i++) {
    buffer[i] *= gain;
    gain += step;
  }
}

void fade_buffer_smooth(float *buffer, int count, bool fade_in) {
  if (!buffer || count <= 0)
    return;

  for (int i = 0; i < count; i++) {
    float t = (float)i / (float)(count - 1);
    float gain = smoothstep(fade_in ? t : (1.0f - t));
    buffer[i] *= gain;
  }
}

void copy_buffer_with_gain(const float *src, float *dst, int count, float gain) {
  if (!src || !dst || count <= 0)
    return;

  for (int i = 0; i < count; i++) {
    dst[i] = src[i] * gain;
  }
}
