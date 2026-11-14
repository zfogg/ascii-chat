#include <ascii-chat/tests/catch2_test_helpers.hpp>
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
PaError Pa_OpenStream(PaStream **stream, const PaStreamParameters *input, const PaStreamParameters *output, double rate,
                      unsigned long frames, PaStreamFlags flags, PaStreamCallback *callback, void *data) {
  (void)frames;
  (void)flags;
  ASCIICHAT_REQUIRE_LT(opens, 16);
  device_stream_t *entry = &streams[opens++];
  *entry = (device_stream_t){
      .callback = callback, .data = data, .input = input != NULL, .output = output != NULL, .rate = rate};
  if (input) {
    ASCIICHAT_REQUIRE_EQ(input->sampleFormat, paFloat32);
    ASCIICHAT_REQUIRE_GE(input->channelCount, 1);
    ASCIICHAT_REQUIRE_LE(input->channelCount, input_info.maxInputChannels);
  }
  if (output) {
    ASCIICHAT_REQUIRE_EQ(output->sampleFormat, paFloat32);
    ASCIICHAT_REQUIRE_GE(output->channelCount, 1);
    ASCIICHAT_REQUIRE_LE(output->channelCount, output_info.maxOutputChannels);
  }
  // Some drivers return a partial stream even when opening fails.
  *stream = (PaStream *)entry;
  if (input && (unsigned)input->channelCount < minimum_input_channels)
    return paInvalidChannelCount;
  return fail_all || opens == fail_open || (fail_duplex && input && output) ? paUnanticipatedHostError : paNoError;
}
PaError Pa_StartStream(PaStream *stream) {
  ASCIICHAT_REQUIRE_NOT_NULL(stream);
  starts++;
  return starts == fail_start ? paUnanticipatedHostError : paNoError;
}
PaError Pa_StopStream(PaStream *stream) {
  ASCIICHAT_REQUIRE_NOT_NULL(stream);
  stops++;
  return paNoError;
}
PaError Pa_CloseStream(PaStream *stream) {
  device_stream_t *entry = (device_stream_t *)stream;
  ASCIICHAT_REQUIRE_NOT_NULL(entry);
  ASCIICHAT_REQUIRE_NOT(entry->closed, "Stream closed twice");
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
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  options_t options = *options_get();
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_MIC;
  options.microphone_channels = 2;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  input_info = (PaDeviceInfo){
      .structVersion = 2, .name = "contract microphone", .maxInputChannels = 2, .defaultSampleRate = AUDIO_SAMPLE_RATE};
  output_info = (PaDeviceInfo){
      .structVersion = 2, .name = "contract speaker", .maxOutputChannels = 2, .defaultSampleRate = AUDIO_SAMPLE_RATE};
  ASCIICHAT_REQUIRE_EQ(audio_init(&context), ASCIICHAT_OK);
}
static void teardown(void) {
  audio_destroy(&context);
  audio_terminate_portaudio_final();
  ASCIICHAT_REQUIRE_EQ(terminated, initialized);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}

