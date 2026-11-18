#include <catch2/catch_test_macros.hpp>
#include "server/stream.h"
#include <options/rcu.h>
#include "session/frame_validation.h"
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/video/rgba/video_frame.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <string.h>

atomic_t g_should_exit;
client_manager_t g_client_manager;
rwlock_t g_client_manager_rwlock;
bool g_client_manager_rwlock_initialized;

static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  REQUIRE((rwlock_init(&g_client_manager_rwlock, "stream-clients")) == (ASCIICHAT_OK));
  g_client_manager_rwlock_initialized = true;
  atomic_store_bool(&g_should_exit, false);
  for (int i = 0; i < 3; ++i) {
    client_info_t *client = &g_client_manager.clients[i];
    SAFE_SNPRINTF(client->client_id, sizeof(client->client_id), "source-%d", i);
    REQUIRE((mutex_init(&client->client_state_mutex, "stream-state")) == (ASCIICHAT_OK));
    options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
    *values = options_t_new();
    values->width = 80;
    values->height = 24;
    values->fps = 30;
    values->color_mode = TERM_COLOR_NONE;
    values->palette_type = PALETTE_CUSTOM;
    SAFE_STRNCPY(values->palette_custom, "#@", sizeof(values->palette_custom));
    REQUIRE((acip_render_settings_prepare(&client->render_settings, values)) == (ASCIICHAT_OK));
    SAFE_FREE(values);
    client->has_terminal_caps = client->client_palette_initialized = true;
    client->incoming_video_buffer = video_frame_buffer_create(client->client_id);
    REQUIRE((client->incoming_video_buffer) != nullptr);
    atomic_store_bool(&client->active, true);
  }
}

