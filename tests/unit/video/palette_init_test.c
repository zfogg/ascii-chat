#include <criterion/criterion.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/tests/logging.h>

typedef struct {
  atomic_t *ready, *start;
  utf8_palette_cache_t *cache;
} palette_worker_t;

static void *open_palette(void *argument) {
  palette_worker_t *worker = argument;
  atomic_fetch_add_u64(worker->ready, 1);
  while (!atomic_load_bool(worker->start))
    platform_sleep_us(1);
  worker->cache = get_utf8_palette_cache(" .:-=+*#%@");
  return NULL;
}

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
}
static void cleanup(void) {
  simd_caches_destroy_all();
  log_destroy();
}

Test(palette_init, concurrent_first_renderers_observe_a_complete_cache, .init = setup, .fini = cleanup, .timeout = 30) {
  for (unsigned round = 0; round < 20; round++) {
    simd_caches_destroy_all();
    atomic_t ready = {0}, start = {0};
    palette_worker_t workers[8];
    asciichat_thread_t threads[8];
    for (unsigned i = 0; i < 8; i++) {
      workers[i] = (palette_worker_t){.ready = &ready, .start = &start};
      cr_assert_eq(acthread_create(&threads[i], "palette-init", open_palette, &workers[i]), 0);
    }
    uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
    while (atomic_load_u64(&ready) != 8 && time_get_ns() < deadline)
      platform_sleep_us(100);
    cr_assert_eq(atomic_load_u64(&ready), 8);
    atomic_store_bool(&start, true);
    for (unsigned i = 0; i < 8; i++) {
      cr_assert_eq(thread_join(&threads[i], NULL), 0);
      cr_assert_not_null(workers[i].cache);
      cr_assert(workers[i].cache->is_valid);
      cr_assert_eq(workers[i].cache, workers[0].cache);
      for (unsigned glyph = 0; glyph < 64; glyph++)
        cr_assert_eq(workers[i].cache->cache64[glyph].byte_len, 1);
    }
  }
}
