#include <criterion/criterion.h>
#include "server/stream.h"
#include <ascii-chat/options/rcu.h>
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
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert_eq(rwlock_init(&g_client_manager_rwlock, "stream-clients"), ASCIICHAT_OK);
  g_client_manager_rwlock_initialized = true;
  atomic_store_bool(&g_should_exit, false);
  for (int i = 0; i < 3; ++i) {
    client_info_t *client = &g_client_manager.clients[i];
    SAFE_SNPRINTF(client->client_id, sizeof(client->client_id), "source-%d", i);
    cr_assert_eq(mutex_init(&client->client_state_mutex, "stream-state"), ASCIICHAT_OK);
    options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
    *values = options_t_new();
    values->width = 80;
    values->height = 24;
    values->color_mode = TERM_COLOR_NONE;
    values->palette_type = PALETTE_CUSTOM;
    SAFE_STRNCPY(values->palette_custom, "#@", sizeof(values->palette_custom));
    cr_assert_eq(acip_render_settings_prepare(&client->render_settings, values), ASCIICHAT_OK);
    SAFE_FREE(values);
    client->has_terminal_caps = client->client_palette_initialized = true;
    client->incoming_video_buffer = video_frame_buffer_create(client->client_id);
    cr_assert_not_null(client->incoming_video_buffer);
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
TestSuite(server_stream, .init = setup, .fini = teardown);

static void publish(int source, uint32_t width, uint32_t height, unsigned char intensity) {
  client_info_t *client = &g_client_manager.clients[source];
  video_frame_t *frame = video_frame_begin_write(client->incoming_video_buffer);
  cr_assert_not_null(frame);
  size_t length = 8 + (size_t)width * height * 3;
  cr_assert_leq(length, client->incoming_video_buffer->allocated_buffer_size);
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
      cr_assert_neq(*p, 0, "Unterminated CSI in frame");
      if (*p == 'b' && previous == wanted)
        count += repeat ? repeat : 1;
    } else {
      count += *p == wanted;
      previous = *p;
    }
  }
  return count;
}

Test(server_stream, latest_frame_is_reusable_and_source_bytes_remain_owned_by_buffer) {
  publish(0, 16, 8, 255);
  size_t size;
  bool changed;
  int sources;
  char *first = render("source-0", &size, &changed, &sources);
  cr_assert_not_null(first);
  cr_assert_eq(size, strlen(first));
  cr_assert_eq(sources, 1);
  cr_assert(changed);
  cr_assert_gt(cells(first, '@'), 0);
  cr_assert_eq(cells(first, '#'), 0);
  cr_assert_not(atomic_load_bool(&g_client_manager.clients[0].incoming_video_buffer->new_frame_available));
  char *second = render("source-0", &size, &changed, &sources);
  cr_assert_not_null(second);
  cr_assert_str_eq(second, first);
  cr_assert_not(changed);
  const video_frame_t *retained = video_frame_peek_latest(g_client_manager.clients[0].incoming_video_buffer);
  cr_assert_not_null(retained);
  cr_assert_eq(((const uint8_t *)retained->data)[8], 255);
  SAFE_FREE(first);
  SAFE_FREE(second);
}

Test(server_stream, multiple_aspect_ratios_reach_grid_and_leaving_changes_layout) {
  publish(0, 32, 8, 0);
  publish(1, 8, 32, 255);
  cr_assert(any_clients_sending_video());
  size_t size;
  bool changed;
  int sources;
  char *frame = render("source-2", &size, &changed, &sources);
  cr_assert_not_null(frame);
  cr_assert_eq(sources, 2);
  cr_assert(changed);
  cr_assert_gt(cells(frame, '#'), 0);
  cr_assert_gt(cells(frame, '@'), 0);
  SAFE_FREE(frame);
  atomic_store_bool(&g_client_manager.clients[0].active, false);
  frame = render("source-2", &size, &changed, &sources);
  cr_assert_not_null(frame);
  cr_assert_eq(sources, 1);
  cr_assert(changed);
  cr_assert_eq(cells(frame, '#'), 0);
  cr_assert_gt(cells(frame, '@'), 0);
  SAFE_FREE(frame);
  atomic_store_bool(&g_client_manager.clients[1].is_sending_video, false);
  cr_assert_not(any_clients_sending_video());
  cr_assert_null(render("source-2", &size, &changed, &sources));
  cr_assert_eq(size, 0);
  cr_assert_eq(sources, 0);
}

Test(server_stream, malformed_or_unannounced_frames_are_not_rendered) {
  size_t size;
  bool changed;
  int sources;
  cr_assert_null(render("source-0", &size, &changed, &sources));
  cr_assert_eq(size, 0);
  publish(0, 8, 8, 255);
  video_frame_buffer_t *buffer = g_client_manager.clients[0].incoming_video_buffer;
  const size_t lengths[] = {0, 7, 10, 11, 8 + 8 * 8 * 3 - 1, buffer->allocated_buffer_size + 1};
  for (size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); ++i) {
    buffer->front_buffer->size = lengths[i];
    cr_assert_null(render("source-0", &size, &changed, &sources));
    cr_assert_eq(size, 0, "Malformed size %zu escaped validation", lengths[i]);
    cr_assert_eq(sources, 0);
  }
  publish(0, 8, 8, 255);
  uint32_t invalid = HOST_TO_NET_U32(4097);
  memcpy(buffer->front_buffer->data, &invalid, sizeof(invalid));
  cr_assert_null(render("source-0", &size, &changed, &sources));
  cr_assert_eq(sources, 0);
  publish(0, 8, 8, 255);
  atomic_store_bool(&g_should_exit, true);
  cr_assert_null(render("source-0", &size, &changed, &sources));
  cr_assert_eq(size, 0);
}

