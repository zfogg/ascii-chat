#include <criterion/criterion.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/platform/thread.h>

typedef struct {
  unsigned char value;
  bool valid;
} allocation_worker_t;

static void *allocate_concurrently(void *argument) {
  allocation_worker_t *worker = argument;
  worker->valid = true;
  for (int iteration = 0; iteration < 1000; iteration++) {
    unsigned char *blocks[4];
    for (int i = 0; i < 4; i++) {
      blocks[i] = SAFE_CALLOC(128, sizeof(unsigned char), unsigned char *);
      memset(blocks[i], worker->value, 128);
    }
    for (int i = 0; i < 4; i++) {
      blocks[i] = SAFE_REALLOC(blocks[i], 256, unsigned char *);
      for (int j = 0; j < 128; j++)
        if (blocks[i][j] != worker->value)
          worker->valid = false;
      SAFE_FREE(blocks[i]);
    }
  }
  return NULL;
}

Test(memory_concurrency, allocation_tracking_is_serialized_across_threads, .timeout = 30) {
  allocation_worker_t workers[8];
  asciichat_thread_t threads[8];
  debug_memory_ensure_init();
  for (int i = 0; i < 8; i++) {
    workers[i] = (allocation_worker_t){.value = (unsigned char)(i + 1)};
    cr_assert_eq(asciichat_thread_create(&threads[i], "allocation-regression", allocate_concurrently, &workers[i]), 0);
  }
  for (int i = 0; i < 8; i++) {
    cr_assert_eq(asciichat_thread_join(&threads[i], NULL), 0);
    cr_assert(workers[i].valid, "Concurrent allocation/reallocation must preserve each thread's data");
  }
}
