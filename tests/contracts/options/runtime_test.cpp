#include "../worker_assert.h"
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/options/runtime.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/acip/settings.h>
#include <libplatform/thread.h>
#include <ascii-chat/common.h>
#include <libplatform/atomic.h>
#include <math.h>
#include <ascii-chat/util/time.h>
#include <string.h>

static atomic_t ready, start;
static void *writer(void *argument) {
  const char *field = argument;
  atomic_fetch_add_int(&ready, 1);
  while (!atomic_load_int(&start))
    time_sleep_ns(1000);
  for (int i = 0; i < 501; ++i)
    ASCIICHAT_WORKER_ASSERT((options_toggle(field)) == (ASCIICHAT_OK));
  return NULL;
}

static void test_flip_defaults(void) {
  ASCIICHAT_REQUIRE(GET_OPTION(flip_x) && !GET_OPTION(flip_x_explicit));
  for (int i = 0; i < 3; ++i) {
    ASCIICHAT_REQUIRE_EQ(options_set_bool("media_input", true), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(!GET_OPTION(flip_x) && !GET_OPTION(flip_x_explicit));
    ASCIICHAT_REQUIRE_EQ(options_set_bool("media_input", false), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(GET_OPTION(flip_x));
    ASCIICHAT_REQUIRE_EQ(options_set_bool("waveform", true), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(!GET_OPTION(flip_x));
    ASCIICHAT_REQUIRE_EQ(options_set_bool("fft", true), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(!GET_OPTION(flip_x));
    ASCIICHAT_REQUIRE_EQ(options_set_bool("fft", false), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(GET_OPTION(flip_x));
  }
  options_edit_t invalid[] = {{.name = "flip_x", .type = OPTIONS_VALUE_BOOL, .value.boolean = false},
                              {.name = "fps", .type = OPTIONS_VALUE_INT, .value.integer = -1}};
  options_patch_t patch = {.edits = invalid, .count = 2, .expected_revision = OPTIONS_REVISION_ANY};
  ASCIICHAT_REQUIRE_NE(options_apply_patch(&patch, NULL), ASCIICHAT_OK);
  CLEAR_ERRNO();
  ASCIICHAT_REQUIRE(GET_OPTION(flip_x) && !GET_OPTION(flip_x_explicit));
  // An explicit assignment of the current value still selects an orientation.
  ASCIICHAT_REQUIRE_EQ(options_set_bool("flip_x", true), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(GET_OPTION(flip_x_explicit));
  ASCIICHAT_REQUIRE_EQ(options_set_bool("media_input", true), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_bool("waveform", true), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(GET_OPTION(flip_x));
  ASCIICHAT_REQUIRE_EQ(options_set_bool("flip_x", false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_bool("media_input", false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_bool("waveform", false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(!GET_OPTION(flip_x));
  ASCIICHAT_REQUIRE_NE(options_set_bool("flip_x_explicit", false), ASCIICHAT_OK);
  CLEAR_ERRNO();
  ASCIICHAT_REQUIRE(GET_OPTION(flip_x_explicit));
  options_t defaults = options_t_new();
  ASCIICHAT_REQUIRE_EQ(options_state_set(&defaults), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(!GET_OPTION(flip_x) && GET_OPTION(flip_x_explicit));
  options_state_destroy();
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(GET_OPTION(flip_x) && !GET_OPTION(flip_x_explicit));
}

static void test_transactions(void) {
  ASCIICHAT_REQUIRE_EQ(options_set_int("renderer", ASCII_RENDERER_GLYPH), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(GET_OPTION(renderer) == ASCII_RENDERER_GLYPH);
  uint64_t palette_revision = options_revision();
  palette_type_t previous_palette = GET_OPTION(palette_type);
  ASCIICHAT_REQUIRE_NE(options_set_int("palette_type", PALETTE_DIGITAL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(GET_OPTION(palette_type), previous_palette);
  ASCIICHAT_REQUIRE_EQ(options_revision(), palette_revision);
  CLEAR_ERRNO();
  ASCIICHAT_REQUIRE_EQ(options_set_int("renderer", ASCII_RENDERER_LUMINANCE), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(GET_OPTION(renderer), ASCII_RENDERER_LUMINANCE);
  uint64_t renderer_revision = options_revision();
  ASCIICHAT_REQUIRE_NE(options_set_int("renderer", 99), ASCIICHAT_OK);
  CLEAR_ERRNO();
  ASCIICHAT_REQUIRE_EQ(GET_OPTION(renderer), ASCII_RENDERER_LUMINANCE);
  ASCIICHAT_REQUIRE_EQ(options_revision(), renderer_revision);
  ASCIICHAT_REQUIRE_EQ(options_set_int("color_mode", TERM_COLOR_AUTO), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_int("width", 80), ASCIICHAT_OK);
  const options_t *borrowed = options_get();
  uint64_t before = options_revision();
  options_edit_t edits[] = {{.name = "width", .type = OPTIONS_VALUE_INT, .value.integer = 120},
                            {.name = "fps", .type = OPTIONS_VALUE_INT, .value.integer = -1}};
  options_patch_t patch = {.edits = edits, .count = 2, .expected_revision = OPTIONS_REVISION_ANY};
  ASCIICHAT_REQUIRE_NE(options_apply_patch(&patch, NULL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(GET_OPTION(width) == 80 && options_revision() == before);
  CLEAR_ERRNO();
  edits[1].value.integer = 30;
  options_patch_result_t result;
  ASCIICHAT_REQUIRE(options_apply_patch(&patch, &result) == ASCIICHAT_OK && result.changed && result.revision == before + 1);
  patch.expected_revision = before;
  edits[0].value.integer = 90;
  ASCIICHAT_REQUIRE(options_apply_patch(&patch, NULL) == ERROR_INVALID_STATE && GET_OPTION(width) == 120);
  CLEAR_ERRNO();
  ASCIICHAT_REQUIRE_NE(options_set_double("speakers_volume", NAN), ASCIICHAT_OK);
  CLEAR_ERRNO();
  ASCIICHAT_REQUIRE_NE(options_apply_json("{\"fps\":30,\"fps\":60}", 19, OPTIONS_REVISION_ANY, OPTIONS_ORIGIN_LOCAL, NULL),
                ASCIICHAT_OK);
  CLEAR_ERRNO();
  const char nul_key[] = "{\"width\\u0000hidden\":99}";
  ASCIICHAT_REQUIRE_NE(options_apply_json(nul_key, sizeof(nul_key) - 1, OPTIONS_REVISION_ANY, OPTIONS_ORIGIN_LOCAL, NULL),
                ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(GET_OPTION(width), 120);
  CLEAR_ERRNO();
  ASCIICHAT_REQUIRE_EQ(options_set_bool("flip_x", false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_bool("flip_y", false), ASCIICHAT_OK);
  before = options_revision();
  platform_thread_t threads[4];
  for (int i = 0; i < 4; ++i)
    ASCIICHAT_REQUIRE_EQ(platform_thread_create(&threads[i], "settings_writer", writer, i % 2 ? "flip_x" : "flip_y"), ASCIICHAT_OK);
  while (atomic_load_int(&ready) != 4)
    time_sleep_ns(1000);
  atomic_store_int(&start, 1);
  for (int i = 0; i < 4; ++i)
    ASCIICHAT_REQUIRE_EQ(thread_join(&threads[i], NULL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(!GET_OPTION(flip_x) && !GET_OPTION(flip_y));
  ASCIICHAT_REQUIRE_EQ(options_revision(), before + 2004);
  ASCIICHAT_REQUIRE_EQ(borrowed->width, 80);
  ASCIICHAT_REQUIRE_EQ(options_set_bool("matrix_rain", true), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_bool("fft", true), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(GET_OPTION(fft) && !GET_OPTION(matrix_rain) && !GET_OPTION(waveform));
  options_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot), options_snapshot_t *);
  ASCIICHAT_REQUIRE(options_read_snapshot(snapshot) == ASCIICHAT_OK && snapshot->revision == options_revision());
  ASCIICHAT_REQUIRE(options_set_bool("fft", true) == ASCIICHAT_OK && snapshot->revision == options_revision());
  SAFE_FREE(snapshot);
  before = options_revision();
  ASCIICHAT_REQUIRE(options_retired_snapshot_count() > 0);
  options_state_destroy();
  ASCIICHAT_REQUIRE(options_state_init() == ASCIICHAT_OK && options_revision() > before);
  patch.expected_revision = before;
  ASCIICHAT_REQUIRE_EQ(options_apply_patch(&patch, NULL), ERROR_INVALID_STATE);
  CLEAR_ERRNO();
}

static acip_settings_header_t request(acip_render_settings_t *state, const char *json, uint32_t expected) {
  uint8_t input[ACIP_SETTINGS_MAX_SIZE], output[ACIP_SETTINGS_MAX_SIZE];
  size_t input_length, output_length;
  acip_settings_header_t header = {.schema = 1, .request = 17, .expected_revision = expected};
  ASCIICHAT_REQUIRE_EQ(acip_settings_encode(&header, json, input, sizeof(input), &input_length), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(acip_settings_request(state, input, input_length, output, sizeof(output), &output_length), ASCIICHAT_OK);
  const char *effective;
  size_t effective_length;
  ASCIICHAT_REQUIRE_EQ(acip_settings_decode(output, output_length, &header, &effective, &effective_length), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(header.request == 17 && effective_length > 0);
  return header;
}

static void test_remote(void) {
  acip_render_settings_t clients[2] = {{0}};
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  *values = options_t_new();
  values->width = 80;
  values->height = 24;
  values->fps = 30;
  values->color_mode = TERM_COLOR_TRUECOLOR;
  values->palette_type = PALETTE_STANDARD;
  for (int i = 0; i < 2; ++i)
    ASCIICHAT_REQUIRE_EQ(acip_render_settings_prepare(&clients[i], values), ASCIICHAT_OK);
  uint64_t local_revision = options_revision();
  ASCIICHAT_REQUIRE_EQ(
      request(&clients[0],
              "{\"width\":55,\"color_filter\":3,\"render_mode\":2,\"palette_type\":6,\"palette_custom\":\" .+#\"}",
              UINT32_MAX)
          .status,
      0);
  ASCIICHAT_REQUIRE(clients[0].width == 55 && clients[0].caps.palette_type == PALETTE_CUSTOM &&
            !strcmp(clients[0].palette, " .+#"));
  ASCIICHAT_REQUIRE(clients[1].width == 80 && clients[1].caps.palette_type == PALETTE_STANDARD);
  ASCIICHAT_REQUIRE_EQ(request(&clients[1], "{\"renderer\":1,\"palette_explicit\":false}", UINT32_MAX).status, 0);
  ASCIICHAT_REQUIRE_EQ(clients[1].caps.text_renderer, ASCII_RENDERER_GLYPH);
  ASCIICHAT_REQUIRE_EQ(clients[1].palette_length, 95);
  ASCIICHAT_REQUIRE_EQ(clients[0].caps.text_renderer, ASCII_RENDERER_LUMINANCE);
  ASCIICHAT_REQUIRE_EQ(request(&clients[1], "{\"renderer\":1,\"palette_type\":6,\"palette_custom\":\"@#\"}", UINT32_MAX).status,
               0);
  ASCIICHAT_REQUIRE(clients[1].caps.palette_explicit && !strcmp(clients[1].palette, "@#"));
  acip_render_settings_t glyph_previous = clients[1];
  ASCIICHAT_REQUIRE_NE(request(&clients[1], "{\"palette_custom\":\"\\u2603\"}", UINT32_MAX).status, 0);
  ASCIICHAT_REQUIRE(!memcmp(&glyph_previous, &clients[1], sizeof(glyph_previous)));
  acip_render_settings_t previous = clients[0];
  ASCIICHAT_REQUIRE_NE(request(&clients[0], "{\"width\":90,\"password\":\"secret\"}", UINT32_MAX).status, 0);
  ASCIICHAT_REQUIRE(!memcmp(&previous, &clients[0], sizeof(previous)));
  ASCIICHAT_REQUIRE_NE(request(&clients[0], "{\"width\":90,\"fps\":0}", UINT32_MAX).status, 0);
  ASCIICHAT_REQUIRE(!memcmp(&previous, &clients[0], sizeof(previous)));
  ASCIICHAT_REQUIRE_EQ(request(&clients[0], "{\"fps\":25}", 0).status, ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE(!memcmp(&previous, &clients[0], sizeof(previous)));
  ASCIICHAT_REQUIRE_EQ(options_revision(), local_revision);
  SAFE_FREE(values);
}

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
}

static void teardown(void) {
  options_state_destroy();
  ASCIICHAT_REQUIRE_EQ(options_retired_snapshot_count(), 0);
  log_destroy();
}
TEST_CASE("runtime_settings/flip_defaults", "[runtime_settings]") {
  test_flip_defaults();
}

TEST_CASE("runtime_settings/transactions", "[runtime_settings]") {
  test_transactions();
}

TEST_CASE("runtime_settings/remote", "[runtime_settings]") {
  test_remote();
}
