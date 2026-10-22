#include <ascii-chat/otel.h>
#include <ascii-chat/common.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/video/h265/encoder.h>
#include <ascii-chat/video/h265/decoder.h>
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
int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  options_t options = options_t_new();
  const char *endpoint = SAFE_GETENV("ASCII_CHAT_OTEL_FIXTURE_ENDPOINT");
  CHECK(endpoint != NULL);
  SAFE_STRNCPY(options.otel_endpoint, endpoint, sizeof(options.otel_endpoint));
  options.otel_realtime_sample_rate = 1;
  options.otel_realtime_max_per_second = 10000;
  CHECK(options_state_init() == ASCIICHAT_OK);
  CHECK(options_state_set(&options) == ASCIICHAT_OK);
  char *args[] = {"otel-media-fixture"};
  CHECK(ascii_otel_init("media-fixture", 1, args) == ASCIICHAT_OK);
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
