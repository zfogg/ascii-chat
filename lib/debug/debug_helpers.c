#include <ascii-chat/debug/debug_helpers.h>
#include <ascii-chat/util/time.h>

uint64_t debug_report_deadline_after(uint64_t now, uint64_t delay_ns) {
  return delay_ns > UINT64_MAX - now ? UINT64_MAX : now + delay_ns;
}

void debug_report_schedule(mutex_t *mutex, cond_t *condition, uint64_t *deadline, uint64_t delay_ns) {
  uint64_t requested = debug_report_deadline_after(time_get_ns(), delay_ns);
  mutex_lock(mutex);
  if (!*deadline || requested < *deadline)
    *deadline = requested;
  cond_signal(condition);
  mutex_unlock(mutex);
}
