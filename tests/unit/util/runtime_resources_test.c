#include <criterion/criterion.h>
#include <ascii-chat/common.h>
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/mmap.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/video/face_follow_image.h>
#include <errno.h>

static void setup(void) {
  log_init(NULL, LOG_ERROR, false, false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
}
static void cleanup(void) {
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(runtime_resources, .init = setup, .fini = cleanup, .timeout = 15);

typedef struct {
  atomic_t ready, release, completed, expired;
} work_t;
static void *blocked_work(void *arg) {
  work_t *work = arg;
  atomic_store_bool(&work->ready, true);
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  while (!atomic_load_bool(&work->release) && time_get_ns() < deadline)
    platform_sleep_ms(1);
  if (!atomic_load_bool(&work->release))
    atomic_store_bool(&work->expired, true);
  atomic_fetch_add_u64(&work->completed, 1);
  return NULL;
}
static void *count_work(void *arg) {
  atomic_fetch_add_u64(&((work_t *)arg)->completed, 1);
  return NULL;
}

Test(runtime_resources, work_queue_drains_pending_tasks_before_join_and_keeps_modes_distinct) {
  work_t work = {0};
  thread_pool_t *pool = thread_pool_create_with_workers("drain-contract", 1);
  cr_assert_not_null(pool);
  cr_assert_eq(thread_pool_queue_work("blocked", pool, blocked_work, &work), ASCIICHAT_OK);
  uint64_t deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (!atomic_load_bool(&work.ready) && time_get_ns() < deadline)
    platform_sleep_ms(1);
  bool ready = atomic_load_bool(&work.ready);
  for (unsigned i = 0; i < 8; ++i)
    cr_assert_eq(thread_pool_queue_work("count", pool, count_work, &work), ASCIICHAT_OK);
  cr_assert_eq(thread_pool_spawn(pool, count_work, &work, 1, NULL), ERROR_INVALID_STATE);
  cr_assert_eq(thread_pool_queue_work(NULL, pool, count_work, &work), ERROR_INVALID_PARAM);
  cr_assert_eq(thread_pool_queue_work("invalid", pool, NULL, &work), ERROR_INVALID_PARAM);
  cr_assert_eq(thread_pool_interrupt_all(pool, 0), ASCIICHAT_OK);
  atomic_store_bool(&work.release, true);
  asciichat_error_t stopped = thread_pool_stop_all(pool);
  size_t count = thread_pool_get_count(pool);
  thread_pool_destroy(pool);
  cr_assert(ready);
  cr_assert_not(atomic_load_bool(&work.expired));
  cr_assert_eq(stopped, ASCIICHAT_OK);
  cr_assert_eq(count, 0);
  cr_assert_eq(atomic_load_u64(&work.completed), 9);
}

Test(runtime_resources, named_thread_pool_retains_ordered_entries_and_can_be_stopped_repeatedly) {
  work_t work = {0};
  thread_pool_t *pool = thread_pool_create(NULL);
  cr_assert_not_null(pool);
  cr_assert_str_eq(pool->name, "unnamed");
  cr_assert_eq(thread_pool_queue_work("wrong-mode", pool, count_work, &work), ERROR_INVALID_STATE);
  const int order[] = {8, 3, 8, 1};
  for (size_t i = 0; i < ARRAY_SIZE(order); ++i)
    cr_assert_eq(thread_pool_spawn(pool, blocked_work, &work, order[i], i == 0 ? "first" : NULL), ASCIICHAT_OK);
  cr_assert_eq(thread_pool_get_count(pool), 4);
  cr_assert(thread_pool_has_threads(pool));
  int previous = 0;
  for (thread_pool_entry_t *entry = pool->threads; entry; entry = entry->next) {
    cr_assert_geq(entry->stop_id, previous);
    previous = entry->stop_id;
  }
  atomic_store_bool(&work.release, true);
  cr_assert_eq(thread_pool_stop_all(pool), ASCIICHAT_OK);
  cr_assert_eq(thread_pool_stop_all(pool), ASCIICHAT_OK);
  cr_assert_eq(thread_pool_interrupt_all(pool, 0), ASCIICHAT_OK);
  cr_assert_not(thread_pool_has_threads(pool));
  cr_assert_eq(atomic_load_u64(&work.completed), 4);
  thread_pool_destroy(pool);
  thread_pool_destroy(NULL);
  cr_assert_null(thread_pool_create_with_workers(NULL, 0));
  cr_assert_eq(thread_pool_spawn(NULL, count_work, &work, 1, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(thread_pool_queue_work("x", NULL, count_work, &work), ERROR_INVALID_PARAM);
  cr_assert_eq(thread_pool_stop_all(NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(thread_pool_interrupt_all(NULL, 0), ERROR_INVALID_PARAM);
  cr_assert_eq(thread_pool_get_count(NULL), 0);
}

Test(runtime_resources, memory_mapping_persists_bytes_preserves_larger_files_and_supports_repeated_close) {
  char path[PLATFORM_MAX_PATH_LENGTH];
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "mapping-contract", &fd), 0);
  cr_assert_eq(platform_close(fd), 0);
  platform_mmap_t map;
  platform_mmap_init(&map);
  cr_assert_not(platform_mmap_is_valid(&map));
  cr_assert_eq(platform_mmap_open("mapping", path, 4096, &map), ASCIICHAT_OK);
  cr_assert(platform_mmap_is_valid(&map));
  cr_assert_eq(map.size, 4096);
  memcpy(map.addr, "persistent\0bytes", 16);
  platform_mmap_sync(&map, false);
  platform_mmap_sync(&map, true);
  platform_mmap_close(&map);
  platform_mmap_close(&map);
  cr_assert_not(platform_mmap_is_valid(&map));
  cr_assert_eq(map.size, 0);
  cr_assert_eq(map.fd, -1);
  cr_assert_eq(platform_mmap_open("smaller", path, 32, &map), ASCIICHAT_OK);
  cr_assert_eq(map.size, 4096);
  cr_assert_arr_eq(map.addr, "persistent\0bytes", 16);
  platform_mmap_close(&map);
  cr_assert_eq(platform_unlink(path), 0);
  cr_assert_eq(platform_mmap_open(NULL, path, 1, &map), ERROR_INVALID_PARAM);
  cr_assert_eq(platform_mmap_open("x", NULL, 1, &map), ERROR_INVALID_PARAM);
  cr_assert_eq(platform_mmap_open("x", path, 1, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(platform_mmap_open("x", path, 0, &map), ERROR_INVALID_PARAM);
  platform_mmap_init(NULL);
  platform_mmap_close(NULL);
  platform_mmap_sync(NULL, false);
  platform_mmap_sync(&map, false);
  cr_assert_not(platform_mmap_is_valid(NULL));
}

Test(runtime_resources, file_errors_include_the_path_and_binary_cache_is_reusable_after_cleanup) {
  const int read_codes[] = {ENOENT, EACCES, EISDIR, EIO};
  for (size_t i = 0; i < ARRAY_SIZE(read_codes); ++i) {
    errno = read_codes[i];
    cr_assert_not_null(strstr(file_read_error_message("fixture-path"), "fixture-path"));
  }
  const int write_codes[] = {ENOENT, EACCES, EROFS, ENOSPC, EISDIR, EIO};
  for (size_t i = 0; i < ARRAY_SIZE(write_codes); ++i) {
    errno = write_codes[i];
    cr_assert_not_null(strstr(file_write_error_message("fixture-path"), "fixture-path"));
  }
  for (unsigned cycle = 0; cycle < 2; ++cycle) {
    cr_assert_not(platform_is_binary_in_path(NULL));
    cr_assert_not(platform_is_binary_in_path(""));
    cr_assert(platform_is_binary_in_path("cmake"));
    cr_assert(platform_is_binary_in_path("cmake"));
    cr_assert_not(platform_is_binary_in_path("ascii-coverage-nonexistent-command-78261"));
    cr_assert_not(platform_is_binary_in_path("ascii-coverage-nonexistent-command-78261"));
    platform_cleanup_binary_path_cache();
    platform_cleanup_binary_path_cache();
  }
}

Test(runtime_resources, session_keyboard_changes_wrap_modes_clamp_volume_and_restore_mute_level) {
  double saved = 0;
  cr_assert_eq(options_set_double("speakers_volume", 0.05), ASCIICHAT_OK);
  cr_assert_eq(ui_session_key(KEY_DOWN, &saved), UI_ACTION_VOLUME);
  cr_assert_float_eq(GET_OPTION(speakers_volume), 0, 1e-6);
  cr_assert_eq(ui_session_key(KEY_M, &saved), UI_ACTION_SETTINGS);
  cr_assert_float_eq(GET_OPTION(speakers_volume), 0.5, 1e-6);
  ui_session_key(KEY_M, &saved);
  cr_assert_float_eq(GET_OPTION(speakers_volume), 0, 1e-6);
  ui_session_key(KEY_M, &saved);
  cr_assert_float_eq(GET_OPTION(speakers_volume), 0.5, 1e-6);
  cr_assert_eq(options_set_double("speakers_volume", 0.95), ASCIICHAT_OK);
  ui_session_key(KEY_UP, &saved);
  cr_assert_float_eq(GET_OPTION(speakers_volume), 1, 1e-6);
  cr_assert_eq(options_set_int("color_mode", 0), ASCIICHAT_OK);
  for (int i = 1; i <= 4; ++i) {
    ui_session_key(KEY_C, &saved);
    cr_assert_eq(GET_OPTION(color_mode), i % 4);
  }
  cr_assert_eq(options_set_int("render_mode", 0), ASCIICHAT_OK);
  for (int i = 1; i <= 3; ++i) {
    ui_session_key(KEY_R, &saved);
    cr_assert_eq((int)GET_OPTION(render_mode), i % 3);
  }
  cr_assert_eq(options_set_int("color_filter", 0), ASCIICHAT_OK);
  for (int i = 1; i <= 13; ++i) {
    ui_session_key(KEY_F, &saved);
    cr_assert_eq((int)GET_OPTION(color_filter), i % 13);
  }
  const keyboard_key_t keys[] = {'x', 'y', KEY_1, 'w', KEY_MINUS};
  const char *names[] = {"flip_x", "flip_y", "matrix_rain", "face_follow", "fps_counter"};
  for (size_t i = 0; i < ARRAY_SIZE(keys); ++i) {
    cr_assert_eq(options_set_bool(names[i], false), ASCIICHAT_OK);
    cr_assert_eq(ui_session_key(keys[i], &saved), UI_ACTION_SETTINGS);
    bool enabled[] = {GET_OPTION(flip_x), GET_OPTION(flip_y), GET_OPTION(matrix_rain), GET_OPTION(face_follow),
                      GET_OPTION(fps_counter)};
    cr_assert(enabled[i]);
    cr_assert_eq(ui_session_key(keys[i], &saved), UI_ACTION_SETTINGS);
    bool disabled[] = {GET_OPTION(flip_x), GET_OPTION(flip_y), GET_OPTION(matrix_rain), GET_OPTION(face_follow),
                       GET_OPTION(fps_counter)};
    cr_assert_not(disabled[i]);
  }
  ui_session_key('z', &saved);
  cr_assert_eq(GET_OPTION(zalgo), 8);
  ui_session_key('Z', &saved);
  cr_assert_eq(GET_OPTION(zalgo), 0);
  cr_assert_eq(ui_session_key(KEY_QUESTION, &saved), UI_ACTION_HELP);
  cr_assert_eq(ui_session_key(KEY_ESCAPE, &saved), UI_ACTION_CLOSE);
  cr_assert_eq(ui_session_key(KEY_LEFT, &saved), UI_ACTION_SEEK_BACK);
  cr_assert_eq(ui_session_key(KEY_RIGHT, &saved), UI_ACTION_SEEK_FORWARD);
  cr_assert_eq(ui_session_key(KEY_SPACE, &saved), UI_ACTION_PAUSE);
  cr_assert_eq(ui_session_key('[', &saved), UI_ACTION_TOGGLE_MEDIA);
  cr_assert_eq(ui_session_key(']', &saved), UI_ACTION_LOGS);
  cr_assert_eq(ui_session_key(KEY_BACKTICK, &saved), UI_ACTION_DEBUG);
  cr_assert_eq(ui_session_key(KEY_NONE, &saved), UI_ACTION_NONE);
}

Test(runtime_resources, face_follow_processing_bounds_output_and_preserves_uniform_source_pixels) {
  face_follow_image_t *context = NULL;
  image_t *image = image_new(80, 40);
  cr_assert_not_null(image);
  for (unsigned i = 0; i < 80 * 40; ++i)
    image->pixels[i] = (rgb_pixel_t){11, 22, 33};
  cr_assert_eq(face_follow_image_process(&context, image, 1, false, false, 40, 20), image);
  cr_assert_null(context);
  image_t *out = face_follow_image_process(&context, image, 2, false, true, 40, 20);
  cr_assert_not_null(out);
  cr_assert_not_null(context);
  cr_assert_eq(out->w, 40);
  cr_assert_eq(out->h, 20);
  for (unsigned i = 0; i < 40 * 20; ++i) {
    cr_assert_eq(out->pixels[i].r, 11);
    cr_assert_eq(out->pixels[i].g, 22);
    cr_assert_eq(out->pixels[i].b, 33);
  }
  cr_assert_eq(face_follow_image_process(&context, image, 2.01, false, true, 40, 20), out);
  out = face_follow_image_process(&context, image, 3, false, true, 20, 10);
  cr_assert_eq(out->w, 20);
  cr_assert_eq(out->h, 10);
  cr_assert_eq(face_follow_image_process(&context, NULL, 4, true, false, 20, 10), NULL);
  face_follow_image_destroy(context);
  face_follow_image_destroy(NULL);
  image_destroy(image);
}
