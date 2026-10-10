#pragma once
/** @file stats.h
 * @brief One diagnostics worker for locks, error stacks/history, hash tables,
 * backtraces, and memory. Signal handlers may only request a report.
 */
#include <ascii-chat/common/error_codes.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void debug_stats_set_main_thread_id(void);
uint64_t debug_stats_get_main_thread_id(void);
asciichat_error_t debug_stats_init(void);
asciichat_error_t debug_stats_start_thread(void);
void debug_stats_cleanup_thread(void);
void debug_stats_destroy(void);
void debug_stats_final_cleanup(void);
bool debug_stats_is_cleanup_in_progress(void);
void debug_stats_trigger_print(void);
// Schedule independent one-shot synchronization and error reports.
void debug_stats_print_errno_delayed(uint64_t delay_ns);
void debug_stats_print_state_delayed(uint64_t delay_ns);
void debug_stats_print_backtrace_delayed(uint64_t delay_ns);
void debug_stats_set_memory_report_interval(uint64_t interval_ns);
void debug_stats_print(void);
// Event-loop integration for single-threaded WASM builds.
void debug_stats_poll(void);
#ifdef __cplusplus
}
#endif
