#include <criterion/criterion.h>
#include "../../../src/common/session/host.h"
#include "../../../src/common/session/audio.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/ringbuffer.h>
static uint64_t fault_clock_us = 1000000;
static uint64_t fault_clock_ns(void) {
  return fault_clock_us * 1000;
}
static bool fail_image, fail_ring, fail_mutex, fail_thread, fail_join;
static bool interactive;
static bool fault_terminal(void) {
  return interactive;
}
static unsigned admission_calls, thread_starts, thread_joins;
static image_t *fault_image(int width, int height) {
  if (fail_image) {
    SET_ERRNO(ERROR_MEMORY, "Injected host image allocation failure");
    return NULL;
  }
  return image_new(width, height);
}
static ringbuffer_t *fault_ring(size_t element, size_t capacity) {
  return fail_ring ? NULL : ringbuffer_create(element, capacity);
}
static int fault_mutex(mutex_t *mutex, const char *name) {
  return fail_mutex ? ERROR_THREAD : mutex_init(mutex, name);
}
static int fault_thread(asciichat_thread_t *thread, const char *name, void *(*fn)(void *), void *arg) {
  (void)thread;
  (void)name;
  (void)fn;
  (void)arg;
  ++thread_starts;
  return fail_thread ? -1 : 0;
}
static int fault_join(asciichat_thread_t *thread, void **result) {
  (void)thread;
  (void)result;
  ++thread_joins;
  return fail_join ? ERROR_THREAD : ASCIICHAT_OK;
}
#define terminal_is_interactive fault_terminal
#define terminal_is_stdout_tty fault_terminal
#define time_get_ns fault_clock_ns
#define image_new fault_image
#define ringbuffer_create fault_ring
#define mutex_init fault_mutex
#define acthread_create fault_thread
#define thread_join fault_join
#include "../../../src/common/session/host.c"
#undef terminal_is_interactive
#undef terminal_is_stdout_tty
#undef time_get_ns
#undef image_new
#undef ringbuffer_create
#undef mutex_init
#undef acthread_create
#undef thread_join
static session_host_t *owner;
static void admitted(session_host_t *host, uint32_t id, void *data) {
  cr_assert_eq(host, owner);
  cr_assert_eq(data, &admission_calls);
  cr_assert_neq(id, 0);
  ++admission_calls;
}
static session_host_config_t configuration(void) {
  return (session_host_config_t){.max_clients = 1,
                                 .ipv4_address = "127.0.0.1",
                                 .ipv6_address = "::1",
                                 .key_path = "fixture-key",
                                 .password = "fixture-password",
                                 .callbacks = {.on_client_join = admitted},
                                 .user_data = &admission_calls};
}
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
}
static void teardown(void) {
  fail_image = fail_ring = fail_mutex = fail_thread = fail_join = false;
  if (owner) {
    owner->render_thread_running = false;
    cr_assert_eq(session_host_destroy(owner), ASCIICHAT_OK);
  }
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(host_faults, .init = setup, .fini = teardown, .timeout = 20);
static void create(void) {
  session_host_config_t config = configuration();
  owner = session_host_create(&config);
  cr_assert_not_null(owner);
}
Test(host_faults, mutex_failure_does_not_publish_host_and_configuration_strings_are_owned) {
  session_host_config_t config = configuration();
  fail_mutex = true;
  cr_assert_null(session_host_create(&config));
  fail_mutex = false;
  CLEAR_ERRNO_ALL();
  create();
  cr_assert_str_eq(owner->ipv4_address, "127.0.0.1");
  cr_assert_str_eq(owner->ipv6_address, "::1");
  cr_assert_str_eq(owner->key_path, "fixture-key");
  cr_assert_str_eq(owner->password, "fixture-password");
}
Test(host_faults, memory_participant_allocation_failure_does_not_consume_capacity_or_publish_admission) {
  create();
  for (unsigned failure = 0; failure < 2; ++failure) {
    fail_image = failure == 0;
    fail_ring = failure == 1;
    cr_assert_eq(session_host_add_memory_participant(owner), 0);
    cr_assert_eq(session_host_get_client_count(owner), 0);
    cr_assert_eq(admission_calls, 0);
    cr_assert_not(owner->clients[0].active);
    cr_assert_null(owner->clients[0].incoming_video);
    cr_assert_null(owner->clients[0].incoming_audio);
    CLEAR_ERRNO_ALL();
  }
  fail_image = fail_ring = false;
  cr_assert_neq(session_host_add_memory_participant(owner), 0);
  cr_assert_eq(admission_calls, 1);
}
Test(host_faults, network_participant_allocation_failure_keeps_caller_transport_and_capacity) {
  create();
  for (unsigned failure = 0; failure < 2; ++failure) {
    fail_image = failure == 0;
    fail_ring = failure == 1;
    cr_assert_eq(session_host_add_client(owner, NULL, ACIP_TRANSPORT_BORROWED, NULL, 0), 0);
    cr_assert_eq(session_host_get_client_count(owner), 0);
    cr_assert_eq(admission_calls, 0);
    cr_assert_null(owner->clients[0].incoming_video);
    cr_assert_null(owner->clients[0].incoming_audio);
    CLEAR_ERRNO_ALL();
  }
  fail_image = fail_ring = false;
  cr_assert_neq(session_host_add_client(owner, NULL, ACIP_TRANSPORT_BORROWED, NULL, 0), 0);
  cr_assert_eq(admission_calls, 1);
}
Test(host_faults, failed_frame_resize_preserves_the_last_committed_video_buffer_and_pixels) {
  create();
  uint32_t id = session_host_add_memory_participant(owner);
  cr_assert_neq(id, 0);
  image_t *image = image_new(2, 2);
  cr_assert_not_null(image);
  memset(image->pixels, 91, 12);
  cr_assert_eq(session_host_inject_frame(owner, id, image), ASCIICHAT_OK);
  image_destroy(image);
  image_t *previous = owner->clients[0].incoming_video;
  image = image_new(3, 3);
  cr_assert_not_null(image);
  fail_image = true;
  cr_assert_eq(session_host_inject_frame(owner, id, image), ERROR_MEMORY);
  cr_assert_eq(owner->clients[0].incoming_video, previous);
  cr_assert_eq(previous->w, 2);
  cr_assert_eq(((uint8_t *)previous->pixels)[0], 91);
  image_destroy(image);
}
Test(host_faults, malformed_injected_frame_is_rejected_before_replacing_the_committed_buffer) {
  create();
  uint32_t id = session_host_add_memory_participant(owner);
  cr_assert_neq(id, 0);
  image_t *previous = owner->clients[0].incoming_video;
  image_t invalid = {.w = 2, .h = 2, .pixels = NULL};
  cr_assert_eq(session_host_inject_frame(owner, id, &invalid), ERROR_INVALID_PARAM);
  cr_assert_eq(owner->clients[0].incoming_video, previous);
  rgb_pixel_t pixel = {0};
  const int dimensions[][2] = {{0, 1}, {1, 0}, {-1, 1}, {1, -1}, {INT_MAX, INT_MAX}};
  for (unsigned i = 0; i < ARRAY_SIZE(dimensions); ++i) {
    invalid = (image_t){.w = dimensions[i][0], .h = dimensions[i][1], .pixels = &pixel};
    cr_assert_eq(session_host_inject_frame(owner, id, &invalid), ERROR_INVALID_PARAM);
    cr_assert_eq(owner->clients[0].incoming_video, previous);
    CLEAR_ERRNO_ALL();
  }
}
Test(host_faults, render_start_failure_is_retryable_and_failed_stop_retains_worker_owned_audio) {
  create();
  cr_assert_eq(session_host_start_render(owner), ERROR_INVALID_STATE);
  owner->running = true;
  fail_thread = true;
  cr_assert_eq(session_host_start_render(owner), ERROR_THREAD);
  cr_assert_not(owner->render_thread_running);
  cr_assert_not_null(owner->audio_ctx);
  fail_thread = false;
  CLEAR_ERRNO_ALL();
  cr_assert_eq(session_host_start_render(owner), ASCIICHAT_OK);
  cr_assert_eq(thread_starts, 2);
  cr_assert_eq(session_host_start_render(owner), ASCIICHAT_OK);
  cr_assert_eq(thread_starts, 2);
  session_audio_ctx_t *audio = owner->audio_ctx;
  fail_join = true;
  session_host_stop_render(owner);
  cr_assert(owner->render_thread_running);
  cr_assert_eq(owner->audio_ctx, audio);
  fail_join = false;
  CLEAR_ERRNO_ALL();
  session_host_stop_render(owner);
  cr_assert_not(owner->render_thread_running);
  cr_assert_null(owner->audio_ctx);
  cr_assert_eq(thread_joins, 2);
  session_host_stop_render(owner);
  cr_assert_eq(thread_joins, 2);
}

static unsigned pump_count, local_frames;
static uint64_t fault_clock(void) {
  return fault_clock_us;
}
static void fault_pump(void) {
  fault_clock_us += 20000;
  ++pump_count;
  if (pump_count == 1) {
    owner->clients[1].render_width = 40;
    owner->clients[1].render_height = 12;
  }
  if (pump_count >= 2)
    owner->render_thread_running = false;
}
static asciichat_error_t fault_display_config(void *context, session_host_render_config_t *config) {
  (void)context;
  *config = (session_host_render_config_t){
      .width = 40, .height = 12, .caps = {.color_level = TERM_COLOR_NONE}, .palette = "XX"};
  return ASCIICHAT_OK;
}
static void fault_display_frame(void *context, const char *frame, size_t length, int width, int height) {
  (void)context;
  cr_assert_not_null(frame);
  cr_assert_gt(length, 0);
  cr_assert_eq(width, 40);
  cr_assert_eq(height, 12);
  ++local_frames;
}
Test(host_faults, host_renderer_waits_for_peer_capabilities_before_presenting_local_frames) {
  session_host_config_t c = configuration();
  c.max_clients = 2;
  owner = session_host_create(&c);
  cr_assert_not_null(owner);
  uint32_t id = session_host_add_memory_participant(owner);
  cr_assert_neq(id, 0);
  cr_assert_neq(session_host_add_client(owner, NULL, ACIP_TRANSPORT_BORROWED, "peer", 0), 0);
  image_t *image = image_new(2, 2);
  cr_assert_not_null(image);
  memset(image->pixels, 120, 12);
  cr_assert_eq(session_host_inject_frame(owner, id, image), ASCIICHAT_OK);
  image_destroy(image);
  session_host_render_target_t target = {.get_config = fault_display_config, .render_frame = fault_display_frame};
  cr_assert_eq(session_host_set_render_target(owner, &target), ASCIICHAT_OK);
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->detected_mode = MODE_DISCOVERY;
  v->snapshot_mode = false;
  v->splash_screen = true;
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  interactive = true;
  static app_callbacks_t callbacks = {.platform_get_monotonic_time_us = fault_clock,
                                      .platform_pump_events = fault_pump};
  app_callbacks_register(&callbacks);
  owner->running = true;
  cr_assert_eq(session_host_start_render(owner), ASCIICHAT_OK);
  host_render_thread(owner);
  cr_assert_eq(pump_count, 2);
  cr_assert_eq(local_frames, 1);
  cr_assert_not(owner->render_thread_running);
  app_callbacks_register(NULL);
  host_render_thread(NULL);
  CLEAR_ERRNO_ALL();
}
