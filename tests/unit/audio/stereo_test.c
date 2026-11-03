#include <criterion/criterion.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/format.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/audio/processor.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/audio/recording.h>
#include <ascii-chat/audio/wav_writer.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/audio/client_pipeline.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/util/fnv1a.h>
#include <math.h>
#include <string.h>

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
}
static void cleanup(void) {
  options_state_destroy();
  log_destroy();
}
TestSuite(stereo, .init = setup, .fini = cleanup);

Test(stereo, routes_interface_inputs_without_mixing_unrelated_channels) {
  const float input[] = {0.1f, 0.3f, -0.7f, 0.9f, 0.2f, 0.4f, -0.6f, 0.8f};
  float output[4];
  cr_assert_eq(audio_route_capture(input, 2, 4, 1, 2, output), ASCIICHAT_OK);
  cr_assert_float_eq(output[0], 0.3f, 1e-6);
  cr_assert_float_eq(output[1], -0.7f, 1e-6);
  cr_assert_float_eq(output[2], 0.4f, 1e-6);
  cr_assert_float_eq(output[3], -0.6f, 1e-6);
  cr_assert_eq(audio_route_capture(input, 2, 4, 2, 1, output), ASCIICHAT_OK);
  cr_assert_float_eq(output[0], -0.7f, 1e-6);
  cr_assert_eq(output[0], output[1]);
  cr_assert_eq(output[2], output[3]);
  cr_assert_eq(audio_route_capture(input, 2, 4, -1, 1, output), ASCIICHAT_OK);
  cr_assert_float_eq(output[0], 0.15f, 1e-6);
  cr_assert_eq(audio_route_capture(input, 2, 4, 3, 2, output), ERROR_INVALID_PARAM);
  cr_assert_eq(audio_route_capture(input, 2, 0, 0, 1, output), ERROR_INVALID_PARAM);
}

Test(stereo, mono_conversion_preserves_center_and_cancels_opposite_phase) {
  float pcm[8] = {0.2f, -0.4f, 0.6f, -0.8f};
  cr_assert_eq(audio_convert_channels(pcm, 4, 1, pcm, 2), ASCIICHAT_OK);
  for (size_t i = 0; i < 4; i++)
    cr_assert_eq(pcm[i * 2], pcm[i * 2 + 1]);
  cr_assert_eq(audio_convert_channels(pcm, 4, 2, pcm, 1), ASCIICHAT_OK);
  cr_assert_float_eq(pcm[3], -0.8f, 1e-6);
  const float opposite[] = {0.5f, -0.5f};
  cr_assert_eq(audio_convert_channels(opposite, 1, 2, pcm, 1), ASCIICHAT_OK);
  cr_assert_eq(pcm[0], 0);
}

Test(stereo, resampling_preserves_channels_and_is_independent_of_callback_partition) {
  float input[2000], whole[2400], split[2400];
  for (size_t i = 0; i < 1000; i++) {
    input[i * 2] = sinf((float)i * 0.04f) * 0.25f;
    input[i * 2 + 1] = -0.5f;
  }
  audio_resampler_t a = {0}, b = {0};
  size_t expected = audio_resample_stereo(&a, input, 1000, 44100, whole, 1200, 48000);
  size_t total = 0;
  for (size_t offset = 0; offset < 1000;) {
    size_t frames = 1000 - offset > 37 ? 37 : 1000 - offset;
    total += audio_resample_stereo(&b, input + offset * 2, frames, 44100, split + total * 2, 1200 - total, 48000);
    offset += frames;
  }
  cr_assert_eq(total, expected);
  cr_assert_geq(total, 1087);
  for (size_t i = 0; i < total; i++) {
    cr_assert_float_eq(split[i * 2], whole[i * 2], 1e-5);
    cr_assert_float_eq(split[i * 2 + 1], -0.5f, 1e-6);
  }
  audio_resampler_t before = b;
  float sentinel[] = {42, 42};
  cr_assert_eq(audio_resample_stereo(&b, input, 1000, 44100, sentinel, 1, 48000), 0);
  cr_assert_eq(memcmp(&b, &before, sizeof(b)), 0);
  cr_assert_eq(sentinel[0], 42);
}

