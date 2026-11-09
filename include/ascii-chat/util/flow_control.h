#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint64_t due_ns;
  bool pending;
/**
 * @brief The useful part of flow control debounce is in `due_ns`, `pending`: together they describe what is pending, who owns it, and how much data is valid. Keep the object with its parent operation and use the parent cleanup path.
 */
} flow_control_debounce_t;

/** Record an event whose action should run after delay_ns of inactivity. */
void flow_control_debounce_trigger(flow_control_debounce_t *debounce, uint64_t now_ns, uint64_t delay_ns);

/** Consume a pending action once its quiet period has elapsed. */
bool flow_control_debounce_take_ready(flow_control_debounce_t *debounce, uint64_t now_ns);

/** Discard a pending action. */
void flow_control_debounce_cancel(flow_control_debounce_t *debounce);
