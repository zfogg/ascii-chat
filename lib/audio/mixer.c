#include <ascii-chat/buffer.h>
/**
 * @file mixer.c
 * @ingroup audio
 * @brief 🎚️ Real-time audio mixer with ducking, gain control, and multi-stream blending
 */

#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/spectral.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h> // For asciichat_errno system
#include <ascii-chat/debug/named.h>
#include <ascii-chat/util/time.h>     // For timing instrumentation
#include <ascii-chat/util/bits.h>     // For find_first_set_bit
#include <ascii-chat/util/overflow.h> // For overflow checking
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <ascii-chat/util/fnv1a.h>
#include <ascii-chat/options/options.h>

#define MIXER_LISTENER_SAMPLES 8192
typedef struct mixer_listener_buffer {
  float samples[MIXER_LISTENER_SAMPLES];
  size_t read;
  size_t count;
} mixer_listener_buffer_t;

// Called with the source write lock. Drain each input once and fan it out to
// independent listener queues so one recipient cannot consume another's audio.
static void mixer_distribute_samples(mixer_t *mixer) {
  float samples[960];
  for (int source = 0; source < mixer->max_sources; source++) {
    if (!mixer->source_ids[source] || !mixer->source_buffers[source])
      continue;
    while (audio_ring_buffer_available_read(mixer->source_buffers[source]) > 0) {
      size_t count = audio_ring_buffer_read(mixer->source_buffers[source], samples, 960);
      if (!count)
        break;
      for (int listener = 0; listener < mixer->max_sources; listener++) {
        if (listener == source || !mixer->source_ids[listener])
          continue;
        mixer_listener_buffer_t *queue = mixer->listener_buffers[listener][source];
        if (!queue)
          continue;
        if (queue->count + count > MIXER_LISTENER_SAMPLES) {
          size_t discard = queue->count + count - MIXER_LISTENER_SAMPLES;
          queue->read = (queue->read + discard) % MIXER_LISTENER_SAMPLES;
          queue->count -= discard;
        }
        for (size_t i = 0; i < count; i++)
          queue->samples[(queue->read + queue->count + i) % MIXER_LISTENER_SAMPLES] = samples[i];
        queue->count += count;
      }
    }
  }
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Ducking implementation
int ducking_init(ducking_t *duck, int num_sources, float sample_rate) {
  if (!duck) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "duck is NULL");
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

  memset(mixer->spectral, 0, sizeof(mixer->spectral));
  mixer->num_sources = 0;
  mixer->max_sources = max_sources;
  mixer->sample_rate = sample_rate;
  memset(mixer->listener_buffers, 0, sizeof(mixer->listener_buffers));

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
  if (checked_size_mul((size_t)max_sources, sizeof(const char *), &ids_size) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Mixer source IDs array overflow: %d sources", max_sources);
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer);
    return NULL;
  }
  mixer->source_ids = SAFE_MALLOC(ids_size, const char **);
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
  memset(mixer->active_sources_mask, 0, sizeof(mixer->active_sources_mask)); // No sources active initially
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
  mixer->mix_buffer = buffer_alloc(NULL, mix_buffer_size);
  if (!mixer->mix_buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate mix buffer");
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer->source_active);
    SAFE_FREE(mixer);
    return NULL;
  }

  // OPTIMIZATION 2: Initialize reader-writer lock
  if (rwlock_init(&mixer->source_lock, "audio_mixer_sources") != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize mixer source lock");
    SAFE_FREE(mixer->source_buffers);
    SAFE_FREE(mixer->source_ids);
    SAFE_FREE(mixer->source_active);
    buffer_release(mixer->mix_buffer);
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
    buffer_release(mixer->mix_buffer);
    SAFE_FREE(mixer);
    return NULL;
  }
  compressor_init(&mixer->compressor, (float)sample_rate);
  if (GET_OPTION(audio_spectral_mixer)) {
    spectral_config_t config = spectral_default_config(sample_rate);
    config.fft_size = GET_OPTION(audio_fft_size);
    config.noise_gate = GET_OPTION(audio_spectral_gate);
    config.compressor = GET_OPTION(audio_multiband);
    config.adaptive_eq = GET_OPTION(audio_adaptive_eq);
    config.auto_gain = GET_OPTION(audio_spectral_agc);
    for (int i = 0; i <= max_sources; ++i) {
      if (spectral_create(&config, &mixer->spectral[i]) != ASCIICHAT_OK) {
        mixer_destroy(mixer);
        return NULL;
      }
    }
  }

  log_debug("Audio mixer created: max_sources=%d, sample_rate=%d", max_sources, sample_rate);

  // Register mixer and its sync primitives for debugging
  NAMED_REGISTER_MIXER(mixer, "audio_mixer", NULL);
  NAMED_REGISTER_RWLOCK(&mixer->source_lock, "source_lock", (uintptr_t)(const void *)(mixer));

  return mixer;
}