static void teardown(void) {
  for (int i = 0; i < 3; ++i) {
    client_info_t *client = &g_client_manager.clients[i];
    video_frame_buffer_destroy(client->incoming_video_buffer);
    digital_rain_destroy(client->digital_rain);
    mutex_destroy(&client->client_state_mutex);
  }
  rwlock_destroy(&g_client_manager_rwlock);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static void publish(int source, uint32_t width, uint32_t height, unsigned char intensity) {
  client_info_t *client = &g_client_manager.clients[source];
  video_frame_t *frame = video_frame_begin_write(client->incoming_video_buffer);
  REQUIRE((frame) != nullptr);
  size_t length = 8 + (size_t)width * height * 3;
  REQUIRE((length) <= (client->incoming_video_buffer->allocated_buffer_size));
  uint32_t dimensions[] = {HOST_TO_NET_U32(width), HOST_TO_NET_U32(height)};
  memcpy(frame->data, dimensions, sizeof(dimensions));
  memset((uint8_t *)frame->data + sizeof(dimensions), intensity, length - sizeof(dimensions));
  frame->size = length;
  frame->width = width;
  frame->height = height;
  frame->capture_timestamp_ns = time_get_ns();
  video_frame_commit(client->incoming_video_buffer);
  atomic_store_bool(&client->is_sending_video, true);
}

static char *render(const char *target, size_t *size, bool *changed, int *sources) {
  return create_mixed_ascii_frame_for_client(target, 80, 24, false, size, changed, sources, NULL);
}

// Count palette cells, including ANSI REP, independently of the renderer.
static size_t cells(const char *frame, char wanted) {
  size_t count = 0;
  char previous = 0;
  for (const char *p = frame; *p; ++p) {
    if (*p == '\033' && p[1] == '[') {
      p += 2;
      unsigned repeat = 0;
      while (*p >= '0' && *p <= '9')
        repeat = repeat * 10 + (unsigned)(*p++ - '0');
      while (*p && (*p < 0x40 || *p > 0x7e))
        ++p;
      REQUIRE((*p) != (0));
      if (*p == 'b' && previous == wanted)
        count += repeat ? repeat : 1;
    } else {
      count += *p == wanted;
      previous = *p;
    }
  }
  return count;
}

TEST_CASE("server_stream/latest_frame_is_reusable_and_source_bytes_remain_owned_by_buffer", "[server_stream] [contract]") {
  publish(0, 16, 8, 255);
  size_t size;
  bool changed;
  int sources;
  char *first = render("source-0", &size, &changed, &sources);
  REQUIRE((first) != nullptr);
  REQUIRE((size) == (strlen(first)));
  REQUIRE((sources) == (1));
  REQUIRE(changed);
  REQUIRE((cells(first, '@')) > (0));
  REQUIRE((cells(first, '#')) == (0));
  REQUIRE(!(atomic_load_bool(&g_client_manager.clients[0].incoming_video_buffer->new_frame_available)));
  char *second = render("source-0", &size, &changed, &sources);
  REQUIRE((second) != nullptr);
  REQUIRE(std::strcmp((second), (first)) == 0);
  REQUIRE(!(changed));
  const video_frame_t *retained = video_frame_peek_latest(g_client_manager.clients[0].incoming_video_buffer);
  REQUIRE((retained) != nullptr);
  REQUIRE((((const uint8_t *)retained->data)[8]) == (255));
  SAFE_FREE(first);
  SAFE_FREE(second);
}

TEST_CASE("server_stream/multiple_aspect_ratios_reach_grid_and_leaving_changes_layout", "[server_stream] [contract]") {
  publish(0, 32, 8, 0);
  publish(1, 8, 32, 255);
  REQUIRE(any_clients_sending_video());
  size_t size;
  bool changed;
  int sources;
  char *frame = render("source-2", &size, &changed, &sources);
  REQUIRE((frame) != nullptr);
  REQUIRE((sources) == (2));
  REQUIRE(changed);
  REQUIRE((cells(frame, '#')) > (0));
  REQUIRE((cells(frame, '@')) > (0));
  SAFE_FREE(frame);
  atomic_store_bool(&g_client_manager.clients[0].active, false);
  frame = render("source-2", &size, &changed, &sources);
  REQUIRE((frame) != nullptr);
  REQUIRE((sources) == (1));
  REQUIRE(changed);
  REQUIRE((cells(frame, '#')) == (0));
  REQUIRE((cells(frame, '@')) > (0));
  SAFE_FREE(frame);
  atomic_store_bool(&g_client_manager.clients[1].is_sending_video, false);
  REQUIRE(!(any_clients_sending_video()));
  REQUIRE((render("source-2", &size, &changed, &sources)) == nullptr);
  REQUIRE((size) == (0));
  REQUIRE((sources) == (0));
}

TEST_CASE("server_stream/malformed_or_unannounced_frames_are_not_rendered", "[server_stream] [contract]") {
  size_t size;
  bool changed;
  int sources;
  REQUIRE((render("source-0", &size, &changed, &sources)) == nullptr);
  REQUIRE((size) == (0));
  publish(0, 8, 8, 255);
  video_frame_buffer_t *buffer = g_client_manager.clients[0].incoming_video_buffer;
  const size_t lengths[] = {0, 7, 10, 11, 8 + 8 * 8 * 3 - 1, buffer->allocated_buffer_size + 1};
  for (size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); ++i) {
    buffer->front_buffer->size = lengths[i];
    REQUIRE((render("source-0", &size, &changed, &sources)) == nullptr);
    REQUIRE((size) == (0));
    REQUIRE((sources) == (0));
  }
  publish(0, 8, 8, 255);
  uint32_t invalid = HOST_TO_NET_U32(4097);
  memcpy(buffer->front_buffer->data, &invalid, sizeof(invalid));
  REQUIRE((render("source-0", &size, &changed, &sources)) == nullptr);
  REQUIRE((sources) == (0));
  publish(0, 8, 8, 255);
  atomic_store_bool(&g_should_exit, true);
  REQUIRE((render("source-0", &size, &changed, &sources)) == nullptr);
  REQUIRE((size) == (0));
}

TEST_CASE("server_stream/missing_capabilities_or_target_rejects_render_then_recovers", "[server_stream] [contract]") {
  publish(0, 8, 8, 255);
  size_t size;
  bool changed;
  int sources;
  REQUIRE((render("missing", &size, &changed, &sources)) == nullptr);
  REQUIRE((size) == (0));
  CLEAR_ERRNO_ALL();
  g_client_manager.clients[0].has_terminal_caps = false;
  REQUIRE((render("source-0", &size, &changed, &sources)) == nullptr);
  REQUIRE((size) == (0));
  CLEAR_ERRNO_ALL();
  g_client_manager.clients[0].has_terminal_caps = true;
  char *frame = render("source-0", &size, &changed, &sources);
  REQUIRE((frame) != nullptr);
  REQUIRE((cells(frame, '@')) > (0));
  SAFE_FREE(frame);
  REQUIRE((create_mixed_ascii_frame_for_client("source-0", 0, 24, false, &size, NULL, NULL, NULL)) == nullptr);
  REQUIRE((create_mixed_ascii_frame_for_client("source-0", 80, 0, false, &size, NULL, NULL, NULL)) == nullptr);
  REQUIRE((create_mixed_ascii_frame_for_client("source-0", 80, 24, false, NULL, NULL, NULL, NULL)) == nullptr);
}

