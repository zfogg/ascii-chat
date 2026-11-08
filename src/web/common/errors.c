#include "errors.h"
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/path.h>
#include <emscripten/emscripten.h>

static asciichat_error_context_t browser_error_frames[ASCIICHAT_ERRNO_MAX_DEPTH];
static size_t browser_error_count;

size_t wasm_error_snapshot(void) {
  asciichat_errno_snapshot_t snapshot =
      errno_snapshot(thread_current_id(), browser_error_frames, ASCIICHAT_ERRNO_MAX_DEPTH);
  browser_error_count = snapshot.copied;
  return snapshot.total;
}

static const asciichat_error_context_t *browser_error_at(size_t index) {
  return index < browser_error_count ? &browser_error_frames[index] : NULL;
}

int wasm_error_code(size_t index) {
  const asciichat_error_context_t *frame = browser_error_at(index);
  return frame ? frame->code : ASCIICHAT_OK;
}

const char *wasm_error_message(size_t index) {
  const asciichat_error_context_t *frame = browser_error_at(index);
  return frame ? frame->context_message : "";
}

const char *wasm_error_file(size_t index) {
  const asciichat_error_context_t *frame = browser_error_at(index);
  return frame && frame->file ? extract_project_relative_path(frame->file) : "";
}

const char *wasm_error_full_file(size_t index) {
#ifdef NDEBUG
  (void)index;
  return "";
#else
  const asciichat_error_context_t *frame = browser_error_at(index);
  return frame && frame->file ? frame->file : "";
#endif
}

const char *wasm_error_function(size_t index) {
  const asciichat_error_context_t *frame = browser_error_at(index);
  return frame && frame->function ? frame->function : "";
}

int wasm_error_line(size_t index) {
  const asciichat_error_context_t *frame = browser_error_at(index);
  return frame ? frame->line : 0;
}

void wasm_error_dismiss(void) {
  errno_clear_all();
  browser_error_count = 0;
}
