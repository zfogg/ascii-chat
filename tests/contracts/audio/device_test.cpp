#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/logging.h>
#include <libplatform/thread.h>
#include <ascii-chat/util/time.h>
#include <portaudio.h>
#include <string.h>

// Only the device boundary is simulated. Stream selection, callbacks, sample
// queues, resampling, processing worker, and teardown are production code.
typedef struct {
  PaStreamCallback *callback;
  void *data;
  bool input, output, closed;
  double rate;
} device_stream_t;
static device_stream_t streams[16];
static unsigned opens, starts, closes, stops, initialized, terminated;
static unsigned fail_open, fail_start;
static unsigned minimum_input_channels;
static bool fail_duplex, fail_all, no_input, no_output;
static PaDeviceInfo input_info, output_info;
static audio_context_t context;

// Complete the PortAudio boundary so static linkers never pull in real devices.
PaDeviceIndex Pa_GetDeviceCount(void) {
  return 2;
}
const char *Pa_GetErrorText(PaError error) {
  return error == paNoError ? "Success" : "Simulated audio device failure";
}
PaError Pa_Initialize(void) {
  initialized++;
  return paNoError;
}
PaError Pa_Terminate(void) {
  terminated++;
  return paNoError;
}
PaDeviceIndex Pa_GetDefaultInputDevice(void) {
  return no_input ? paNoDevice : 0;
}
PaDeviceIndex Pa_GetDefaultOutputDevice(void) {
  return no_output ? paNoDevice : 1;
}
const PaDeviceInfo *Pa_GetDeviceInfo(PaDeviceIndex device) {
  return device == 0 ? &input_info : device == 1 ? &output_info : NULL;
}
PaError Pa_OpenStream(PaStream **stream, const PaStreamParameters *input, const PaStreamParameters *output, double rate, unsigned long frames, PaStreamFlags flags, PaStreamCallback *callback,
                      void *data) {
  (void)frames;
  (void)flags;
  REQUIRE((opens) < (16));
  device_stream_t *entry = &streams[opens++];
  *entry = (device_stream_t){.callback = callback, .data = data, .input = input != NULL, .output = output != NULL, .rate = rate};
  if (input) {
    REQUIRE((input->sampleFormat) == (paFloat32));
    REQUIRE((input->channelCount) >= (1));
    REQUIRE((input->channelCount) <= (input_info.maxInputChannels));
  }
  if (output) {
    REQUIRE((output->sampleFormat) == (paFloat32));
    REQUIRE((output->channelCount) >= (1));
    REQUIRE((output->channelCount) <= (output_info.maxOutputChannels));
  }
  // Some drivers return a partial stream even when opening fails.
  *stream = (PaStream *)entry;
  if (input && (unsigned)input->channelCount < minimum_input_channels)
    return paInvalidChannelCount;
  return fail_all || opens == fail_open || (fail_duplex && input && output) ? paUnanticipatedHostError : paNoError;
}
PaError Pa_StartStream(PaStream *stream) {
  REQUIRE((stream) != nullptr);
  starts++;
  return starts == fail_start ? paUnanticipatedHostError : paNoError;
}
PaError Pa_StopStream(PaStream *stream) {
  REQUIRE((stream) != nullptr);
  stops++;
  return paNoError;
}
PaError Pa_CloseStream(PaStream *stream) {
  device_stream_t *entry = (device_stream_t *)stream;
  REQUIRE((entry) != nullptr);
  REQUIRE(!(entry->closed));
  entry->closed = true;
  closes++;
  return paNoError;
}

