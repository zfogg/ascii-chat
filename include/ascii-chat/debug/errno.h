#pragma once
/** @file errno.h
 * @brief Dedicated worker for all pending errno stacks and handled history. Signal handlers may only request a report.
 */
#include <ascii-chat/common/error_codes.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
asciichat_error_t debug_errno_init(void);
asciichat_error_t debug_errno_start_thread(void);
void debug_errno_cleanup_thread(void);
void debug_errno_destroy(void);
bool debug_errno_is_cleanup_in_progress(void);
void debug_errno_trigger_print(void);
// Schedule one-shot error reports.
void debug_errno_print_delayed(uint64_t delay_ns);
void debug_errno_print(void);
// Event-loop integration for single-threaded WASM builds.
void debug_errno_poll(void);
#ifdef __cplusplus
}
#endif