Test(server_stream, missing_capabilities_or_target_rejects_render_then_recovers) {
  publish(0, 8, 8, 255);
  size_t size;
  bool changed;
  int sources;
  cr_assert_null(render("missing", &size, &changed, &sources));
  cr_assert_eq(size, 0);
  CLEAR_ERRNO_ALL();
  g_client_manager.clients[0].has_terminal_caps = false;
  cr_assert_null(render("source-0", &size, &changed, &sources));
  cr_assert_eq(size, 0);
  CLEAR_ERRNO_ALL();
  g_client_manager.clients[0].has_terminal_caps = true;
  char *frame = render("source-0", &size, &changed, &sources);
  cr_assert_not_null(frame);
  cr_assert_gt(cells(frame, '@'), 0);
  SAFE_FREE(frame);
  cr_assert_null(create_mixed_ascii_frame_for_client("source-0", 0, 24, false, &size, NULL, NULL, NULL));
  cr_assert_null(create_mixed_ascii_frame_for_client("source-0", 80, 0, false, &size, NULL, NULL, NULL));
  cr_assert_null(create_mixed_ascii_frame_for_client("source-0", 80, 24, false, NULL, NULL, NULL, NULL));
}

static void stream_settings(client_info_t *client, bool rain, int width, int height, const char *palette) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = options_t_new();
  v->width = width;
  v->height = height;
  v->color_mode = TERM_COLOR_NONE;
  v->palette_type = PALETTE_CUSTOM;
  SAFE_STRNCPY(v->palette_custom, palette, sizeof(v->palette_custom));
  v->matrix_rain = rain;
  cr_assert_eq(acip_render_settings_prepare(&client->render_settings, v), ASCIICHAT_OK);
  SAFE_FREE(v);
}
Test(server_stream, matrix_rain_rebuilds_for_dimensions_and_is_released_when_disabled) {
  publish(0, 16, 8, 120);
  client_info_t *client = &g_client_manager.clients[0];
  size_t size;
  bool changed;
  int sources;
  stream_settings(client, true, 80, 24, "XX");
  char *frame = render("source-0", &size, &changed, &sources);
  cr_assert_not_null(frame);
  cr_assert_not_null(client->digital_rain);
  cr_assert_eq(client->digital_rain->num_columns, 80);
  cr_assert_eq(client->digital_rain->num_rows, 24);
  SAFE_FREE(frame);
  stream_settings(client, true, 40, 12, "XX");
  frame = render("source-0", &size, &changed, &sources);
  cr_assert_not_null(frame);
  cr_assert_not_null(client->digital_rain);
  cr_assert_eq(client->digital_rain->num_columns, 40);
  cr_assert_eq(client->digital_rain->num_rows, 12);
  SAFE_FREE(frame);
  stream_settings(client, false, 40, 12, "XX");
  frame = render("source-0", &size, &changed, &sources);
  cr_assert_not_null(frame);
  cr_assert_null(client->digital_rain);
  cr_assert_eq(session_frame_validate(frame, size, 40, 12), ASCIICHAT_OK);
  SAFE_FREE(frame);
}
Test(server_stream, wide_palette_normalization_keeps_every_row_at_the_recipients_display_width) {
  publish(0, 16, 8, 120);
  client_info_t *client = &g_client_manager.clients[0];
  stream_settings(client, false, 40, 12, "界é");
  size_t size;
  bool changed;
  int sources;
  char *frame = render("source-0", &size, &changed, &sources);
  cr_assert_not_null(frame);
  cr_assert_eq(size, strlen(frame));
  cr_assert_eq(session_frame_validate(frame, size, 40, 12), ASCIICHAT_OK);
  cr_assert_eq(sources, 1);
  SAFE_FREE(frame);
}
Test(server_stream, three_sources_use_multiple_grid_cells_and_dimension_rejections_do_not_consume_frames) {
  publish(0, 16, 8, 0);
  publish(1, 8, 16, 255);
  publish(2, 16, 16, 120);
  size_t size;
  bool changed;
  int sources;
  char *frame = render("source-2", &size, &changed, &sources);
  cr_assert_not_null(frame);
  cr_assert_eq(sources, 3);
  cr_assert_gt(cells(frame, '#'), 0);
  cr_assert_gt(cells(frame, '@'), 0);
  SAFE_FREE(frame);
  video_frame_buffer_t *buffer = g_client_manager.clients[0].incoming_video_buffer;
  const uint32_t bad[][2] = {{0, 8}, {8, 0}, {4097, 8}, {8, 2161}};
  atomic_store_bool(&g_client_manager.clients[1].active, false);
  atomic_store_bool(&g_client_manager.clients[2].active, false);
  for (unsigned i = 0; i < ARRAY_SIZE(bad); ++i) {
    uint32_t dims[] = {HOST_TO_NET_U32(bad[i][0]), HOST_TO_NET_U32(bad[i][1])};
    memcpy(buffer->front_buffer->data, dims, sizeof(dims));
    atomic_store_bool(&buffer->new_frame_available, true);
    cr_assert_null(render("source-0", &size, &changed, &sources));
    cr_assert(atomic_load_bool(&buffer->new_frame_available));
    cr_assert_eq(sources, 0);
  }
  publish(0, 16, 8, 255);
  frame = render("source-0", &size, &changed, &sources);
  cr_assert_not_null(frame);
  cr_assert_eq(sources, 1);
  SAFE_FREE(frame);
}