Test(stereo, opus_preserves_distinct_tones_and_legacy_mono_decoder_compatibility) {
  opus_codec_t *encoder = opus_codec_create_encoder_channels(OPUS_APPLICATION_AUDIO, 48000, 160000, 2);
  opus_codec_t *decoder = opus_codec_create_decoder_channels(48000, 2);
  opus_codec_t *legacy = opus_codec_create_decoder(48000);
  cr_assert_not_null(encoder);
  cr_assert_not_null(decoder);
  cr_assert_not_null(legacy);
  float input[1920], output[1920], mono[960];
  uint8_t packet[4000];
  double left440 = 0, left880 = 0, right440 = 0, right880 = 0;
  for (int block = 0; block < 40; block++) {
    for (int i = 0; i < 960; i++) {
      double t = (block * 960 + i) / 48000.0;
      input[i * 2] = 0.25f * sinf((float)(2 * M_PI * 440 * t));
      input[i * 2 + 1] = 0.25f * sinf((float)(2 * M_PI * 880 * t));
    }
    size_t bytes = opus_codec_encode(encoder, input, 1920, packet, sizeof(packet));
    cr_assert_gt(bytes, 0);
    cr_assert_eq(opus_codec_decode(decoder, packet, bytes, output, 1920), 1920);
    cr_assert_eq(opus_codec_decode(legacy, packet, bytes, mono, 960), 960);
    if (block >= 10)
      for (int i = 0; i < 960; i++) {
        double t = (block * 960 + i) / 48000.0;
        left440 += output[i * 2] * cos(2 * M_PI * 440 * t);
        left880 += output[i * 2] * cos(2 * M_PI * 880 * t);
        right440 += output[i * 2 + 1] * cos(2 * M_PI * 440 * t);
        right880 += output[i * 2 + 1] * cos(2 * M_PI * 880 * t);
        cr_assert(isfinite(mono[i]));
      }
  }
  cr_assert_gt(fabs(left440), fabs(left880) * 8);
  cr_assert_gt(fabs(right880), fabs(right440) * 8);
  opus_codec_destroy(encoder);
  opus_codec_destroy(decoder);
  opus_codec_destroy(legacy);
}

Test(stereo, jitter_drift_and_fades_do_not_swap_or_mix_channels) {
  audio_ring_buffer_t *rb = audio_ring_buffer_create_format(2, false);
  cr_assert_not_null(rb);
  float input[8000], output[960];
  for (size_t i = 0; i < 4000; i++) {
    input[i * 2] = 0.25f;
    input[i * 2 + 1] = -0.5f;
  }
  cr_assert_eq(audio_ring_buffer_write(rb, input, 8000), ASCIICHAT_OK);
  rb->jitter_drift_phase = 1;
  cr_assert_eq(audio_ring_buffer_read(rb, output, 960), 960);
  for (size_t i = 0; i < 480; i++) {
    cr_assert_geq(output[i * 2], 0);
    cr_assert_float_eq(output[i * 2 + 1], output[i * 2] * -2, 1e-6);
  }
  cr_assert_eq(atomic_load_u64(&rb->read_index) % 2, 0);
  cr_assert_eq(audio_ring_buffer_read(rb, output, 959), 0);
  audio_ring_buffer_destroy(rb);
}

