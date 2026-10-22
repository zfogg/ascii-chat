#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include <ascii-chat/media/ffmpeg_encoder.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <math.h>
#include <string.h>

static char directory[512], path[600];
static ffmpeg_encoder_t *encoder;
static ffmpeg_decoder_t *decoder;
static media_source_t *source;

static void setup(void) {
  cr_assert_eq(platform_mkdtemp(directory, sizeof(directory), "ascii-codec-roundtrip"), ASCIICHAT_OK);
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = *options_get();
  options->snapshot_mode = false;
  cr_assert_eq(options_state_set(options), ASCIICHAT_OK);
  SAFE_FREE(options);
}

static void cleanup(void) {
  media_source_destroy(source);
  source = NULL;
  ffmpeg_decoder_destroy(decoder);
  decoder = NULL;
  ffmpeg_encoder_destroy(encoder);
  encoder = NULL;
  platform_rmdir_recursive(directory);
}

TestSuite(codec_roundtrip, .init = setup, .fini = cleanup, .timeout = 30);

static image_t *next_frame(bool from_source) {
  uint64_t deadline = platform_get_monotonic_time_us() + 3000000;
  image_t *image = NULL;
  do {
    image = from_source ? media_source_read_video(source) : ffmpeg_decoder_read_video_frame(decoder);
    if (image)
      return image;
    platform_sleep_ms(1);
  } while (platform_get_monotonic_time_us() < deadline);
  cr_assert_not_null(image, "No decoded frame arrived before the deadline");
  return image;
}

static void encode_fixture(const char *extension, int frames, bool audio) {
  safe_snprintf(path, sizeof(path), "%s/fixture.%s", directory, extension);
  cr_assert_eq(ffmpeg_encoder_create(path, 64, 48, 24, &encoder), ASCIICHAT_OK, "Format: %s", extension);
  uint8_t rgba[64 * 48 * 4];
  float samples[2000];
  for (int frame = 0; frame < frames; ++frame) {
    for (size_t pixel = 0; pixel < 64 * 48; ++pixel) {
      rgba[4 * pixel] = 36;
      rgba[4 * pixel + 1] = 146;
      rgba[4 * pixel + 2] = 170;
      rgba[4 * pixel + 3] = 255;
    }
    cr_assert_eq(ffmpeg_encoder_write_frame(encoder, rgba, 64 * 4, 1000000000ULL + frame * 41666667ULL), ASCIICHAT_OK);
    if (audio) {
      for (int i = 0; i < 2000; ++i)
        samples[i] = 0.2f * sinf(6.283185307f * 480.0f * (frame * 2000 + i) / 48000.0f);
      cr_assert_eq(ffmpeg_encoder_write_audio(encoder, samples, 2000), ASCIICHAT_OK);
    }
  }
  cr_assert_eq(ffmpeg_encoder_destroy(encoder), ASCIICHAT_OK);
  encoder = NULL;
}

typedef struct {
  char extension[8];
  bool audio;
  bool image;
  int tolerance;
} format_case_t;
// RGB332 GIF conversion permits one 3-bit palette step (255 / 7).
static format_case_t formats[] = {{"png", false, true, 0},  {"jpg", false, true, 20}, {"gif", false, false, 36},
                                  {"mp4", true, false, 20}, {"mkv", true, false, 20}, {"webm", true, false, 20},
                                  {"avi", true, false, 20}, {"mov", true, false, 20}, {"flv", true, false, 20},
                                  {"3gp", true, false, 20}, {"mka", true, false, 20}, {"unknown", true, false, 20},
                                  {"jpeg", false, true, 20}};
