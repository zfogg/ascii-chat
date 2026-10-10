#pragma once
/** @file debug_helpers.h
 * @brief Shared deadline and scheduling helpers for diagnostic reports.
 */
#include <ascii-chat/platform/cond.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Compute a monotonic deadline, saturating on overflow. */
uint64_t debug_report_deadline_after(uint64_t now, uint64_t delay_ns);
/** Schedule a report under the initialized worker mutex and wake its condition.
 * Duplicate requests for the same report retain the earliest deadline.
 */
void debug_report_schedule(mutex_t *mutex, cond_t *condition, uint64_t *deadline, uint64_t delay_ns);
#ifdef __cplusplus
}
#endif