struct AudioDeviceFixture {
  AudioDeviceFixture() { setup(); }
  ~AudioDeviceFixture() { teardown(); }
};
static void pause_processing(void) {
  ASCIICHAT_REQUIRE(context.worker_running);
  atomic_store_bool(&context.worker_should_stop, true);
  cond_signal(&context.audio_processing_wakeup);
  ASCIICHAT_REQUIRE_EQ(thread_join(&context.worker_thread, NULL), 0);
  context.worker_running = false;
}
static int invoke(device_stream_t *stream, const float *input, float *output, unsigned long count,
                  PaStreamCallbackFlags flags) {
  PaStreamCallbackTimeInfo time = {0};
  return stream->callback(input, output, count, &time, flags, stream->data);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/duplex_callback_preserves_capture_scales_playback_and_silences_underrun_and_shutdown", "[audio_device]") {
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(context.running && context.duplex_stream && !context.separate_streams);
  ASCIICHAT_REQUIRE_EQ(opens, 1);
  ASCIICHAT_REQUIRE_EQ(starts, 1);
  ASCIICHAT_REQUIRE_EQ(initialized, 1);
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(opens, 1);
  pause_processing();
  context.playback_buffer->jitter_buffer_enabled = false;
  float input[32 * AUDIO_CHANNELS], playback[16 * AUDIO_CHANNELS], output[32 * AUDIO_CHANNELS],
      captured[32 * AUDIO_CHANNELS];
  for (unsigned i = 0; i < 32 * AUDIO_CHANNELS; ++i)
    input[i] = (float)i / 64;
  for (unsigned i = 0; i < 16 * AUDIO_CHANNELS; ++i)
    playback[i] = i & 1 ? -0.5f : 0.25f;
  ASCIICHAT_REQUIRE_EQ(options_set_double("speakers_volume", 0.5), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_write(context.playback_buffer, playback, 16 * AUDIO_CHANNELS), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[0], input, output, 32, paOutputUnderflow | paInputOverflow), paContinue);
  for (unsigned i = 0; i < 32 * AUDIO_CHANNELS; ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(output[i], i < 16 * AUDIO_CHANNELS ? playback[i] * 0.5f : 0, 1e-6);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(context.raw_capture_rb, captured, 32 * AUDIO_CHANNELS), 32 * AUDIO_CHANNELS);
  ASCIICHAT_REQUIRE_ARRAY_EQ(captured, input, sizeof(input));
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(context.raw_render_rb, captured, 32 * AUDIO_CHANNELS), 32 * AUDIO_CHANNELS);
  ASCIICHAT_REQUIRE_ARRAY_EQ(captured, output, sizeof(output));
  context.playback_only = true;
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[0], input, output, 32, 0), paContinue);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(context.raw_capture_rb), 0);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(context.raw_render_rb), 0);
  atomic_store_bool(&context.shutting_down, true);
  for (unsigned i = 0; i < 32 * AUDIO_CHANNELS; ++i)
    output[i] = 1;
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[0], input, output, 32, 0), paContinue);
  for (unsigned i = 0; i < 32 * AUDIO_CHANNELS; ++i)
    ASCIICHAT_REQUIRE_EQ(output[i], 0);
  ASCIICHAT_REQUIRE_EQ(audio_stop_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(context.running);
  ASCIICHAT_REQUIRE_NULL(context.duplex_stream);
  ASCIICHAT_REQUIRE_EQ(stops, 1);
  ASCIICHAT_REQUIRE_EQ(closes, 1);
  ASCIICHAT_REQUIRE_EQ(audio_stop_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(closes, 1);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/separate_native_rate_callbacks_convert_constant_samples_and_ignore_shutdown_capture", "[audio_device]") {
  input_info.defaultSampleRate = 24000;
  output_info.defaultSampleRate = 32000;
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(context.separate_streams && context.input_stream && context.output_stream);
  ASCIICHAT_REQUIRE_EQ(opens, 2);
  ASCIICHAT_REQUIRE_EQ(streams[0].rate, 32000);
  ASCIICHAT_REQUIRE_EQ(streams[1].rate, 24000);
  pause_processing();
  context.playback_buffer->jitter_buffer_enabled = false;
  float samples[256 * AUDIO_CHANNELS], output[64 * AUDIO_CHANNELS], captured[256 * AUDIO_CHANNELS];
  for (unsigned i = 0; i < 256 * AUDIO_CHANNELS; ++i)
    samples[i] = 0.25f;
  ASCIICHAT_REQUIRE_EQ(options_set_double("speakers_volume", 1.0), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_write(context.playback_buffer, samples, 256 * AUDIO_CHANNELS), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[0], NULL, output, 64, paOutputUnderflow), paContinue);
  for (unsigned i = 0; i < 64 * AUDIO_CHANNELS; ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(output[i], 0.25f, 1e-6);
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[1], samples, NULL, 64, paInputOverflow), paContinue);
  size_t count = audio_ring_buffer_read(context.raw_capture_rb, captured, 256 * AUDIO_CHANNELS);
  ASCIICHAT_REQUIRE_GE(count, 126 * AUDIO_CHANNELS);
  ASCIICHAT_REQUIRE_LE(count, 128 * AUDIO_CHANNELS);
  for (size_t i = 0; i < count; ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(captured[i], 0.25f, 1e-6);
  atomic_store_bool(&context.shutting_down, true);
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[1], samples, NULL, 64, 0), paContinue);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(context.raw_capture_rb), 0);
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[0], NULL, output, 64, 0), paContinue);
  for (unsigned i = 0; i < 64 * AUDIO_CHANNELS; ++i)
    ASCIICHAT_REQUIRE_EQ(output[i], 0);
  ASCIICHAT_REQUIRE_EQ(audio_stop_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(closes, 2);
  ASCIICHAT_REQUIRE_EQ(stops, 2);
  ASCIICHAT_REQUIRE_NULL(context.render_buffer);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/duplex_open_failure_falls_back_and_retires_all_partial_streams", "[audio_device]") {
  fail_duplex = true;
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(context.separate_streams && context.input_stream && context.output_stream);
  ASCIICHAT_REQUIRE_EQ(opens, 3);
  ASCIICHAT_REQUIRE(streams[0].closed);
  ASCIICHAT_REQUIRE_EQ(audio_stop_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(closes, opens);
  for (unsigned i = 0; i < opens; ++i)
    ASCIICHAT_REQUIRE(streams[i].closed);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/channel_retry_closes_partial_stream_without_unnecessary_rate_change", "[audio_device]") {
  context.playback_only = true;
  fail_open = 1;
  output_info.defaultSampleRate = 44100;
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(opens, 2);
  ASCIICHAT_REQUIRE(streams[0].closed);
  ASCIICHAT_REQUIRE_EQ(context.output_device_rate, 44100);
  ASCIICHAT_REQUIRE_EQ(context.output_device_channels, 1);
  ASCIICHAT_REQUIRE_EQ(audio_stop_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(closes, 2);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/start_failure_releases_both_streams_and_allows_clean_retry", "[audio_device]") {
  input_info.defaultSampleRate = 24000;
  fail_start = 2;
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ERROR_AUDIO);
  ASCIICHAT_REQUIRE_NULL(context.input_stream);
  ASCIICHAT_REQUIRE_NULL(context.output_stream);
  ASCIICHAT_REQUIRE_NULL(context.render_buffer);
  ASCIICHAT_REQUIRE_NOT(context.running || context.worker_running);
  ASCIICHAT_REQUIRE_EQ(closes, 2);
  fail_start = 0;
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(context.worker_running && context.running);
  ASCIICHAT_REQUIRE_EQ(audio_stop_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(closes, opens);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/absent_microphone_and_failed_devices_report_audio_error_without_starting_worker", "[audio_device]") {
  no_input = true;
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ERROR_AUDIO);
  ASCIICHAT_REQUIRE_NOT(context.worker_running);
  ASCIICHAT_REQUIRE_EQ(opens, 0);
  no_input = false;
  fail_all = true;
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ERROR_AUDIO);
  ASCIICHAT_REQUIRE_NOT(context.running || context.worker_running);
  ASCIICHAT_REQUIRE_NULL(context.input_stream);
  ASCIICHAT_REQUIRE_NULL(context.output_stream);
  ASCIICHAT_REQUIRE_NULL(context.render_buffer);
  ASCIICHAT_REQUIRE_EQ(closes, opens);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/real_worker_publishes_microphone_samples_with_configured_sensitivity", "[audio_device]") {
  ASCIICHAT_REQUIRE_EQ(options_set_double("microphone_sensitivity", 0.25), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  float input[480 * AUDIO_CHANNELS], output[480 * AUDIO_CHANNELS], captured[480 * AUDIO_CHANNELS];
  for (unsigned i = 0; i < 480 * AUDIO_CHANNELS; ++i)
    input[i] = i & 1 ? -0.5f : 0.25f;
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[0], input, output, 480, 0), paContinue);
  uint64_t deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (audio_ring_buffer_available_read(context.capture_buffer) < 480 * AUDIO_CHANNELS && time_get_ns() < deadline)
    platform_sleep_ns(NS_PER_MS_INT);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(context.capture_buffer, captured, 480 * AUDIO_CHANNELS), 480 * AUDIO_CHANNELS);
  for (unsigned i = 0; i < 480 * AUDIO_CHANNELS; ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(captured[i], input[i] * 0.25f, 1e-6);
  ASCIICHAT_REQUIRE_EQ(audio_stop_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(context.worker_running || context.running);
  ASCIICHAT_REQUIRE_EQ(closes, opens);
}

// Signal counters are debug instrumentation; the sample-delivery tests run in every build.
#ifndef NDEBUG
TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/capture_callbacks_notify_once_per_twenty_millisecond_batch", "[audio_device]") {
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  pause_processing();
  float input[480 * AUDIO_CHANNELS] = {0}, output[480 * AUDIO_CHANNELS];
  uint64_t signals = context.audio_processing_wakeup.signal_count;
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[0], input, output, 480, 0), paContinue);
  ASCIICHAT_REQUIRE_EQ(context.audio_processing_wakeup.signal_count, signals);
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[0], input, output, 480, 0), paContinue);
  ASCIICHAT_REQUIRE_EQ(context.audio_processing_wakeup.signal_count, signals + 1);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(context.raw_capture_rb), 960 * AUDIO_CHANNELS);
}
#endif

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/real_worker_preserves_both_ten_millisecond_capture_blocks", "[audio_device]") {
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  float input[960 * AUDIO_CHANNELS], output[480 * AUDIO_CHANNELS], captured[960 * AUDIO_CHANNELS];
  for (unsigned i = 0; i < 960 * AUDIO_CHANNELS; ++i)
    input[i] = (i & 1) ? -0.25f : 0.5f;
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[0], input, output, 480, 0), paContinue);
  ASCIICHAT_REQUIRE_EQ(invoke(&streams[0], input + 480 * AUDIO_CHANNELS, output, 480, 0), paContinue);
  uint64_t deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (audio_ring_buffer_available_read(context.capture_buffer) < 960 * AUDIO_CHANNELS && time_get_ns() < deadline)
    platform_sleep_ns(NS_PER_MS_INT);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(context.capture_buffer, captured, 960 * AUDIO_CHANNELS), 960 * AUDIO_CHANNELS);
  for (unsigned i = 0; i < 960 * AUDIO_CHANNELS; ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(captured[i], input[i], 1e-6);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/stereo_only_microphone_accepts_mono_speech_with_explicit_capture_fallback", "[audio_device]") {
  options_t options = *options_get();
  options.microphone_channels = 1;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  minimum_input_channels = 2;
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(context.input_device_channels, 2);
  pause_processing();
  float input[960], captured[960];
  for (size_t i = 0; i < 480; i++) {
    input[i * 2] = 0.25f;
    input[i * 2 + 1] = -0.75f;
  }
  ASCIICHAT_REQUIRE_EQ(invoke((device_stream_t *)context.input_stream, input, NULL, 480, 0), paContinue);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(context.raw_capture_rb, captured, 960), 960);
  for (size_t i = 0; i < 960; i++)
    ASCIICHAT_REQUIRE_EQ(captured[i], 0.25f);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/mono_speakers_receive_a_centered_downmix_of_stereo_playback", "[audio_device]") {
  context.playback_only = true;
  output_info.maxOutputChannels = 1;
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  pause_processing();
  context.playback_buffer->jitter_buffer_enabled = false;
  ASCIICHAT_REQUIRE_EQ(options_set_double("speakers_volume", 1), ASCIICHAT_OK);
  float input[960], output[480];
  for (size_t i = 0; i < 480; i++) {
    input[i * 2] = 0.25f;
    input[i * 2 + 1] = -0.75f;
  }
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_write(context.playback_buffer, input, 960), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(invoke((device_stream_t *)context.output_stream, NULL, output, 480, 0), paContinue);
  for (size_t i = 0; i < 480; i++)
    ASCIICHAT_REQUIRE_EQ(output[i], -0.25f);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/four_channel_interface_routes_only_the_selected_stereo_pair", "[audio_device]") {
  input_info.maxInputChannels = 4;
  options_t options = *options_get();
  options.microphone_channel = 2;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(context.input_device_channels, 4);
  pause_processing();
  float input[1920], captured[960];
  for (size_t i = 0; i < 480; i++) {
    input[i * 4] = 0.9f;
    input[i * 4 + 1] = 0.8f;
    input[i * 4 + 2] = 0.25f;
    input[i * 4 + 3] = -0.5f;
  }
  ASCIICHAT_REQUIRE_EQ(invoke((device_stream_t *)context.input_stream, input, NULL, 480, 0), paContinue);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(context.raw_capture_rb, captured, 960), 960);
  for (size_t i = 0; i < 480; i++) {
    ASCIICHAT_REQUIRE_EQ(captured[i * 2], 0.25f);
    ASCIICHAT_REQUIRE_EQ(captured[i * 2 + 1], -0.5f);
  }
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/invalid_interface_pair_fails_before_opening_any_stream", "[audio_device]") {
  options_t options = *options_get();
  options.microphone_channel = 1;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(opens, 0);
  ASCIICHAT_REQUIRE_NOT(context.worker_running);
}

TEST_CASE_METHOD(AudioDeviceFixture, "audio_device/remote_only_capture_without_playback_needs_no_portaudio_device", "[audio_device]") {
  options_t options = *options_get();
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_REMOTE;
  options.audio_no_playback = true;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
  fail_all = true;
  ASCIICHAT_REQUIRE_EQ(audio_start_duplex(&context), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(context.running && context.worker_running);
  ASCIICHAT_REQUIRE_EQ(initialized, 0);
  ASCIICHAT_REQUIRE_EQ(opens, 0);
  ASCIICHAT_REQUIRE_NULL(context.input_stream);
  ASCIICHAT_REQUIRE_NULL(context.output_stream);
  ASCIICHAT_REQUIRE_NULL(context.duplex_stream);
}