ParameterizedTestParameters(codec_roundtrip, encoded_frames_and_audio_decode_to_the_original_signal) {
  return cr_make_param_array(format_case_t, formats, sizeof(formats) / sizeof(formats[0]));
}
ParameterizedTest(format_case_t *format, codec_roundtrip, encoded_frames_and_audio_decode_to_the_original_signal) {
  encode_fixture(format->extension, format->image ? 1 : 12, format->audio);
  decoder = ffmpeg_decoder_create(path);
  cr_assert_not_null(decoder);
  cr_assert(ffmpeg_decoder_has_video(decoder));
  int width, height;
  cr_assert_eq(ffmpeg_decoder_get_video_dimensions(decoder, &width, &height), ASCIICHAT_OK);
  cr_assert_eq(width, 64);
  cr_assert_eq(height, 48);
  cr_assert_eq(ffmpeg_decoder_start_prefetch(decoder), ASCIICHAT_OK);
  image_t *image = next_frame(false);
  cr_assert_not_null(image);
  for (size_t i = 0; i < 64 * 48; ++i) {
    cr_assert_leq(abs((int)image->pixels[i].r - 36), format->tolerance, "%s RGB=%u,%u,%u", format->extension,
                  image->pixels[i].r, image->pixels[i].g, image->pixels[i].b);
    cr_assert_leq(abs((int)image->pixels[i].g - 146), format->tolerance, "%s RGB=%u,%u,%u", format->extension,
                  image->pixels[i].r, image->pixels[i].g, image->pixels[i].b);
    cr_assert_leq(abs((int)image->pixels[i].b - 170), format->tolerance, "%s blue=%u", format->extension,
                  image->pixels[i].b);
  }
  cr_assert_eq(ffmpeg_decoder_has_audio(decoder), format->audio);
  if (format->audio) {
    cr_assert_gt(ffmpeg_decoder_get_duration(decoder), 0.35);
    cr_assert_eq(ffmpeg_decoder_seek_to_timestamp(decoder, 0.2), ASCIICHAT_OK);
    cr_assert_not_null(next_frame(false));
    cr_assert_eq(ffmpeg_decoder_rewind(decoder), ASCIICHAT_OK);
    cr_assert_not_null(next_frame(false));
    ffmpeg_decoder_destroy(decoder);
    decoder = ffmpeg_decoder_create(path);
    cr_assert_not_null(decoder);
    float samples[4096];
    size_t count = ffmpeg_decoder_read_audio_samples(decoder, samples, 4096);
    cr_assert_gt(count, 960);
    double energy = 0, in_phase = 0, quadrature = 0;
    for (size_t i = 0; i < count; ++i) {
      cr_assert(isfinite(samples[i]));
      energy += samples[i] * samples[i];
      in_phase += samples[i] * cos(6.283185307 * 480 * i / 48000);
      quadrature += samples[i] * sin(6.283185307 * 480 * i / 48000);
    }
    cr_assert_gt(2 * hypot(in_phase, quadrature) / count, 0.08, "%s lost the 480 Hz tone", format->extension);
    cr_assert_gt(sqrt(energy / count), 0.02);
    cr_assert_lt(sqrt(energy / count), 0.3, "%s RMS=%g", format->extension, sqrt(energy / count));
    cr_assert_eq(ffmpeg_decoder_seek_to_timestamp(decoder, 0.2), ASCIICHAT_OK);
    cr_assert_gt(ffmpeg_decoder_read_audio_samples(decoder, samples, 4096), 0);
    cr_assert_eq(ffmpeg_decoder_rewind(decoder), ASCIICHAT_OK);
    cr_assert_gt(ffmpeg_decoder_read_audio_samples(decoder, samples, 4096), 0);
  }
}

Test(codec_roundtrip, media_source_pause_seek_rewind_and_loop_preserve_a_playable_stream) {
  encode_fixture("avi", 12, true);
  source = media_source_create(MEDIA_SOURCE_FILE, path);
  cr_assert_not_null(source);
  cr_assert_eq(media_source_get_type(source), MEDIA_SOURCE_FILE);
  cr_assert(media_source_has_video(source));
  cr_assert(media_source_has_audio(source));
  cr_assert_not(media_source_uses_webcam(source));
  cr_assert_gt(media_source_get_duration(source), 0.35);
  cr_assert_float_eq(media_source_get_video_fps(source), 24, 0.01, "FPS: %g", media_source_get_video_fps(source));
  media_source_pause(source);
  cr_assert(media_source_is_paused(source));
  cr_assert_null(media_source_read_video(source));
  media_source_toggle_pause(source);
  cr_assert_not(media_source_is_paused(source));
  cr_assert_not_null(next_frame(true));
  cr_assert_eq(media_source_seek(source, 0.2), ASCIICHAT_OK);
  cr_assert_geq(media_source_get_position(source), 0);
  cr_assert_eq(media_source_rewind(source), ASCIICHAT_OK);
  media_source_set_loop(source, true);
  for (int i = 0; i < 30; ++i) {
    cr_assert_not_null(next_frame(true), "Loop stopped at frame %d", i);
  }
  media_source_resume(source);
  cr_assert_not(media_source_at_end(source));
}

