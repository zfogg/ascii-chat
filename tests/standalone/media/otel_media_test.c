#include <ascii-chat/otel.h>
#include <ascii-chat/common.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/video/h265/encoder.h>
#include <ascii-chat/video/h265/decoder.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/log/log.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#define CHECK(value)                                                                                                   \
  do {                                                                                                                 \
    if (!(value))                                                                                                      \
      FATAL(ERROR_ASSERTION_FAILED, "%s", #value);                                                                     \
  } while (0)
static void emit_severity(int level, const char *format, ...) {
  va_list args;
  va_start(args, format);
  ascii_otel_log(level, __FILE__, __LINE__, __func__, format, args);
  va_end(args);
}
int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  options_t options = options_t_new();
  const char *endpoint = SAFE_GETENV("ASCII_CHAT_OTEL_FIXTURE_ENDPOINT");
  CHECK(endpoint != NULL);
  SAFE_STRNCPY(options.otel_endpoint, endpoint, sizeof(options.otel_endpoint));
  options.otel_realtime_sample_rate = 1;
  options.otel_realtime_max_per_second = 10000;
  SAFE_STRNCPY(options.otel_log_level, "dev", sizeof(options.otel_log_level));
  CHECK(options_state_init() == ASCIICHAT_OK);
  CHECK(options_state_set(&options) == ASCIICHAT_OK);
  char *args[] = {"ascii-chat", "mirror"};
  CHECK(ascii_otel_init("mirror", 2, args) == ASCIICHAT_OK);
  for (int level = LOG_DEV; level <= LOG_FATAL; ++level)
    emit_severity(level, "otel severity fixture %d", level);
  char video_path[OPTIONS_BUFF_SIZE], audio_path[OPTIONS_BUFF_SIZE];
  const char *path = SAFE_GETENV("ASCII_CHAT_OTEL_FIXTURE_VIDEO");
  CHECK(path != NULL);
  SAFE_STRNCPY(video_path, path, sizeof(video_path));
  path = SAFE_GETENV("ASCII_CHAT_OTEL_FIXTURE_AUDIO");
  CHECK(path != NULL);
  SAFE_STRNCPY(audio_path, path, sizeof(audio_path));
  ffmpeg_decoder_t *media = ffmpeg_decoder_create(video_path);
  CHECK(media != NULL);
  CHECK(ffmpeg_decoder_start_prefetch(media) == ASCIICHAT_OK);
  image_t *frame = NULL;
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  while (!frame && time_get_ns() < deadline) {
    frame = ffmpeg_decoder_read_video_frame(media);
    if (!frame)
      platform_sleep_ms(1);
  }
  CHECK(frame && frame->w == 16 && frame->h == 16);
  ffmpeg_decoder_destroy(media);
  media = ffmpeg_decoder_create(audio_path);
  CHECK(media != NULL && ffmpeg_decoder_has_audio(media));
  float samples[128];
  CHECK(ffmpeg_decoder_read_audio_samples(media, samples, 128) > 0);
  CHECK(ffmpeg_decoder_seek_to_timestamp(media, 0) == ASCIICHAT_OK);
  CHECK(ffmpeg_decoder_read_audio_samples(media, samples, 128) > 0);
  ffmpeg_decoder_destroy(media);
  char missing_path[sizeof(video_path) + 16];
  safe_snprintf(missing_path, sizeof(missing_path), "%s.missing", video_path);
  CHECK(ffmpeg_decoder_create(missing_path) == NULL);
  CLEAR_ERRNO();
  uint8_t input[32 * 32 * 3], decoded[32 * 32 * 3];
  memset(input, 65, sizeof(input));
  void *compressed = NULL;
  size_t compressed_size = 0;
  CHECK(compress_data(input, sizeof(input), &compressed, &compressed_size, 3) == ASCIICHAT_OK);
  CHECK(decompress_data(compressed, compressed_size, decoded, sizeof(decoded)) == ASCIICHAT_OK);
  CHECK(memcmp(input, decoded, sizeof(input)) == 0);
  CHECK(decompress_data_exact(compressed, compressed_size, decoded, sizeof(decoded)) == ASCIICHAT_OK);
  CHECK(memcmp(input, decoded, sizeof(input)) == 0);
  CHECK(decompress_data_exact(compressed, compressed_size, decoded, sizeof(decoded) - 1) != ASCIICHAT_OK);
  CLEAR_ERRNO();
  buffer_release(compressed);
  compression_workspace_t workspace = {0};
  CHECK(compress_workspace(&workspace, input, sizeof(input), 3) == ASCIICHAT_OK);
  compression_workspace_destroy(&workspace);
  if (!avcodec_find_encoder_by_name("libx264") || !avcodec_find_encoder_by_name("libx265")) {
    ascii_otel_exit(0);
    ascii_otel_shutdown();
    options_state_destroy();
    return 77;
  }
  uint8_t packet[65536];
  for (int i = 0; i < 2; ++i) {
    h265_encoder_t *encoder = video_encoder_create(i ? VIDEO_CODEC_H264 : VIDEO_CODEC_H265, 32, 32);
    CHECK(encoder != NULL);
    size_t packet_size = sizeof(packet);
    CHECK(h265_encode(encoder, 32, 32, input, packet, &packet_size) == ASCIICHAT_OK);
    CHECK(packet_size > 5);
    if (!i) {
      h265_decoder_t *decoder = h265_decoder_create();
      CHECK(decoder != NULL);
      uint16_t width = 0, height = 0;
      size_t size = sizeof(decoded);
      CHECK(h265_decode(decoder, packet, packet_size, &width, &height, decoded, &size) == ASCIICHAT_OK);
      CHECK(width == 32 && height == 32 && size == 1024);
      h265_decoder_destroy(decoder);
    }
    h265_encoder_destroy(encoder);
  }
  ascii_otel_exit(0);
  ascii_otel_shutdown();
  options_state_destroy();
  return 0;
}
