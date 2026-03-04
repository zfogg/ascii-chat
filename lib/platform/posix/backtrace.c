/**
 * @file platform/posix/backtrace.c
 * @brief POSIX backtrace implementation using libexecinfo and addr2line
 * @ingroup platform
 */

#include <ascii-chat/platform/backtrace.h>
#include <ascii-chat/platform/symbols.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/path.h>
#include <execinfo.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

/**
 * @brief Manual stack unwinding using frame pointers
 *
 * Falls back to manual frame pointer walking if libexecinfo's backtrace()
 * is unavailable or fails (e.g., on musl libc without libexecinfo).
 */
static int manual_backtrace(void **buffer, int size) {
  if (!buffer || size <= 0) {
    return 0;
  }

  void **frame = (void **)__builtin_frame_address(0);
  int depth = 0;

  while (frame && depth < size) {
    void *return_addr = frame[1];

    if (!return_addr || return_addr < (void *)0x1000) {
      break;
    }

    buffer[depth++] = return_addr;

    void **prev_frame = (void **)frame[0];

    if (!prev_frame || prev_frame <= frame || (uintptr_t)prev_frame & 0x7) {
      break;
    }

    frame = prev_frame;
  }

  return depth;
}

/**
 * @brief Safe wrapper for backtrace() with weak symbol check
 *
 * On musl libc, backtrace is a weak symbol that may not be available.
 * This wrapper checks for NULL before calling.
 */
static inline int safe_backtrace(void **buffer, int size) {
#ifdef USE_MUSL
  if (backtrace != NULL) {
    return backtrace(buffer, size);
  }
  return 0;
#else
  return backtrace(buffer, size);
#endif
}

int platform_backtrace(void **buffer, int size) {
  int depth = safe_backtrace(buffer, size);

  if (depth == 0) {
    depth = manual_backtrace(buffer, size);
  }

  return depth;
}

char **platform_backtrace_symbols(void *const *buffer, int size) {
  return symbol_cache_resolve_batch(buffer, size);
}

void platform_backtrace_symbols_destroy(char **strings) {
  if (!strings) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: strings=%p", (void *)strings);
    return;
  }
  symbol_cache_free_symbols(strings);
}

void backtrace_print_simple(int skip_frames) {
  void *buffer[64];
  int depth = platform_backtrace(buffer, 64);

  if (depth > 0) {
    char **symbols = platform_backtrace_symbols((void *const *)buffer, depth);
    if (symbols) {
      for (int i = skip_frames; i < depth && symbols[i]; i++) {
        fprintf(stderr, "  [%d] %s\n", i - skip_frames, symbols[i]);
      }
      platform_backtrace_symbols_destroy(symbols);
    }
  }
}