static void put_le16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *out, uint32_t value) {
  put_le16(out, (uint16_t)value);
  put_le16(out + 2, (uint16_t)(value >> 16));
}

Test(codec_roundtrip, independent_stereo_pcm_is_resampled_and_rewound) {
  // A manually serialized WAV keeps the decoder check independent of our encoder.
  uint8_t wav[44 + 8000 * 4] = {0};
  memcpy(wav, "RIFF", 4);
  put_le32(wav + 4, sizeof(wav) - 8);
  memcpy(wav + 8, "WAVEfmt ", 8);
  put_le32(wav + 16, 16);
  put_le16(wav + 20, 1);
  put_le16(wav + 22, 2);
  put_le32(wav + 24, 32000);
  put_le32(wav + 28, 32000 * 4);
  put_le16(wav + 32, 4);
  put_le16(wav + 34, 16);
  memcpy(wav + 36, "data", 4);
  put_le32(wav + 40, sizeof(wav) - 44);
  for (int i = 0; i < 8000; ++i) {
    double tone = sin(6.283185307 * 480 * i / 32000);
    put_le16(wav + 44 + i * 4, (uint16_t)(int16_t)(tone * 6553));
    put_le16(wav + 46 + i * 4, (uint16_t)(int16_t)(tone * 3276));
  }
  safe_snprintf(path, sizeof(path), "%s/stereo.wav", directory);
  int fd = platform_open("stereo-wav", path,
                         PLATFORM_O_WRONLY | PLATFORM_O_CREAT | PLATFORM_O_TRUNC | PLATFORM_O_BINARY, 0600);
  cr_assert_geq(fd, 0);
  ssize_t written = platform_write(fd, wav, sizeof(wav));
  int closed = platform_close(fd);
  cr_assert_eq(written, sizeof(wav));
  cr_assert_eq(closed, 0);
  decoder = ffmpeg_decoder_create(path);
  cr_assert_not_null(decoder);
  cr_assert(ffmpeg_decoder_has_audio(decoder));
  cr_assert_not(ffmpeg_decoder_has_video(decoder));
  cr_assert_float_eq(ffmpeg_decoder_get_duration(decoder), 0.25, 0.001);
  float samples[257];
  size_t total = 0, count;
  double in_phase = 0, quadrature = 0;
  while ((count = ffmpeg_decoder_read_audio_samples(decoder, samples, 257)) != 0) {
    cr_assert_leq(total + count, 12000);
    for (size_t i = 0; i < count; ++i) {
      cr_assert(isfinite(samples[i]));
      in_phase += samples[i] * cos(6.283185307 * 480 * (total + i) / 48000);
      quadrature += samples[i] * sin(6.283185307 * 480 * (total + i) / 48000);
    }
    total += count;
  }
  // A resampler may retain a short filter tail at EOF.
  cr_assert_geq(total, 11900);
  cr_assert_gt(2 * hypot(in_phase, quadrature) / total, 0.1);
  cr_assert(ffmpeg_decoder_at_end(decoder));
  cr_assert_eq(ffmpeg_decoder_rewind(decoder), ASCIICHAT_OK);
  cr_assert_not(ffmpeg_decoder_at_end(decoder));
  cr_assert_eq(ffmpeg_decoder_read_audio_samples(decoder, samples, 257), 257);
}