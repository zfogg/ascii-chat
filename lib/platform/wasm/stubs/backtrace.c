/**
 * @file lib/platform/wasm/stubs/backtrace.c
 * @brief Backtrace stubs for WASM (not available in browser environment)
 */

#include <ascii-chat/debug/backtrace.h>
#include <stddef.h>

void backtrace_capture(backtrace_t *bt) {
  if (bt) {
    bt->count = 0;
    bt->symbols = NULL;
  }
}

void backtrace_capture_and_symbolize(backtrace_t *bt) {
  if (bt) {
    bt->count = 0;
    bt->symbols = NULL;
  }
}

void backtrace_free(backtrace_t *bt) {
  if (bt) {
    bt->count = 0;
    bt->symbols = NULL;
  }
}

void backtrace_t_free(backtrace_t *bt) {
  if (bt) {
    bt->count = 0;
    bt->symbols = NULL;
  }
}

void backtrace_symbolize(backtrace_t *bt) {
  (void)bt;
  // No-op for WASM
}

typedef bool (*backtrace_frame_filter_t)(const char *frame);

void backtrace_print(const char *label, const backtrace_t *bt, int skip_frames, int max_frames,
                     backtrace_frame_filter_t filter) {
  (void)label;
  (void)bt;
  (void)skip_frames;
  (void)max_frames;
  (void)filter;
  // No-op for WASM
}

void backtrace_print_simple(int skip_frames) {
  (void)skip_frames;
  // No-op for WASM
}