void mixer_destroy(mixer_t *mixer) {
  if (!mixer)
    return;

  NAMED_UNREGISTER(mixer);
  for (int i = 0; i <= mixer->max_sources; ++i)
    spectral_destroy(mixer->spectral[i]);

  // OPTIMIZATION 2: Destroy reader-writer lock
  rwlock_destroy(&mixer->source_lock);

  ducking_destroy(&mixer->ducking);

  // Free all allocated client_id strings before freeing the array itself
  for (int i = 0; i < mixer->max_sources; i++) {
    for (int j = 0; j < mixer->max_sources; j++)
      SAFE_FREE(mixer->listener_buffers[i][j]);
    if (mixer->source_ids[i] != NULL) {
      SAFE_FREE(mixer->source_ids[i]);
    }
  }

  SAFE_FREE(mixer->source_buffers);
  SAFE_FREE(mixer->source_ids);
  SAFE_FREE(mixer->source_active);
  buffer_release(mixer->mix_buffer);
  SAFE_FREE(mixer);
  log_debug("Audio mixer destroyed");
}

int mixer_add_source(mixer_t *mixer, const char *client_id, audio_ring_buffer_t *buffer) {
  if (!mixer || !buffer)
    return -1;

  // OPTIMIZATION 2: Acquire write lock for source modification
  rwlock_wrlock(&mixer->source_lock);

  // Find an empty slot
  int slot = -1;
  for (int i = 0; i < mixer->max_sources; i++) {
    if (mixer->source_ids[i] == NULL) {
      slot = i;
      break;
    }
  }

  if (slot == -1) {
    rwlock_wrunlock(&mixer->source_lock);
    log_warn("Mixer: No available slots for client %s", client_id);
    return -1;
  }

  mixer->source_buffers[slot] = buffer;
  // Duplicate the client_id string - we own this memory now
  size_t client_id_len = strlen(client_id) + 1;
  mixer->source_ids[slot] = SAFE_MALLOC(client_id_len, char *);
  if (!mixer->source_ids[slot]) {
    log_warn("Mixer: Failed to allocate memory for client_id %s", client_id);
    mixer->source_buffers[slot] = NULL;
    rwlock_wrunlock(&mixer->source_lock);
    return -1;
  }
  asciichat_error_t strcpy_result = SAFE_STRCPY((char *)mixer->source_ids[slot], client_id_len, client_id);
  if (strcpy_result != ASCIICHAT_OK) {
    log_error("Mixer: Failed to copy client_id: %s", asciichat_error_string(strcpy_result));
    SAFE_FREE(mixer->source_ids[slot]);
    mixer->source_buffers[slot] = NULL;
    rwlock_wrunlock(&mixer->source_lock);
    return -1;
  }
  for (int other = 0; other < mixer->max_sources; other++) {
    if (other == slot || !mixer->source_ids[other])
      continue;
    mixer->listener_buffers[slot][other] = SAFE_CALLOC(1, sizeof(mixer_listener_buffer_t), void *);
    mixer->listener_buffers[other][slot] = SAFE_CALLOC(1, sizeof(mixer_listener_buffer_t), void *);
  }
  mixer->source_active[slot] = true;
  mixer->num_sources++;

  // OPTIMIZATION 1: Update bitset optimization structures
  mixer->active_sources_mask[slot / 64] |= (1ULL << (slot % 64)); // Set bit for this slot
  // Hash table optimization disabled for string-based client_ids (linear search is acceptable for small mixer sizes)

  rwlock_wrunlock(&mixer->source_lock);

  log_info("Mixer: Added source for client %s at slot %d", client_id, slot);
  return slot;
}

