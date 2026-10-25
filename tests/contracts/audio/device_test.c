#include <criterion/criterion.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/platform/thread.h>
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
static bool fail_duplex, fail_all, no_input, no_output;
static PaDeviceInfo input_info, output_info;
static audio_context_t context;

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
  cr_assert_lt(opens, 16);
  device_stream_t *entry = &streams[opens++];
  *entry = (device_stream_t){
      .callback = callback, .data = data, .input = input != NULL, .output = output != NULL, .rate = rate};
  if (input) {
    cr_assert_eq(input->sampleFormat, paFloat32);
    cr_assert_eq(input->channelCount, AUDIO_CHANNELS);
  }
  if (output) {
    cr_assert_eq(output->sampleFormat, paFloat32);
    cr_assert_eq(output->channelCount, AUDIO_CHANNELS);
  }
  // Some drivers return a partial stream even when opening fails.
  *stream = (PaStream *)entry;
  return fail_all || opens == fail_open || (fail_duplex && input && output) ? paUnanticipatedHostError : paNoError;
}
PaError Pa_StartStream(PaStream *stream) {
  cr_assert_not_null(stream);
  starts++;
  return starts == fail_start ? paUnanticipatedHostError : paNoError;
}
PaError Pa_StopStream(PaStream *stream) {
  cr_assert_not_null(stream);
  stops++;
  return paNoError;
}
PaError Pa_CloseStream(PaStream *stream) {
  device_stream_t *entry = (device_stream_t *)stream;
  cr_assert_not_null(entry);
  cr_assert_not(entry->closed, "Stream closed twice");
  entry->closed = true;
  closes++;
  return paNoError;
}

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  options_t options = *options_get();
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_MIC;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  input_info = (PaDeviceInfo){
      .structVersion = 2, .name = "contract microphone", .maxInputChannels = 1, .defaultSampleRate = AUDIO_SAMPLE_RATE};
  output_info = (PaDeviceInfo){
      .structVersion = 2, .name = "contract speaker", .maxOutputChannels = 1, .defaultSampleRate = AUDIO_SAMPLE_RATE};
  cr_assert_eq(audio_init(&context), ASCIICHAT_OK);
}
static void teardown(void) {
  audio_destroy(&context);
  audio_terminate_portaudio_final();
  cr_assert_eq(terminated, initialized);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(audio_device, .init = setup, .fini = teardown, .timeout = 10);

static void pause_processing(void) {
  cr_assert(context.worker_running);
  atomic_store_bool(&context.worker_should_stop, true);
  cond_signal(&context.audio_processing_wakeup);
  cr_assert_eq(thread_join(&context.worker_thread, NULL), 0);
  context.worker_running = false;
}
static int invoke(device_stream_t *stream, const float *input, float *output, unsigned long count,
                  PaStreamCallbackFlags flags) {
  PaStreamCallbackTimeInfo time = {0};
  return stream->callback(input, output, count, &time, flags, stream->data);
}

Test(audio_device, duplex_callback_preserves_capture_scales_playback_and_silences_underrun_and_shutdown) {
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  cr_assert(context.running && context.duplex_stream && !context.separate_streams);
  cr_assert_eq(opens, 1);
  cr_assert_eq(starts, 1);
  cr_assert_eq(initialized, 1);
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  cr_assert_eq(opens, 1);
  pause_processing();
  context.playback_buffer->jitter_buffer_enabled = false;
  float input[32], playback[16], output[32], captured[32];
  for (unsigned i = 0; i < 32; ++i)
    input[i] = (float)i / 64;
  for (unsigned i = 0; i < 16; ++i)
    playback[i] = i & 1 ? -0.5f : 0.25f;
  cr_assert_eq(options_set_double("speakers_volume", 0.5), ASCIICHAT_OK);
  cr_assert_eq(audio_ring_buffer_write(context.playback_buffer, playback, 16), ASCIICHAT_OK);
  cr_assert_eq(invoke(&streams[0], input, output, 32, paOutputUnderflow | paInputOverflow), paContinue);
  for (unsigned i = 0; i < 32; ++i)
    cr_assert_float_eq(output[i], i < 16 ? playback[i] * 0.5f : 0, 1e-6);
  cr_assert_eq(audio_ring_buffer_read(context.raw_capture_rb, captured, 32), 32);
  cr_assert_arr_eq(captured, input, sizeof(input));
  cr_assert_eq(audio_ring_buffer_read(context.raw_render_rb, captured, 32), 32);
  cr_assert_arr_eq(captured, output, sizeof(output));
  context.playback_only = true;
  cr_assert_eq(invoke(&streams[0], input, output, 32, 0), paContinue);
  cr_assert_eq(audio_ring_buffer_available_read(context.raw_capture_rb), 0);
  cr_assert_eq(audio_ring_buffer_available_read(context.raw_render_rb), 0);
  atomic_store_bool(&context.shutting_down, true);
  for (unsigned i = 0; i < 32; ++i)
    output[i] = 1;
  cr_assert_eq(invoke(&streams[0], input, output, 32, 0), paContinue);
  for (unsigned i = 0; i < 32; ++i)
    cr_assert_eq(output[i], 0);
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
  cr_assert_not(context.running);
  cr_assert_null(context.duplex_stream);
  cr_assert_eq(stops, 1);
  cr_assert_eq(closes, 1);
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
  cr_assert_eq(closes, 1);
}

Test(audio_device, separate_native_rate_callbacks_convert_constant_samples_and_ignore_shutdown_capture) {
  input_info.defaultSampleRate = 24000;
  output_info.defaultSampleRate = 32000;
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  cr_assert(context.separate_streams && context.input_stream && context.output_stream);
  cr_assert_eq(opens, 2);
  cr_assert_eq(streams[0].rate, 32000);
  cr_assert_eq(streams[1].rate, 24000);
  pause_processing();
  context.playback_buffer->jitter_buffer_enabled = false;
  float samples[256], output[64], captured[256];
  for (unsigned i = 0; i < 256; ++i)
    samples[i] = 0.25f;
  cr_assert_eq(options_set_double("speakers_volume", 1.0), ASCIICHAT_OK);
  cr_assert_eq(audio_ring_buffer_write(context.playback_buffer, samples, 256), ASCIICHAT_OK);
  cr_assert_eq(invoke(&streams[0], NULL, output, 64, paOutputUnderflow), paContinue);
  for (unsigned i = 0; i < 64; ++i)
    cr_assert_float_eq(output[i], 0.25f, 1e-6);
  cr_assert_eq(invoke(&streams[1], samples, NULL, 64, paInputOverflow), paContinue);
  size_t count = audio_ring_buffer_read(context.raw_capture_rb, captured, 256);
  cr_assert_geq(count, 126);
  cr_assert_leq(count, 128);
  for (size_t i = 0; i < count; ++i)
    cr_assert_float_eq(captured[i], 0.25f, 1e-6);
  atomic_store_bool(&context.shutting_down, true);
  cr_assert_eq(invoke(&streams[1], samples, NULL, 64, 0), paContinue);
  cr_assert_eq(audio_ring_buffer_available_read(context.raw_capture_rb), 0);
  cr_assert_eq(invoke(&streams[0], NULL, output, 64, 0), paContinue);
  for (unsigned i = 0; i < 64; ++i)
    cr_assert_eq(output[i], 0);
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
  cr_assert_eq(closes, 2);
  cr_assert_eq(stops, 2);
  cr_assert_null(context.render_buffer);
}

Test(audio_device, duplex_open_failure_falls_back_and_retires_all_partial_streams) {
  fail_duplex = true;
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  cr_assert(context.separate_streams && context.input_stream && context.output_stream);
  cr_assert_eq(opens, 3);
  cr_assert(streams[0].closed);
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
  cr_assert_eq(closes, opens);
  for (unsigned i = 0; i < opens; ++i)
    cr_assert(streams[i].closed);
}

Test(audio_device, output_rate_retry_closes_partial_stream_before_starting_fallback) {
  context.playback_only = true;
  fail_open = 1;
  output_info.defaultSampleRate = 44100;
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  cr_assert_eq(opens, 2);
  cr_assert(streams[0].closed);
  cr_assert_eq(context.output_device_rate, AUDIO_SAMPLE_RATE);
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
  cr_assert_eq(closes, 2);
}

Test(audio_device, start_failure_releases_both_streams_and_allows_clean_retry) {
  input_info.defaultSampleRate = 24000;
  fail_start = 2;
  cr_assert_eq(audio_start_duplex(&context), ERROR_AUDIO);
  cr_assert_null(context.input_stream);
  cr_assert_null(context.output_stream);
  cr_assert_null(context.render_buffer);
  cr_assert_not(context.running || context.worker_running);
  cr_assert_eq(closes, 2);
  fail_start = 0;
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  cr_assert(context.worker_running && context.running);
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
  cr_assert_eq(closes, opens);
}

Test(audio_device, absent_microphone_and_failed_devices_report_audio_error_without_starting_worker) {
  no_input = true;
  cr_assert_eq(audio_start_duplex(&context), ERROR_AUDIO);
  cr_assert_not(context.worker_running);
  cr_assert_eq(opens, 0);
  no_input = false;
  fail_all = true;
  cr_assert_eq(audio_start_duplex(&context), ERROR_AUDIO);
  cr_assert_not(context.running || context.worker_running);
  cr_assert_null(context.input_stream);
  cr_assert_null(context.output_stream);
  cr_assert_null(context.render_buffer);
  cr_assert_eq(closes, opens);
}

Test(audio_device, real_worker_publishes_microphone_samples_with_configured_sensitivity) {
  cr_assert_eq(options_set_double("microphone_sensitivity", 0.25), ASCIICHAT_OK);
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  float input[480], output[480], captured[480];
  for (unsigned i = 0; i < 480; ++i)
    input[i] = i & 1 ? -0.5f : 0.25f;
  cr_assert_eq(invoke(&streams[0], input, output, 480, 0), paContinue);
  uint64_t deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (audio_ring_buffer_available_read(context.capture_buffer) < 480 && time_get_ns() < deadline)
    platform_sleep_ns(NS_PER_MS_INT);
  cr_assert_eq(audio_ring_buffer_read(context.capture_buffer, captured, 480), 480);
  for (unsigned i = 0; i < 480; ++i)
    cr_assert_float_eq(captured[i], input[i] * 0.25f, 1e-6);
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
  cr_assert_not(context.worker_running || context.running);
  cr_assert_eq(closes, opens);
}

// Signal counters are debug instrumentation; the sample-delivery tests run in every build.
#ifndef NDEBUG
Test(audio_device, capture_callbacks_notify_once_per_twenty_millisecond_batch) {
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  pause_processing();
  float input[480] = {0}, output[480];
  uint64_t signals = context.audio_processing_wakeup.signal_count;
  cr_assert_eq(invoke(&streams[0], input, output, 480, 0), paContinue);
  cr_assert_eq(context.audio_processing_wakeup.signal_count, signals);
  cr_assert_eq(invoke(&streams[0], input, output, 480, 0), paContinue);
  cr_assert_eq(context.audio_processing_wakeup.signal_count, signals + 1);
  cr_assert_eq(audio_ring_buffer_available_read(context.raw_capture_rb), 960);
}
#endif

Test(audio_device, real_worker_preserves_both_ten_millisecond_capture_blocks) {
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  float input[960], output[480], captured[960];
  for (unsigned i = 0; i < 960; ++i)
    input[i] = (i & 1) ? -0.25f : 0.5f;
  cr_assert_eq(invoke(&streams[0], input, output, 480, 0), paContinue);
  cr_assert_eq(invoke(&streams[0], input + 480, output, 480, 0), paContinue);
  uint64_t deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (audio_ring_buffer_available_read(context.capture_buffer) < 960 && time_get_ns() < deadline)
    platform_sleep_ns(NS_PER_MS_INT);
  cr_assert_eq(audio_ring_buffer_read(context.capture_buffer, captured, 960), 960);
  for (unsigned i = 0; i < 960; ++i)
    cr_assert_float_eq(captured[i], input[i], 1e-6);
}
