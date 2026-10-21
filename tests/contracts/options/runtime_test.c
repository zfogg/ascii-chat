#include "../worker_assert.h"
#include <criterion/criterion.h>
#include <ascii-chat/options/runtime.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/acip/settings.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/common.h>
#include <ascii-chat/atomic.h>
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
    cr_assert_worker((options_toggle(field)) == (ASCIICHAT_OK));
  return NULL;
}

static void test_flip_defaults(void) {
  cr_assert(GET_OPTION(flip_x) && !GET_OPTION(flip_x_explicit));
  for (int i = 0; i < 3; ++i) {
    cr_assert_eq(options_set_bool("media_input", true), ASCIICHAT_OK);
    cr_assert(!GET_OPTION(flip_x) && !GET_OPTION(flip_x_explicit));
    cr_assert_eq(options_set_bool("media_input", false), ASCIICHAT_OK);
    cr_assert(GET_OPTION(flip_x));
    cr_assert_eq(options_set_bool("waveform", true), ASCIICHAT_OK);
    cr_assert(!GET_OPTION(flip_x));
    cr_assert_eq(options_set_bool("fft", true), ASCIICHAT_OK);
    cr_assert(!GET_OPTION(flip_x));
    cr_assert_eq(options_set_bool("fft", false), ASCIICHAT_OK);
    cr_assert(GET_OPTION(flip_x));
  }
  options_edit_t invalid[] = {{.name = "flip_x", .type = OPTIONS_VALUE_BOOL, .value.boolean = false},
                              {.name = "fps", .type = OPTIONS_VALUE_INT, .value.integer = 0}};
  options_patch_t patch = {.edits = invalid, .count = 2, .expected_revision = OPTIONS_REVISION_ANY};
  cr_assert_neq(options_apply_patch(&patch, NULL), ASCIICHAT_OK);
  CLEAR_ERRNO();
  cr_assert(GET_OPTION(flip_x) && !GET_OPTION(flip_x_explicit));
  // An explicit assignment of the current value still selects an orientation.
  cr_assert_eq(options_set_bool("flip_x", true), ASCIICHAT_OK);
  cr_assert(GET_OPTION(flip_x_explicit));
  cr_assert_eq(options_set_bool("media_input", true), ASCIICHAT_OK);
  cr_assert_eq(options_set_bool("waveform", true), ASCIICHAT_OK);
  cr_assert(GET_OPTION(flip_x));
  cr_assert_eq(options_set_bool("flip_x", false), ASCIICHAT_OK);
  cr_assert_eq(options_set_bool("media_input", false), ASCIICHAT_OK);
  cr_assert_eq(options_set_bool("waveform", false), ASCIICHAT_OK);
  cr_assert(!GET_OPTION(flip_x));
  cr_assert_neq(options_set_bool("flip_x_explicit", false), ASCIICHAT_OK);
  CLEAR_ERRNO();
  cr_assert(GET_OPTION(flip_x_explicit));
  options_t defaults = options_t_new();
  cr_assert_eq(options_state_set(&defaults), ASCIICHAT_OK);
  cr_assert(!GET_OPTION(flip_x) && GET_OPTION(flip_x_explicit));
  options_state_destroy();
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert(GET_OPTION(flip_x) && !GET_OPTION(flip_x_explicit));
}

