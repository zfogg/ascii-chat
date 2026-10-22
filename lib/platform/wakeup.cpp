#include <atomic>
#include <chrono>
#include <new>
#include <semaphore>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/wakeup.h>

struct platform_wakeup {
  std::binary_semaphore ready{0};
  std::atomic<bool> pending{false};
};

asciichat_error_t platform_wakeup_create(platform_wakeup_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL wakeup output");
  auto *storage = SAFE_MALLOC(sizeof(platform_wakeup_t), platform_wakeup_t *);
  *out = new (storage) platform_wakeup_t;
  return ASCIICHAT_OK;
}

void platform_wakeup_signal(platform_wakeup_t *wake) {
  if (!wake->pending.exchange(true))
    wake->ready.release();
}

bool platform_wakeup_wait(platform_wakeup_t *wake, uint64_t timeout_ns) {
  if (!wake->ready.try_acquire_for(std::chrono::nanoseconds(timeout_ns)))
    return false;
  // The consumer inspects published work after clearing pending. A producer
  // racing this store either has its work consumed or leaves another token.
  wake->pending.store(false);
  return true;
}

void platform_wakeup_destroy(platform_wakeup_t *wake) {
  if (wake) {
    wake->~platform_wakeup();
    SAFE_FREE(wake);
  }
}
