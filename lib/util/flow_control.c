#include <ascii-chat/util/flow_control.h>

void flow_control_debounce_trigger(flow_control_debounce_t *debounce, uint64_t now_ns, uint64_t delay_ns) {
  if (!debounce)
    return;
  debounce->due_ns = UINT64_MAX - now_ns < delay_ns ? UINT64_MAX : now_ns + delay_ns;
  debounce->pending = true;
}

bool flow_control_debounce_take_ready(flow_control_debounce_t *debounce, uint64_t now_ns) {
  if (!debounce || !debounce->pending || now_ns < debounce->due_ns)
    return false;
  debounce->pending = false;
  return true;
}

void flow_control_debounce_cancel(flow_control_debounce_t *debounce) {
  if (!debounce)
    return;
  debounce->pending = false;
  debounce->due_ns = 0;
}