static void test_transactions(void) {
  cr_assert_eq(options_set_int("color_mode", TERM_COLOR_AUTO), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("width", 80), ASCIICHAT_OK);
  const options_t *borrowed = options_get();
  uint64_t before = options_revision();
  options_edit_t edits[] = {{.name = "width", .type = OPTIONS_VALUE_INT, .value.integer = 120},
                            {.name = "fps", .type = OPTIONS_VALUE_INT, .value.integer = 0}};
  options_patch_t patch = {.edits = edits, .count = 2, .expected_revision = OPTIONS_REVISION_ANY};
  cr_assert_neq(options_apply_patch(&patch, NULL), ASCIICHAT_OK);
  cr_assert(GET_OPTION(width) == 80 && options_revision() == before);
  CLEAR_ERRNO();
  edits[1].value.integer = 30;
  options_patch_result_t result;
  cr_assert(options_apply_patch(&patch, &result) == ASCIICHAT_OK && result.changed && result.revision == before + 1);
  patch.expected_revision = before;
  edits[0].value.integer = 90;
  cr_assert(options_apply_patch(&patch, NULL) == ERROR_INVALID_STATE && GET_OPTION(width) == 120);
  CLEAR_ERRNO();
  cr_assert_neq(options_set_double("speakers_volume", NAN), ASCIICHAT_OK);
  CLEAR_ERRNO();
  cr_assert_neq(options_apply_json("{\"fps\":30,\"fps\":60}", 19, OPTIONS_REVISION_ANY, OPTIONS_ORIGIN_LOCAL, NULL),
                ASCIICHAT_OK);
  CLEAR_ERRNO();
  const char nul_key[] = "{\"width\\u0000hidden\":99}";
  cr_assert_neq(options_apply_json(nul_key, sizeof(nul_key) - 1, OPTIONS_REVISION_ANY, OPTIONS_ORIGIN_LOCAL, NULL),
                ASCIICHAT_OK);
  cr_assert_eq(GET_OPTION(width), 120);
  CLEAR_ERRNO();
  cr_assert_eq(options_set_bool("flip_x", false), ASCIICHAT_OK);
  cr_assert_eq(options_set_bool("flip_y", false), ASCIICHAT_OK);
  before = options_revision();
  asciichat_thread_t threads[4];
  for (int i = 0; i < 4; ++i)
    cr_assert_eq(asciichat_thread_create(&threads[i], "settings_writer", writer, i % 2 ? "flip_x" : "flip_y"),
                 ASCIICHAT_OK);
  while (atomic_load_int(&ready) != 4)
    time_sleep_ns(1000);
  atomic_store_int(&start, 1);
  for (int i = 0; i < 4; ++i)
    cr_assert_eq(asciichat_thread_join(&threads[i], NULL), ASCIICHAT_OK);
  cr_assert(!GET_OPTION(flip_x) && !GET_OPTION(flip_y));
  cr_assert_eq(options_revision(), before + 2004);
  cr_assert_eq(borrowed->width, 80);
  cr_assert_eq(options_set_bool("matrix_rain", true), ASCIICHAT_OK);
  cr_assert_eq(options_set_bool("fft", true), ASCIICHAT_OK);
  cr_assert(GET_OPTION(fft) && !GET_OPTION(matrix_rain) && !GET_OPTION(waveform));
  options_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot), options_snapshot_t *);
  cr_assert(options_read_snapshot(snapshot) == ASCIICHAT_OK && snapshot->revision == options_revision());
  cr_assert(options_set_bool("fft", true) == ASCIICHAT_OK && snapshot->revision == options_revision());
  SAFE_FREE(snapshot);
  before = options_revision();
  cr_assert(options_retired_snapshot_count() > 0);
  options_state_destroy();
  cr_assert(options_state_init() == ASCIICHAT_OK && options_revision() > before);
  patch.expected_revision = before;
  cr_assert_eq(options_apply_patch(&patch, NULL), ERROR_INVALID_STATE);
  CLEAR_ERRNO();
}

static acip_settings_header_t request(acip_render_settings_t *state, const char *json, uint32_t expected) {
  uint8_t input[ACIP_SETTINGS_MAX_SIZE], output[ACIP_SETTINGS_MAX_SIZE];
  size_t input_length, output_length;
  acip_settings_header_t header = {.schema = 1, .request = 17, .expected_revision = expected};
  cr_assert_eq(acip_settings_encode(&header, json, input, sizeof(input), &input_length), ASCIICHAT_OK);
  cr_assert_eq(acip_settings_request(state, input, input_length, output, sizeof(output), &output_length), ASCIICHAT_OK);
  const char *effective;
  size_t effective_length;
  cr_assert_eq(acip_settings_decode(output, output_length, &header, &effective, &effective_length), ASCIICHAT_OK);
  cr_assert(header.request == 17 && effective_length > 0);
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
    cr_assert_eq(acip_render_settings_prepare(&clients[i], values), ASCIICHAT_OK);
  uint64_t local_revision = options_revision();
  cr_assert_eq(
      request(&clients[0],
              "{\"width\":55,\"color_filter\":3,\"render_mode\":2,\"palette_type\":6,\"palette_custom\":\" .+#\"}",
              UINT32_MAX)
          .status,
      0);
  cr_assert(clients[0].width == 55 && clients[0].caps.palette_type == PALETTE_CUSTOM &&
            !strcmp(clients[0].palette, " .+#"));
  cr_assert(clients[1].width == 80 && clients[1].caps.palette_type == PALETTE_STANDARD);
  acip_render_settings_t previous = clients[0];
  cr_assert_neq(request(&clients[0], "{\"width\":90,\"password\":\"secret\"}", UINT32_MAX).status, 0);
  cr_assert(!memcmp(&previous, &clients[0], sizeof(previous)));
  cr_assert_neq(request(&clients[0], "{\"width\":90,\"fps\":0}", UINT32_MAX).status, 0);
  cr_assert(!memcmp(&previous, &clients[0], sizeof(previous)));
  cr_assert_eq(request(&clients[0], "{\"fps\":25}", 0).status, ERROR_INVALID_STATE);
  cr_assert(!memcmp(&previous, &clients[0], sizeof(previous)));
  cr_assert_eq(options_revision(), local_revision);
  SAFE_FREE(values);
}

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
}

static void teardown(void) {
  options_state_destroy();
  cr_assert_eq(options_retired_snapshot_count(), 0);
  log_destroy();
}

TestSuite(runtime_settings, .init = setup, .fini = teardown, .timeout = 30);

Test(runtime_settings, flip_defaults) {
  test_flip_defaults();
}

Test(runtime_settings, transactions) {
  test_transactions();
}

Test(runtime_settings, remote) {
  test_remote();
}