Test(stereo, processor_filters_do_not_leak_and_dynamics_share_gain) {
  audio_processor_config_t config = audio_processor_default_config(48000);
  config.channels = 2;
  config.spectral_enabled = false;
  config.flags = (audio_processor_flags_t){.agc = true, .compressor = true, .highpass = true};
  audio_processor_t *processor = NULL;
  cr_assert_eq(audio_processor_create(&config, &processor), ASCIICHAT_OK);
  float samples[1920];
  for (int block = 0; block < 30; block++) {
    for (size_t i = 0; i < 960; i++) {
      samples[i * 2] = 0.05f * sinf((float)(block * 960 + i) * 0.1f);
      samples[i * 2 + 1] = samples[i * 2] * 0.5f;
    }
    cr_assert_eq(audio_processor_process(processor, samples, samples, 1920), ASCIICHAT_OK);
    for (size_t i = 0; i < 960; i++)
      cr_assert_float_eq(samples[i * 2 + 1], samples[i * 2] * 0.5f, 1e-6);
  }
  audio_processor_reset(processor);
  memset(samples, 0, sizeof(samples));
  samples[0] = 1;
  cr_assert_eq(audio_processor_process(processor, samples, samples, 1920), ASCIICHAT_OK);
  for (size_t i = 0; i < 960; i++)
    cr_assert_eq(samples[i * 2 + 1], 0);
  audio_processor_destroy(processor);
}

Test(stereo, recording_preserves_channels_and_uses_frame_timestamps) {
  audio_recording_t *recording = NULL;
  cr_assert_eq(audio_recording_create_channels(&recording, 2), ASCIICHAT_OK);
  audio_recording_start(recording, 1000000000ULL);
  float input[1920], output[3840];
  for (size_t i = 0; i < 960; i++) {
    input[i * 2] = 0.25f;
    input[i * 2 + 1] = -0.5f;
  }
  audio_recording_submit(AUDIO_RECORDING_MIC, input, 1920, 1000000000ULL);
  audio_recording_submit(AUDIO_RECORDING_MIC, input, 1920, 1020000000ULL);
  audio_recording_read(recording, output, 3840);
  for (size_t i = 0; i < 1920; i++) {
    cr_assert_eq(output[i * 2], 0.25f);
    cr_assert_eq(output[i * 2 + 1], -0.5f);
  }
  audio_recording_destroy(recording);
}

Test(stereo, server_mix_minus_excludes_self_without_leaking_left_into_right) {
  mixer_t *mixer = mixer_create_channels(3, 48000, 2);
  audio_ring_buffer_t *self = audio_ring_buffer_create_format(2, true);
  audio_ring_buffer_t *remote = audio_ring_buffer_create_format(2, true);
  cr_assert_not_null(mixer);
  cr_assert_not_null(self);
  cr_assert_not_null(remote);
  cr_assert_geq(mixer_add_source(mixer, "self", self), 0);
  cr_assert_geq(mixer_add_source(mixer, "remote", remote), 0);
  float centered[1920], left[1920], output[1920];
  for (size_t i = 0; i < 960; i++) {
    centered[i * 2] = centered[i * 2 + 1] = 0.6f;
    left[i * 2] = 0.1f;
    left[i * 2 + 1] = 0;
  }
  cr_assert_eq(audio_ring_buffer_write(self, centered, 1920), ASCIICHAT_OK);
  cr_assert_eq(audio_ring_buffer_write(remote, left, 1920), ASCIICHAT_OK);
  cr_assert_eq(mixer_process_excluding_source(mixer, output, 1920, fnv1a_hash_string("self")), 1920);
  double energy = 0;
  for (size_t i = 0; i < 960; i++) {
    energy += output[i * 2] * output[i * 2];
    cr_assert_eq(output[i * 2 + 1], 0);
  }
  cr_assert_gt(energy, 1);
  cr_assert_eq(mixer_process_excluding_source(mixer, output, 1920, fnv1a_hash_string("remote")), 1920);
  for (size_t i = 0; i < 960; i++)
    cr_assert_eq(output[i * 2], output[i * 2 + 1]);
  mixer_destroy(mixer);
  audio_ring_buffer_destroy(self);
  audio_ring_buffer_destroy(remote);
}