static void setup(void) {
  memset(streams, 0, sizeof(streams));
  opens = starts = closes = stops = initialized = terminated = 0;
  fail_open = fail_start = minimum_input_channels = 0;
  fail_duplex = fail_all = no_input = no_output = false;
  memset(&context, 0, sizeof(context));
  log_init(NULL, LOG_FATAL, false, false);
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  options_t options = *options_get();
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_MIC;
  options.microphone_channels = 2;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  input_info = (PaDeviceInfo){.structVersion = 2, .name = "contract microphone", .maxInputChannels = 2, .defaultSampleRate = AUDIO_SAMPLE_RATE};
  output_info = (PaDeviceInfo){.structVersion = 2, .name = "contract speaker", .maxOutputChannels = 2, .defaultSampleRate = AUDIO_SAMPLE_RATE};
  REQUIRE((audio_init(&context)) == (ASCIICHAT_OK));
}
static void teardown(void) {
  audio_destroy(&context);
  audio_terminate_portaudio_final();
  REQUIRE((terminated) == (initialized));
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}

struct AudioDeviceFixture {
  AudioDeviceFixture() {
    setup();
  }
  ~AudioDeviceFixture() {
    teardown();
  }
};
static void pause_processing(void) {
  REQUIRE(context.worker_running);
  atomic_store_bool(&context.worker_should_stop, true);
  cond_signal(&context.audio_processing_wakeup);
  REQUIRE((thread_join(&context.worker_thread, NULL)) == (0));
  context.worker_running = false;
}
static int invoke(device_stream_t *stream, const float *input, float *output, unsigned long count, PaStreamCallbackFlags flags) {
  PaStreamCallbackTimeInfo time = {0};
  return stream->callback(input, output, count, &time, flags, stream->data);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/duplex_callback_preserves_capture_scales_playback_and_silences_underrun_and_shutdown", "[audio_device]") {
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE(context.running && context.duplex_stream && !context.separate_streams);
  REQUIRE((opens) == (1));
  REQUIRE((starts) == (1));
  REQUIRE((initialized) == (1));
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE((opens) == (1));
  pause_processing();
  context.playback_buffer->jitter_buffer_enabled = false;
  float input[32 * AUDIO_CHANNELS], playback[16 * AUDIO_CHANNELS], output[32 * AUDIO_CHANNELS], captured[32 * AUDIO_CHANNELS];
  for (unsigned i = 0; i < 32 * AUDIO_CHANNELS; ++i)
    input[i] = (float)i / 64;
  for (unsigned i = 0; i < 16 * AUDIO_CHANNELS; ++i)
    playback[i] = i & 1 ? -0.5f : 0.25f;
  REQUIRE((options_set_double("speakers_volume", 0.5)) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_write(context.playback_buffer, playback, 16 * AUDIO_CHANNELS)) == (ASCIICHAT_OK));
  REQUIRE((invoke(&streams[0], input, output, 32, paOutputUnderflow | paInputOverflow)) == (paContinue));
  for (unsigned i = 0; i < 32 * AUDIO_CHANNELS; ++i)
    REQUIRE((output[i]) == Catch::Approx(i < 16 * AUDIO_CHANNELS ? playback[i] * 0.5f : 0));
  REQUIRE((audio_ring_buffer_read(context.raw_capture_rb, captured, 32 * AUDIO_CHANNELS)) == (32 * AUDIO_CHANNELS));
  REQUIRE(std::memcmp((captured), (input), (sizeof(input))) == 0);
  REQUIRE((audio_ring_buffer_read(context.raw_render_rb, captured, 32 * AUDIO_CHANNELS)) == (32 * AUDIO_CHANNELS));
  REQUIRE(std::memcmp((captured), (output), (sizeof(output))) == 0);
  context.playback_only = true;
  REQUIRE((invoke(&streams[0], input, output, 32, 0)) == (paContinue));
  REQUIRE((audio_ring_buffer_available_read(context.raw_capture_rb)) == (0));
  REQUIRE((audio_ring_buffer_available_read(context.raw_render_rb)) == (0));
  atomic_store_bool(&context.shutting_down, true);
  for (unsigned i = 0; i < 32 * AUDIO_CHANNELS; ++i)
    output[i] = 1;
  REQUIRE((invoke(&streams[0], input, output, 32, 0)) == (paContinue));
  for (unsigned i = 0; i < 32 * AUDIO_CHANNELS; ++i)
    REQUIRE((output[i]) == (0));
  REQUIRE((audio_stop_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE(!(context.running));
  REQUIRE((context.duplex_stream) == nullptr);
  REQUIRE((stops) == (1));
  REQUIRE((closes) == (1));
  REQUIRE((audio_stop_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE((closes) == (1));
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/separate_native_rate_callbacks_convert_constant_samples_and_ignore_shutdown_capture", "[audio_device]") {
  input_info.defaultSampleRate = 24000;
  output_info.defaultSampleRate = 32000;
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE(context.separate_streams && context.input_stream && context.output_stream);
  REQUIRE((opens) == (2));
  REQUIRE((streams[0].rate) == (32000));
  REQUIRE((streams[1].rate) == (24000));
  pause_processing();
  context.playback_buffer->jitter_buffer_enabled = false;
  float samples[256 * AUDIO_CHANNELS], output[64 * AUDIO_CHANNELS], captured[256 * AUDIO_CHANNELS];
  for (unsigned i = 0; i < 256 * AUDIO_CHANNELS; ++i)
    samples[i] = 0.25f;
  REQUIRE((options_set_double("speakers_volume", 1.0)) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_write(context.playback_buffer, samples, 256 * AUDIO_CHANNELS)) == (ASCIICHAT_OK));
  REQUIRE((invoke(&streams[0], NULL, output, 64, paOutputUnderflow)) == (paContinue));
  for (unsigned i = 0; i < 64 * AUDIO_CHANNELS; ++i)
    REQUIRE((output[i]) == Catch::Approx(0.25f));
  REQUIRE((invoke(&streams[1], samples, NULL, 64, paInputOverflow)) == (paContinue));
  size_t count = audio_ring_buffer_read(context.raw_capture_rb, captured, 256 * AUDIO_CHANNELS);
  REQUIRE((count) >= (126 * AUDIO_CHANNELS));
  REQUIRE((count) <= (128 * AUDIO_CHANNELS));
  for (size_t i = 0; i < count; ++i)
    REQUIRE((captured[i]) == Catch::Approx(0.25f));
  atomic_store_bool(&context.shutting_down, true);
  REQUIRE((invoke(&streams[1], samples, NULL, 64, 0)) == (paContinue));
  REQUIRE((audio_ring_buffer_available_read(context.raw_capture_rb)) == (0));
  REQUIRE((invoke(&streams[0], NULL, output, 64, 0)) == (paContinue));
  for (unsigned i = 0; i < 64 * AUDIO_CHANNELS; ++i)
    REQUIRE((output[i]) == (0));
  REQUIRE((audio_stop_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE((closes) == (2));
  REQUIRE((stops) == (2));
  REQUIRE((context.render_buffer) == nullptr);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/duplex_open_failure_falls_back_and_retires_all_partial_streams", "[audio_device]") {
  fail_duplex = true;
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE(context.separate_streams && context.input_stream && context.output_stream);
  REQUIRE((opens) == (3));
  REQUIRE(streams[0].closed);
  REQUIRE((audio_stop_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE((closes) == (opens));
  for (unsigned i = 0; i < opens; ++i)
    REQUIRE(streams[i].closed);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/channel_retry_closes_partial_stream_without_unnecessary_rate_change", "[audio_device]") {
  context.playback_only = true;
  fail_open = 1;
  output_info.defaultSampleRate = 44100;
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE((opens) == (2));
  REQUIRE(streams[0].closed);
  REQUIRE((context.output_device_rate) == (44100));
  REQUIRE((context.output_device_channels) == (1));
  REQUIRE((audio_stop_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE((closes) == (2));
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/start_failure_releases_both_streams_and_allows_clean_retry", "[audio_device]") {
  input_info.defaultSampleRate = 24000;
  fail_start = 2;
  REQUIRE((audio_start_duplex(&context)) == (ERROR_AUDIO));
  REQUIRE((context.input_stream) == nullptr);
  REQUIRE((context.output_stream) == nullptr);
  REQUIRE((context.render_buffer) == nullptr);
  REQUIRE(!(context.running || context.worker_running));
  REQUIRE((closes) == (2));
  fail_start = 0;
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE(context.worker_running && context.running);
  REQUIRE((audio_stop_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE((closes) == (opens));
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/absent_microphone_and_failed_devices_report_audio_error_without_starting_worker", "[audio_device]") {
  no_input = true;
  REQUIRE((audio_start_duplex(&context)) == (ERROR_AUDIO));
  REQUIRE(!(context.worker_running));
  REQUIRE((opens) == (0));
  no_input = false;
  fail_all = true;
  REQUIRE((audio_start_duplex(&context)) == (ERROR_AUDIO));
  REQUIRE(!(context.running || context.worker_running));
  REQUIRE((context.input_stream) == nullptr);
  REQUIRE((context.output_stream) == nullptr);
  REQUIRE((context.render_buffer) == nullptr);
  REQUIRE((closes) == (opens));
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/real_worker_publishes_microphone_samples_with_configured_sensitivity", "[audio_device]") {
  REQUIRE((options_set_double("microphone_sensitivity", 0.25)) == (ASCIICHAT_OK));
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  float input[480 * AUDIO_CHANNELS], output[480 * AUDIO_CHANNELS], captured[480 * AUDIO_CHANNELS];
  for (unsigned i = 0; i < 480 * AUDIO_CHANNELS; ++i)
    input[i] = i & 1 ? -0.5f : 0.25f;
  REQUIRE((invoke(&streams[0], input, output, 480, 0)) == (paContinue));
  uint64_t deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (audio_ring_buffer_available_read(context.capture_buffer) < 480 * AUDIO_CHANNELS && time_get_ns() < deadline)
    platform_sleep_ns(NS_PER_MS_INT);
  REQUIRE((audio_ring_buffer_read(context.capture_buffer, captured, 480 * AUDIO_CHANNELS)) == (480 * AUDIO_CHANNELS));
  for (unsigned i = 0; i < 480 * AUDIO_CHANNELS; ++i)
    REQUIRE((captured[i]) == Catch::Approx(input[i] * 0.25f));
  REQUIRE((audio_stop_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE(!(context.worker_running || context.running));
  REQUIRE((closes) == (opens));
}

// Signal counters are debug instrumentation; the sample-delivery tests run in every build.
#ifndef NDEBUG
TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/capture_callbacks_notify_once_per_twenty_millisecond_batch", "[audio_device]") {
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  pause_processing();
  float input[480 * AUDIO_CHANNELS] = {0}, output[480 * AUDIO_CHANNELS];
  uint64_t signals = context.audio_processing_wakeup.signal_count;
  REQUIRE((invoke(&streams[0], input, output, 480, 0)) == (paContinue));
  REQUIRE((context.audio_processing_wakeup.signal_count) == (signals));
  REQUIRE((invoke(&streams[0], input, output, 480, 0)) == (paContinue));
  REQUIRE((context.audio_processing_wakeup.signal_count) == (signals + 1));
  REQUIRE((audio_ring_buffer_available_read(context.raw_capture_rb)) == (960 * AUDIO_CHANNELS));
}
#endif

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/real_worker_preserves_both_ten_millisecond_capture_blocks", "[audio_device]") {
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  float input[960 * AUDIO_CHANNELS], output[480 * AUDIO_CHANNELS], captured[960 * AUDIO_CHANNELS];
  for (unsigned i = 0; i < 960 * AUDIO_CHANNELS; ++i)
    input[i] = (i & 1) ? -0.25f : 0.5f;
  REQUIRE((invoke(&streams[0], input, output, 480, 0)) == (paContinue));
  REQUIRE((invoke(&streams[0], input + 480 * AUDIO_CHANNELS, output, 480, 0)) == (paContinue));
  uint64_t deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (audio_ring_buffer_available_read(context.capture_buffer) < 960 * AUDIO_CHANNELS && time_get_ns() < deadline)
    platform_sleep_ns(NS_PER_MS_INT);
  REQUIRE((audio_ring_buffer_read(context.capture_buffer, captured, 960 * AUDIO_CHANNELS)) == (960 * AUDIO_CHANNELS));
  for (unsigned i = 0; i < 960 * AUDIO_CHANNELS; ++i)
    REQUIRE((captured[i]) == Catch::Approx(input[i]));
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/stereo_only_microphone_accepts_mono_speech_with_explicit_capture_fallback", "[audio_device]") {
  options_t options = *options_get();
  options.microphone_channels = 1;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  minimum_input_channels = 2;
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE((context.input_device_channels) == (2));
  pause_processing();
  float input[960], captured[960];
  for (size_t i = 0; i < 480; i++) {
    input[i * 2] = 0.25f;
    input[i * 2 + 1] = -0.75f;
  }
  REQUIRE((invoke((device_stream_t *)context.input_stream, input, NULL, 480, 0)) == (paContinue));
  REQUIRE((audio_ring_buffer_read(context.raw_capture_rb, captured, 960)) == (960));
  for (size_t i = 0; i < 960; i++)
    REQUIRE((captured[i]) == (0.25f));
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/mono_speakers_receive_a_centered_downmix_of_stereo_playback", "[audio_device]") {
  context.playback_only = true;
  output_info.maxOutputChannels = 1;
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  pause_processing();
  context.playback_buffer->jitter_buffer_enabled = false;
  REQUIRE((options_set_double("speakers_volume", 1)) == (ASCIICHAT_OK));
  float input[960], output[480];
  for (size_t i = 0; i < 480; i++) {
    input[i * 2] = 0.25f;
    input[i * 2 + 1] = -0.75f;
  }
  REQUIRE((audio_ring_buffer_write(context.playback_buffer, input, 960)) == (ASCIICHAT_OK));
  REQUIRE((invoke((device_stream_t *)context.output_stream, NULL, output, 480, 0)) == (paContinue));
  for (size_t i = 0; i < 480; i++)
    REQUIRE((output[i]) == (-0.25f));
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/four_channel_interface_routes_only_the_selected_stereo_pair", "[audio_device]") {
  input_info.maxInputChannels = 4;
  options_t options = *options_get();
  options.microphone_channel = 2;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE((context.input_device_channels) == (4));
  pause_processing();
  float input[1920], captured[960];
  for (size_t i = 0; i < 480; i++) {
    input[i * 4] = 0.9f;
    input[i * 4 + 1] = 0.8f;
    input[i * 4 + 2] = 0.25f;
    input[i * 4 + 3] = -0.5f;
  }
  REQUIRE((invoke((device_stream_t *)context.input_stream, input, NULL, 480, 0)) == (paContinue));
  REQUIRE((audio_ring_buffer_read(context.raw_capture_rb, captured, 960)) == (960));
  for (size_t i = 0; i < 480; i++) {
    REQUIRE((captured[i * 2]) == (0.25f));
    REQUIRE((captured[i * 2 + 1]) == (-0.5f));
  }
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/invalid_interface_pair_fails_before_opening_any_stream", "[audio_device]") {
  options_t options = *options_get();
  options.microphone_channel = 1;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  REQUIRE((audio_start_duplex(&context)) == (ERROR_INVALID_PARAM));
  REQUIRE((opens) == (0));
  REQUIRE(!(context.worker_running));
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/remote_only_capture_without_playback_needs_no_portaudio_device", "[audio_device]") {
  options_t options = *options_get();
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_REMOTE;
  options.audio_no_playback = true;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  fail_all = true;
  REQUIRE((audio_start_duplex(&context)) == (ASCIICHAT_OK));
  REQUIRE(context.running && context.worker_running);
  REQUIRE((initialized) == (0));
  REQUIRE((opens) == (0));
  REQUIRE((context.input_stream) == nullptr);
  REQUIRE((context.output_stream) == nullptr);
  REQUIRE((context.duplex_stream) == nullptr);
}