void mixer_remove_source(mixer_t *mixer, const char *client_id) {
  if (!mixer)
    return;

  // OPTIMIZATION 2: Acquire write lock for source modification
  rwlock_wrlock(&mixer->source_lock);

  for (int i = 0; i < mixer->max_sources; i++) {
    if (mixer->source_ids[i] != NULL && strcmp(mixer->source_ids[i], client_id) == 0) {
      for (int j = 0; j < mixer->max_sources; j++) {
        SAFE_FREE(mixer->listener_buffers[i][j]);
        if (j != i)
          SAFE_FREE(mixer->listener_buffers[j][i]);
      }
      spectral_reset(mixer->spectral[i]);
      mixer->source_buffers[i] = NULL;
      // Free the allocated client_id string
      SAFE_FREE(mixer->source_ids[i]);
      mixer->source_active[i] = false;
      mixer->num_sources--;

      // OPTIMIZATION 1: Update bitset optimization structures
      mixer->active_sources_mask[i / 64] &= ~(1ULL << (i % 64)); // Clear bit for this slot
      // Hash table optimization disabled for string-based client_ids

      // Reset ducking state for this source
      mixer->ducking.envelope[i] = 0.0f;
      mixer->ducking.gain[i] = 1.0f;

      rwlock_wrunlock(&mixer->source_lock);

      log_info("Mixer: Removed source for client %s from slot %d", client_id, i);
      return;
    }
  }

  rwlock_wrunlock(&mixer->source_lock);
}

void mixer_set_source_active(mixer_t *mixer, const char *client_id, bool active) {
  if (!mixer)
    return;

  // OPTIMIZATION 2: Acquire write lock for source modification
  rwlock_wrlock(&mixer->source_lock);

  for (int i = 0; i < mixer->max_sources; i++) {
    if (mixer->source_ids[i] != NULL && strcmp(mixer->source_ids[i], client_id) == 0) {
      mixer->source_active[i] = active;

      // OPTIMIZATION 1: Update bitset for active state change
      if (active) {
        mixer->active_sources_mask[i / 64] |= (1ULL << (i % 64)); // Set bit
      } else {
        mixer->active_sources_mask[i / 64] &= ~(1ULL << (i % 64)); // Clear bit
      }

      rwlock_wrunlock(&mixer->source_lock);
      log_debug("Mixer: Set source %s active=%d", client_id, active);
      return;
    }
  }

  rwlock_wrunlock(&mixer->source_lock);
}

int mixer_process(mixer_t *mixer, float *output, int num_samples) {
  if (!mixer || !output || num_samples <= 0)
    return -1;

  // Serialize source access and mutable compressor/spectral state.
  rwlock_wrlock(&mixer->source_lock);

  // Clear output buffer
  SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));

  // Count active sources
  int active_count = 0;
  for (int i = 0; i < mixer->max_sources; i++) {
    if (mixer->source_ids[i] != 0 && mixer->source_active[i] && mixer->source_buffers[i]) {
      active_count++;
    }
  }

  if (active_count == 0 && !mixer->spectral[mixer->max_sources]) {
    rwlock_wrunlock(&mixer->source_lock);
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

  if (mixer->spectral[mixer->max_sources])
    spectral_process(mixer->spectral[mixer->max_sources], output, output, (size_t)num_samples);
  rwlock_wrunlock(&mixer->source_lock);
  return num_samples;
}