Test(stereo, stereo_aec_and_codec_process_complete_ten_and_twenty_millisecond_frames) {
  client_audio_pipeline_config_t config = client_audio_pipeline_default_config();
  config.channels = config.encoder_channels = 2;
  config.processor.spectral_enabled = false;
  config.flags = CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
  config.flags.echo_cancel = true;
  client_audio_pipeline_t *pipeline = client_audio_pipeline_create(&config);
  cr_assert_not_null(pipeline);
  float capture[1920], render[1920] = {0}, processed[1920], decoded[1920];
  uint8_t packet[4000];
  for (int block = 0; block < 30; block++) {
    for (size_t frame = 0; frame < 960; frame++) {
      capture[frame * 2] = 0.1f * sinf((float)(block * 960 + frame) * 0.07f);
      capture[frame * 2 + 1] = 0.1f * sinf((float)(block * 960 + frame) * 0.13f);
    }
    client_audio_pipeline_process_duplex(pipeline, render, 1920, capture, 1920, processed);
    int bytes = client_audio_pipeline_capture(pipeline, processed, 1920, packet, sizeof(packet));
    cr_assert_gt(bytes, 0);
    cr_assert_eq(client_audio_pipeline_playback(pipeline, packet, bytes, decoded, 1920), 1920);
    for (size_t i = 0; i < 1920; i++)
      cr_assert(isfinite(decoded[i]));
  }
  client_audio_pipeline_destroy(pipeline);
}

Test(stereo, stereo_media_resampling_and_eof_preserve_duration_and_independent_channels) {
  char path[1024];
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "stereo-media", &fd), 0);
  platform_close(fd);
  wav_writer_t *writer = wav_writer_open(path, 32000, 2);
  cr_assert_not_null(writer);
  float input[640];
  for (size_t block = 0; block < 100; block++) {
    for (size_t frame = 0; frame < 320; frame++) {
      double t = (double)(block * 320 + frame) / 32000;
      input[frame * 2] = 0.25f * sinf((float)(2 * M_PI * 1000 * t));
      input[frame * 2 + 1] = 0;
    }
    cr_assert_eq(wav_writer_write(writer, input, 640), 0);
  }
  wav_writer_close(writer);
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  cr_assert_not_null(decoder);
  float pcm[255];
  cr_assert_eq(ffmpeg_decoder_read_audio_samples(decoder, pcm, 1), 0);
  pcm[254] = 42;
  size_t total = 0, count;
  double energy = 0;
  while ((count = ffmpeg_decoder_read_audio_samples(decoder, pcm, 255)) > 0) {
    cr_assert_eq(count % 2, 0);
    cr_assert_eq(pcm[254], 42);
    for (size_t frame = 0; frame < count / 2; frame++) {
      energy += pcm[frame * 2] * pcm[frame * 2];
      cr_assert_float_eq(pcm[frame * 2 + 1], 0, 1e-7);
    }
    total += count / 2;
    cr_assert_leq(total, 48000);
  }
  cr_assert_eq(total, 48000);
  cr_assert_gt(energy / total, 0.02);
  ffmpeg_decoder_destroy(decoder);
  platform_delete_temp_file(path);
}

Test(stereo, capture_and_transmission_layouts_are_independent) {
  for (int channels = 1; channels <= 2; channels++) {
    for (int encoded_channels = 1; encoded_channels <= 2; encoded_channels++) {
      client_audio_pipeline_config_t config = client_audio_pipeline_default_config();
      config.channels = channels;
      config.encoder_channels = encoded_channels;
      config.flags = CLIENT_AUDIO_PIPELINE_FLAGS_MINIMAL;
      config.processor.spectral_enabled = false;
      client_audio_pipeline_t *pipeline = client_audio_pipeline_create(&config);
      cr_assert_not_null(pipeline);
      float input[1920], output[1920];
      for (int i = 0; i < 960; i++)
        for (int channel = 0; channel < channels; channel++)
          input[i * channels + channel] = 0.25f * sinf((float)i * 0.1f);
      uint8_t packet[4000];
      int bytes = client_audio_pipeline_capture(pipeline, input, 960 * channels, packet, sizeof(packet));
      cr_assert_gt(bytes, 0);
      cr_assert_eq(client_audio_pipeline_playback(pipeline, packet, bytes, output, 960 * channels), 960 * channels);
      for (int i = 0; i < 960 * channels; i++)
        cr_assert(isfinite(output[i]));
      client_audio_pipeline_destroy(pipeline);
    }
  }
}
