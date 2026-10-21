#include <criterion/criterion.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/client_pipeline.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <math.h>
#include <string.h>

#ifdef __linux__
static char config_path[256];
static audio_context_t context;
static client_audio_pipeline_t *pipeline;

static void setup(void) {
  int fd;
  cr_assert_eq(platform_create_temp_file(config_path, sizeof(config_path), "ascii-null-audio", &fd), 0);
  const char config[] = "pcm.!default { type file slave.pcm { type null } infile \"/dev/zero\" "
                        "file \"/dev/null\" format \"raw\" "
                        "hint { show on description \"ascii-chat test zero PCM\" } }\n";
  cr_assert_eq(platform_write(fd, config, sizeof(config) - 1), (ssize_t)sizeof(config) - 1);
  cr_assert_eq(platform_close(fd), 0);
  cr_assert_eq(platform_setenv("ALSA_CONFIG_PATH", config_path), 0);
  memset(&context, 0, sizeof(context));
  cr_assert_eq(audio_init(&context), ASCIICHAT_OK);
}

static void teardown(void) {
  audio_destroy(&context);
  client_audio_pipeline_destroy(pipeline);
  pipeline = NULL;
  audio_terminate_portaudio_final();
  platform_unsetenv("ALSA_CONFIG_PATH");
  platform_delete_temp_file(config_path);
}

TestSuite(null_device, .init = setup, .fini = teardown);

Test(null_device, duplex_callbacks_deliver_capture_and_stop_cleanly, .timeout = 20) {
  client_audio_pipeline_config_t config = client_audio_pipeline_default_config();
  config.flags = CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
  config.processor.spectral_enabled = false;
  pipeline = client_audio_pipeline_create(&config);
  cr_assert_not_null(pipeline);
  audio_set_pipeline(&context, pipeline);
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  cr_assert(context.running);
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  uint64_t deadline = platform_get_monotonic_time_us() + 3000000;
  while (audio_ring_buffer_available_read(context.capture_buffer) < 480 && platform_get_monotonic_time_us() < deadline)
    platform_sleep_ms(5);
  cr_assert_geq(audio_ring_buffer_available_read(context.capture_buffer), 480);
  float input[480], output[480];
  for (int i = 0; i < 480; ++i)
    input[i] = 0.1f * sinf(6.283185307f * 440 * i / 48000);
  cr_assert_eq(audio_write_samples(&context, input, 480), ASCIICHAT_OK);
  cr_assert_eq(audio_read_samples(&context, output, 480), ASCIICHAT_OK);
  for (int i = 0; i < 480; ++i)
    cr_assert(isfinite(output[i]));
  audio_flush_playback_buffers(&context);
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
  cr_assert_not(context.running);
  cr_assert_null(context.duplex_stream);
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
}

Test(null_device, playback_only_stream_consumes_queued_samples, .timeout = 20) {
  context.playback_only = true;
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  cr_assert_not_null(context.output_stream);
  float input[9600] = {0.1f};
  cr_assert_eq(audio_write_samples(&context, input, 9600), ASCIICHAT_OK);
  uint64_t deadline = platform_get_monotonic_time_us() + 3000000;
  while (audio_ring_buffer_available_read(context.playback_buffer) > 0 && platform_get_monotonic_time_us() < deadline)
    platform_sleep_ms(5);
  cr_assert_eq(audio_ring_buffer_available_read(context.playback_buffer), 0);
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
}

Test(null_device, input_only_device_is_enumerated_and_normalized_to_the_pipeline_clock, .timeout = 20) {
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = *options_get();
  options->audio_no_playback = true;
  options->audio_capture_source = AUDIO_CAPTURE_SOURCE_MIC;
  cr_assert_eq(options_state_set(options), ASCIICHAT_OK);
  SAFE_FREE(options);
  audio_device_info_t *devices = NULL;
  unsigned int count = 0;
  cr_assert_eq(audio_list_input_devices(&devices, &count), ASCIICHAT_OK);
  cr_assert_gt(count, 0);
  audio_free_device_list(devices);
  cr_assert_eq(audio_list_output_devices(&devices, &count), ASCIICHAT_OK);
  cr_assert_gt(count, 0);
  audio_free_device_list(devices);
  client_audio_pipeline_config_t config = client_audio_pipeline_default_config();
  pipeline = client_audio_pipeline_create(&config);
  cr_assert_not_null(pipeline);
  audio_set_pipeline(&context, pipeline);
  cr_assert_eq(audio_start_duplex(&context), ASCIICHAT_OK);
  cr_assert_not_null(context.input_stream);
  cr_assert_null(context.output_stream);
  uint64_t deadline = platform_get_monotonic_time_us() + 3000000;
  while (audio_ring_buffer_available_read(context.capture_buffer) < 960 && platform_get_monotonic_time_us() < deadline)
    platform_sleep_ms(5);
  cr_assert_geq(audio_ring_buffer_available_read(context.capture_buffer), 960);
  float captured[960] = {0};
  cr_assert_eq(audio_read_samples(&context, captured, 960), ASCIICHAT_OK);
  for (int i = 0; i < 960; ++i)
    cr_assert(isfinite(captured[i]));
  cr_assert_eq(audio_stop_duplex(&context), ASCIICHAT_OK);
}
#endif
