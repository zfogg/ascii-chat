/** Production participant lifecycle with deterministic device/startup failures and real workers. */
#include "session/participant.h"
#include "session/capture.h"
#include "session/audio.h"
#include <ascii-chat/common.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/util/time.h>
#include <stdlib.h>
#include <ascii-chat/util/endian.h>

#define CHECK(c)                                                                                                       \
  do {                                                                                                                 \
    if (!(c)) {                                                                                                        \
      log_error("CHECK failed: %s at %d", #c, __LINE__);                                                               \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)
static int fail_stage, capture_count, audio_count, codec_count, workers;
static atomic_t packets, video_packets, audio_packets;
static image_t *frame;

static session_capture_ctx_t *test_capture_create(const session_capture_config_t *config) {
  (void)config;
  if (fail_stage == 1) {
    SET_ERRNO(ERROR_MEMORY, "Injected allocation failure");
    return NULL;
  }
  capture_count++;
  return (session_capture_ctx_t *)SAFE_CALLOC(1, 1, char *);
}
static void test_capture_destroy(session_capture_ctx_t *ctx) {
  CHECK(workers == 0);
  capture_count--;
  SAFE_FREE(ctx);
}
static image_t *test_read_frame(session_capture_ctx_t *ctx) {
  (void)ctx;
  return frame;
}
static image_t *test_process(session_capture_ctx_t *ctx, image_t *input) {
  (void)ctx;
  (void)input;
  return image_new(2, 2);
}
static void test_sleep(session_capture_ctx_t *ctx) {
  (void)ctx;
  platform_sleep_ms(1);
}
static void *test_media_source(session_capture_ctx_t *ctx) {
  return ctx;
}
static session_audio_ctx_t *test_audio_create(bool host) {
  (void)host;
  if (fail_stage == 3) {
    SET_ERRNO(ERROR_MEMORY, "Injected allocation failure");
    return NULL;
  }
  audio_count++;
  return (session_audio_ctx_t *)SAFE_CALLOC(1, 1, char *);
}
static void test_audio_destroy(session_audio_ctx_t *ctx) {
  CHECK(workers <= 1); // Video may still be running; audio has already joined.
  audio_count--;
  SAFE_FREE(ctx);
}
static void test_audio_source(session_audio_ctx_t *ctx, void *source) {
  (void)ctx;
  (void)source;
}
static asciichat_error_t test_audio_start(session_audio_ctx_t *ctx) {
  (void)ctx;
  return fail_stage == 4 ? ERROR_INVALID_STATE : ASCIICHAT_OK;
}
static size_t test_audio_read(session_audio_ctx_t *ctx, float *samples, size_t count) {
  (void)ctx;
  memset(samples, 0, count * sizeof(float));
  platform_sleep_ms(1);
  return count;
}
static opus_codec_t *test_codec_create(opus_application_t app, int rate, int bitrate) {
  if (fail_stage == 5) {
    SET_ERRNO(ERROR_MEMORY, "Injected allocation failure");
    return NULL;
  }
  opus_codec_t *codec = opus_codec_create_encoder(app, rate, bitrate);
  CHECK(codec);
  codec_count++;
  return codec;
}
static void test_codec_destroy(opus_codec_t *codec) {
  codec_count--;
  opus_codec_destroy(codec);
}
static int test_thread_create(asciichat_thread_t *thread, const char *name, void *(*fn)(void *), void *arg) {
  if (fail_stage == 2 || fail_stage == 6)
    return -1;
  int result = asciichat_thread_create(thread, name, fn, arg);
  if (!result)
    workers++;
  return result;
}
static int test_thread_join(asciichat_thread_t *thread, void **result) {
  int error = asciichat_thread_join(thread, result);
  if (!error)
    workers--;
  return error;
}
#define session_capture_create test_capture_create
#define session_capture_destroy test_capture_destroy
#define session_capture_read_frame test_read_frame
#define session_capture_process_for_transmission test_process
#define session_capture_sleep_for_fps test_sleep
#define session_capture_get_media_source test_media_source
#define session_audio_create test_audio_create
#define session_audio_destroy test_audio_destroy
#define session_audio_set_capture_source test_audio_source
#define session_audio_start_duplex test_audio_start
#define session_audio_read_captured test_audio_read
#define opus_codec_create_encoder test_codec_create
#define opus_codec_destroy test_codec_destroy
#define asciichat_thread_create test_thread_create
#define asciichat_thread_join test_thread_join
#include "session/participant.c"

static bool connected(acip_transport_t *transport) {
  (void)transport;
  return true;
}
static asciichat_error_t test_transport_send(acip_transport_t *transport, const void *data, size_t len) {
  (void)transport;
  CHECK(data && len);
  atomic_fetch_add_int(&packets, 1);
  CHECK(len >= sizeof(packet_header_t));
  const packet_header_t *header = data;
  uint16_t type = NET_TO_HOST_U16(header->type);
  if (type == PACKET_TYPE_IMAGE_FRAME)
    atomic_fetch_add_int(&video_packets, 1);
  if (type == PACKET_TYPE_AUDIO_OPUS_BATCH)
    atomic_fetch_add_int(&audio_packets, 1);
  return ASCIICHAT_OK;
}
static void clean(void) {
  CHECK(!workers && !capture_count && !audio_count && !codec_count);
}
int main(void) {
  log_set_terminal_output(true);
  options_t options = options_t_new();
  CHECK(options_state_init() == ASCIICHAT_OK);
  CHECK(options_state_set(&options) == ASCIICHAT_OK);
  frame = image_new(2, 2);
  CHECK(frame);
  acip_transport_methods_t methods = {.send = test_transport_send, .is_connected = connected};
  acip_transport_t transport = {.methods = &methods};
  session_participant_config_t config = {.enable_video = true, .enable_audio = true};
  CHECK(session_participant_start_video(NULL) == ERROR_INVALID_PARAM);
  session_participant_destroy(NULL);
  for (fail_stage = 1; fail_stage <= 6; fail_stage++) {
    session_participant_t *p = session_participant_create(&config);
    CHECK(p);
    CHECK(session_participant_start_video(p) == ERROR_INVALID_STATE);
    CHECK(session_participant_set_transport(p, &transport) == ASCIICHAT_OK);
    asciichat_error_t error = fail_stage <= 2 ? session_participant_start_video(p) : session_participant_start_audio(p);
    CHECK(error == (fail_stage == 1 || fail_stage == 3 || fail_stage == 5 ? ERROR_MEMORY
                    : fail_stage == 2 || fail_stage == 6                  ? ERROR_THREAD
                                                                          : ERROR_INVALID_STATE));
    CHECK(!session_participant_is_video_active(p) && !session_participant_is_audio_active(p));
    clean();
    session_participant_disconnect(p);
    session_participant_disconnect(p);
    session_participant_destroy(p);
    clean();
  }
  fail_stage = 0;
  for (int cycle = 0; cycle < 20; cycle++) {
    session_participant_t *p = session_participant_create(&config);
    CHECK(session_participant_set_transport(p, &transport) == ASCIICHAT_OK);
    CHECK(session_participant_start_video(p) == ASCIICHAT_OK);
    CHECK(session_participant_start_video(p) == ASCIICHAT_OK);
    CHECK(session_participant_start_audio(p) == ASCIICHAT_OK);
    CHECK(session_participant_start_audio(p) == ASCIICHAT_OK);
    CHECK(workers == 2 && session_participant_is_video_active(p) && session_participant_is_audio_active(p));
    CHECK(session_participant_set_transport(p, NULL) == ERROR_INVALID_STATE);
    int before = atomic_load_int(&packets);
    for (int i = 0; i < 200 && atomic_load_int(&packets) < before + 4; i++)
      platform_sleep_ms(1);
    CHECK(atomic_load_int(&packets) >= before + 4);
    if (cycle % 3 == 0) {
      session_participant_stop_video(p); // Joins audio before releasing its borrowed source.
      clean();
      session_participant_stop_video(p);
      CHECK(session_participant_start_video(p) == ASCIICHAT_OK);
    }
    if (cycle % 3 == 1)
      p->connected = false; // Partial/disconnected state still owns workers.
    if (cycle % 3 == 2) {
      session_participant_destroy(p);
      clean();
      continue;
    }
    session_participant_disconnect(p);
    clean();
    CHECK(!session_participant_is_video_active(p) && !session_participant_is_audio_active(p));
    session_participant_destroy(p);
    clean();
  }
  CHECK(atomic_load_int(&video_packets) > 0 && atomic_load_int(&audio_packets) > 0);
  // Async host startup tokens cannot reactivate a stopped or replaced stream.
  session_media_t media = {0};
  uint32_t old = session_media_begin(&media);
  CHECK(old && !session_media_is_active(&media) && !session_media_begin(&media));
  session_media_begin_stop(&media);
  CHECK(!session_media_complete(&media, old));
  session_media_finish_stop(&media);
  uint32_t next = session_media_begin(&media);
  CHECK(next != old && !session_media_complete(&media, old));
  CHECK(session_media_complete(&media, next) && session_media_is_active(&media));
  session_media_begin_stop(&media);
  session_media_finish_stop(&media);
  image_destroy(frame);
  options_state_destroy();
  log_info("PASS: six startup failures, 20 real-worker cycles, media packets, cleanup, async cancellation");
  return 0;
}