static void stream_settings(client_info_t *client, bool rain, int width, int height, const char *palette) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = options_t_new();
  v->width = width;
  v->height = height;
  v->fps = 30;
  v->color_mode = TERM_COLOR_NONE;
  v->palette_type = PALETTE_CUSTOM;
  SAFE_STRNCPY(v->palette_custom, palette, sizeof(v->palette_custom));
  v->matrix_rain = rain;
  REQUIRE((acip_render_settings_prepare(&client->render_settings, v)) == (ASCIICHAT_OK));
  SAFE_FREE(v);
}
TEST_CASE("server_stream/matrix_rain_rebuilds_for_dimensions_and_is_released_when_disabled", "[server_stream] [contract]") {
  publish(0, 16, 8, 120);
  client_info_t *client = &g_client_manager.clients[0];
  size_t size;
  bool changed;
  int sources;
  stream_settings(client, true, 80, 24, "XX");
  char *frame = render("source-0", &size, &changed, &sources);
  REQUIRE((frame) != nullptr);
  REQUIRE((client->digital_rain) != nullptr);
  REQUIRE((client->digital_rain->num_columns) == (80));
  REQUIRE((client->digital_rain->num_rows) == (24));
  SAFE_FREE(frame);
  stream_settings(client, true, 40, 12, "XX");
  frame = render("source-0", &size, &changed, &sources);
  REQUIRE((frame) != nullptr);
  REQUIRE((client->digital_rain) != nullptr);
  REQUIRE((client->digital_rain->num_columns) == (40));
  REQUIRE((client->digital_rain->num_rows) == (12));
  SAFE_FREE(frame);
  stream_settings(client, false, 40, 12, "XX");
  frame = render("source-0", &size, &changed, &sources);
  REQUIRE((frame) != nullptr);
  REQUIRE((client->digital_rain) == nullptr);
  REQUIRE((session_frame_validate(frame, size, 40, 12)) == (ASCIICHAT_OK));
  SAFE_FREE(frame);
}
TEST_CASE("server_stream/wide_palette_normalization_keeps_every_row_at_the_recipients_display_width", "[server_stream] [contract]") {
  publish(0, 16, 8, 120);
  client_info_t *client = &g_client_manager.clients[0];
  stream_settings(client, false, 40, 12, "界é");
  size_t size;
  bool changed;
  int sources;
  char *frame = render("source-0", &size, &changed, &sources);
  REQUIRE((frame) != nullptr);
  REQUIRE((size) == (strlen(frame)));
  REQUIRE((session_frame_validate(frame, size, 40, 12)) == (ASCIICHAT_OK));
  REQUIRE((sources) == (1));
  SAFE_FREE(frame);
}
TEST_CASE("server_stream/three_sources_use_multiple_grid_cells_and_dimension_rejections_do_not_consume_frames", "[server_stream] [contract]") {
  publish(0, 16, 8, 0);
  publish(1, 8, 16, 255);
  publish(2, 16, 16, 120);
  size_t size;
  bool changed;
  int sources;
  char *frame = render("source-2", &size, &changed, &sources);
  REQUIRE((frame) != nullptr);
  REQUIRE((sources) == (3));
  REQUIRE((cells(frame, '#')) > (0));
  REQUIRE((cells(frame, '@')) > (0));
  SAFE_FREE(frame);
  video_frame_buffer_t *buffer = g_client_manager.clients[0].incoming_video_buffer;
  const uint32_t bad[][2] = {{0, 8}, {8, 0}, {4097, 8}, {8, 2161}};
  atomic_store_bool(&g_client_manager.clients[1].active, false);
  atomic_store_bool(&g_client_manager.clients[2].active, false);
  for (unsigned i = 0; i < ARRAY_SIZE(bad); ++i) {
    uint32_t dims[] = {HOST_TO_NET_U32(bad[i][0]), HOST_TO_NET_U32(bad[i][1])};
    memcpy(buffer->front_buffer->data, dims, sizeof(dims));
    atomic_store_bool(&buffer->new_frame_available, true);
    REQUIRE((render("source-0", &size, &changed, &sources)) == nullptr);
    REQUIRE(atomic_load_bool(&buffer->new_frame_available));
    REQUIRE((sources) == (0));
  }
  publish(0, 16, 8, 255);
  frame = render("source-0", &size, &changed, &sources);
  REQUIRE((frame) != nullptr);
  REQUIRE((sources) == (1));
  SAFE_FREE(frame);
}
