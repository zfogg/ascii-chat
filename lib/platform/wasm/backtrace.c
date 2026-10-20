/**
 * @file platform/wasm/backtrace.c
 * @brief WASM implementation for backtrace (no-op in web)
 */

#include <ascii-chat/debug/backtrace.h>
#include <stdarg.h>

void backtrace_t_free(backtrace_t *bt) {
  (void)bt;
  // No-op - no resources to free in WASM
}

void backtrace_print(log_level_t level, const char *label, const backtrace_t *bt, int skip_frames, int max_frames,
                     backtrace_frame_filter_t filter) {
  (void)level;
  (void)label;
  (void)bt;
  (void)skip_frames;
  (void)max_frames;
  (void)filter;
  // No-op - backtraces not available in WASM
}

void backtrace_print_many(log_level_t level, const char *label, const backtrace_t *bts, int count) {
  (void)level;
  (void)label;
  (void)bts;
  (void)count;
  // No-op
}

void backtrace_symbolize(backtrace_t *bt) {
  (void)bt;
  // No-op - symbolization not available in WASM
}