int mixer_process_excluding_source(mixer_t *mixer, float *output, int num_samples, uint32_t exclude_client_id) {
  if (!mixer || !output || num_samples <= 0) {
    return -1;
  }

  // Only use timing in debug builds - snprintf + hashtable ops are expensive in hot path
#ifndef NDEBUG
  START_TIMER("mixer_total");
#endif

  // Serialize sample distribution and mutable ducking/compressor state.
  rwlock_wrlock(&mixer->source_lock);

  // Clear output buffer
  SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));

  // Source IDs are strings; match the complete ID hash used by the caller.
  uint8_t exclude_index = MIXER_HASH_INVALID;
  for (int i = 0; i < mixer->max_sources; i++) {
    if (mixer->source_ids[i] && fnv1a_hash_string(mixer->source_ids[i]) == exclude_client_id) {
      exclude_index = (uint8_t)i;
      break;
    }
  }
  if (exclude_index != MIXER_HASH_INVALID)
    mixer_distribute_samples(mixer);
  uint64_t active_mask[MIXER_MASK_WORDS];
  memcpy(active_mask, mixer->active_sources_mask, sizeof(active_mask));
  bool valid_exclude = exclude_index < mixer->max_sources && exclude_index != MIXER_HASH_INVALID;
  if (valid_exclude)
    active_mask[exclude_index / 64] &= ~(1ULL << (exclude_index % 64));
  bool any_active = false;
  for (size_t word = 0; word < MIXER_MASK_WORDS; ++word)
    any_active |= active_mask[word] != 0;

  // Fast check: any sources to mix?
  int spectral_slot = valid_exclude ? exclude_index : mixer->max_sources;
  if (!any_active && !mixer->spectral[spectral_slot]) {
    rwlock_wrunlock(&mixer->source_lock);
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
    size_t word = 0;
    uint64_t current_mask = active_mask[0];
    while (source_count < MIXER_MAX_SOURCES) {
      while (!current_mask && word + 1 < MIXER_MASK_WORDS)
        current_mask = active_mask[++word];
      if (!current_mask)
        break;
      int i = (int)(word * 64) + find_first_set_bit(current_mask);
      current_mask &= current_mask - 1;

      // Verify source is valid (defensive programming)
      if (i < mixer->max_sources && mixer->source_ids[i] != 0 && mixer->source_buffers[i]) {
        // Read samples from this source's ring buffer
        size_t samples_read_size = 0;
        if (valid_exclude) {
          mixer_listener_buffer_t *queue = mixer->listener_buffers[exclude_index][i];
          if (queue && queue->count >= (size_t)frame_size) {
            samples_read_size = (size_t)frame_size;
            for (int s = 0; s < frame_size; s++)
              source_samples[source_count][s] = queue->samples[(queue->read + s) % MIXER_LISTENER_SAMPLES];
            queue->read = (queue->read + samples_read_size) % MIXER_LISTENER_SAMPLES;
            queue->count -= samples_read_size;
          }
        } else {
          samples_read_size =
              audio_ring_buffer_read(mixer->source_buffers[i], source_samples[source_count], frame_size);
        }
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
            log_dev_every(NS_PER_MS_INT, "MIXER SOURCE READ: client_id=%u, slot=%d, samples_read=%d, RMS=%.6f",
                          mixer->source_ids[i], i, samples_read, source_rms);
          }

          source_map[source_count] = i;
          source_count++;
        }
      }
    }

    uint64_t read_end_ns = time_get_ns();
    uint64_t read_time_ns = time_elapsed_ns(read_start_ns, read_end_ns);

    if (read_time_ns > 10 * NS_PER_MS_INT) {
      char read_time_str[32];
      time_pretty(read_time_ns, -1, read_time_str, sizeof(read_time_str));
      log_warn_every(LOG_RATE_DEFAULT, "Mixer: Slow source reading took %s for %d sources", read_time_str,
                     source_count);
    }

    if (GET_OPTION(no_audio_mixer)) {
      for (int sample = 0; sample < frame_size; sample++) {
        for (int source = 0; source < source_count; source++)
          output[frame_start + sample] += source_samples[source][sample];
      }
      continue;
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

  if (mixer->spectral[spectral_slot])
    spectral_process(mixer->spectral[spectral_slot], output, output, (size_t)num_samples);
  rwlock_wrunlock(&mixer->source_lock);

#ifndef NDEBUG
  STOP_TIMER_AND_LOG_EVERY(warn, NS_PER_SEC_INT, 2 * NS_PER_MS_INT, "mixer_total", "Mixer took");
#endif

  return num_samples;
}
