#include <ascii-chat/audio/recording.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/memory.h>
#include <math.h>
#include <string.h>
#include <stdatomic.h>

#define RECORDING_RATE 48000ULL
#define RECORDING_CAPACITY (RECORDING_RATE * 30)

/**
 * @brief The useful part of audio recording is in `mix`, `channels`, `epoch_ns`, `read_position`: together they describe what is pending, who owns it, and how much data is valid. Keep the object with its parent operation and use the parent cleanup path.
 */
struct audio_recording {
  float *mix;
  unsigned channels;
  uint64_t epoch_ns;
  uint64_t read_position;
  uint64_t source_position[AUDIO_RECORDING_SOURCE_COUNT];
  bool started;
  bool source_started[AUDIO_RECORDING_SOURCE_COUNT];
};

static static_mutex_t recording_mutex = STATIC_MUTEX_INIT;
static audio_recording_t *active_recording;
static _Atomic bool recording_accepts_samples;

asciichat_error_t audio_recording_create(audio_recording_t **out) {
  return audio_recording_create_channels(out, 1);
}

asciichat_error_t audio_recording_create_channels(audio_recording_t **out, unsigned channels) {
  if (channels != 1 && channels != 2)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid recording channel count");
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing recording output");
  *out = NULL;
  static_mutex_lock_named(&recording_mutex, "audio_recording");
  if (active_recording) {
    static_mutex_unlock(&recording_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "A session recording is already active");
  }
  audio_recording_t *recording = SAFE_CALLOC(1, sizeof(*recording), audio_recording_t *);
  recording->channels = channels;
  recording->mix = SAFE_CALLOC(RECORDING_CAPACITY * channels, sizeof(float), float *);
  active_recording = recording;
  *out = recording;
  static_mutex_unlock(&recording_mutex);
  return ASCIICHAT_OK;
}

void audio_recording_destroy(audio_recording_t *recording) {
  if (!recording)
    return;
  static_mutex_lock_named(&recording_mutex, "audio_recording");
  if (active_recording == recording) {
    atomic_store_explicit(&recording_accepts_samples, false, memory_order_release);
    active_recording = NULL;
  }
  SAFE_FREE(recording->mix);
  SAFE_FREE(recording);
  static_mutex_unlock(&recording_mutex);
}

void audio_recording_start(audio_recording_t *recording, uint64_t epoch_ns) {
  if (!recording)
    return;
  static_mutex_lock_named(&recording_mutex, "audio_recording");
  if (!recording->started) {
    recording->epoch_ns = epoch_ns;
    recording->started = true;
    atomic_store_explicit(&recording_accepts_samples, true, memory_order_release);
  }
  static_mutex_unlock(&recording_mutex);
}

void audio_recording_submit(audio_recording_source_t source, const float *samples, size_t count, uint64_t start_ns) {
  // This is only a hint: the locked pointer/state check protects destruction.
  if (!atomic_load_explicit(&recording_accepts_samples, memory_order_acquire) || !samples || !count || source < 0 ||
      source >= AUDIO_RECORDING_SOURCE_COUNT)
    return;
  static_mutex_lock_named(&recording_mutex, "audio_recording");
  audio_recording_t *recording = active_recording;
  if (!recording || !recording->started) {
    static_mutex_unlock(&recording_mutex);
    return;
  }
  uint64_t delta = start_ns > recording->epoch_ns ? start_ns - recording->epoch_ns : 0;
  uint64_t position = delta / 1000000000ULL * RECORDING_RATE + delta % 1000000000ULL * RECORDING_RATE / 1000000000ULL;
  position *= recording->channels;
  /* Smooth callback jitter while preserving gaps longer than 100 ms. */
  if (recording->source_started[source] &&
      position < recording->source_position[source] + RECORDING_RATE * recording->channels / 10)
    position = recording->source_position[source];
  if (position < recording->read_position) {
    position = recording->read_position;
    if (position % recording->channels) {
      uint64_t padding = recording->channels - position % recording->channels;
      if (position > UINT64_MAX - padding) {
        static_mutex_unlock(&recording_mutex);
        return;
      }
      position += padding;
    }
  }
  if (position > UINT64_MAX - (recording->channels - 1) || count > UINT64_MAX - position - (recording->channels - 1)) {
    static_mutex_unlock(&recording_mutex);
    return;
  }
  recording->source_started[source] = true;
  // Each submission starts at channel zero. An incomplete final frame leaves
  // its missing channels silent rather than shifting the next submission.
  recording->source_position[source] = position + count;
  if (recording->source_position[source] % recording->channels)
    recording->source_position[source] +=
        recording->channels - recording->source_position[source] % recording->channels;
  for (size_t i = 0; i < count; i++, position++) {
    if (position >= recording->read_position &&
        position - recording->read_position < RECORDING_CAPACITY * recording->channels && isfinite(samples[i]))
      recording->mix[position % (RECORDING_CAPACITY * recording->channels)] += samples[i];
  }
  static_mutex_unlock(&recording_mutex);
}

void audio_recording_read(audio_recording_t *recording, float *samples, size_t count) {
  if (!recording || !samples)
    return;
  static_mutex_lock_named(&recording_mutex, "audio_recording");
  for (size_t i = 0; i < count; i++) {
    size_t slot = recording->read_position++ % (RECORDING_CAPACITY * recording->channels);
    float sample = recording->mix[slot];
    samples[i] = fmaxf(-1.0f, fminf(1.0f, sample));
    recording->mix[slot] = 0;
  }
  static_mutex_unlock(&recording_mutex);
}
