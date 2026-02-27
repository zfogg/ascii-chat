/**
 * @file ui/frame_buffer.c
 * @brief Buffered terminal frame rendering implementation
 *
 * Accumulates terminal output into a growable buffer and flushes atomically.
 */

#include "ascii-chat/ui/frame_buffer.h"
#include "ascii-chat/platform/abstraction.h"
#include "ascii-chat/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

/**
 * @brief Frame buffer structure
 */
struct frame_buffer {
  char *data;      ///< Allocated buffer
  size_t len;      ///< Current content length
  size_t capacity; ///< Allocated capacity
};

frame_buffer_t *frame_buffer_create(int rows, int cols) {
  frame_buffer_t *buf = SAFE_MALLOC(1, frame_buffer_t *);
  if (!buf) {
    return NULL;
  }

  // Initial capacity: rows * cols * 16 to account for ANSI codes
  size_t initial_capacity = (rows > 0 && cols > 0) ? (size_t)rows * cols * 16 : 4096;

  buf->data = SAFE_MALLOC(initial_capacity, char *);
  if (!buf->data) {
    SAFE_FREE(buf);
    return NULL;
  }

  buf->len = 0;
  buf->capacity = initial_capacity;

  return buf;
}

void frame_buffer_destroy(frame_buffer_t *buf) {
  if (!buf) {
    return;
  }

  SAFE_FREE(buf->data);
  SAFE_FREE(buf);
}

void frame_buffer_reset(frame_buffer_t *buf) {
  if (!buf) {
    return;
  }

  buf->len = 0;
}

void frame_buffer_append(frame_buffer_t *buf, const char *str, size_t len) {
  if (!buf || !str || len == 0) {
    return;
  }

  // Grow buffer if needed
  if (buf->len + len > buf->capacity) {
    size_t new_capacity = buf->capacity * 2;
    while (new_capacity < buf->len + len) {
      new_capacity *= 2;
    }

    char *new_data = SAFE_MALLOC(new_capacity, char *);
    if (!new_data) {
      return; // Failed to grow, silently ignore
    }

    if (buf->len > 0 && buf->data) {
      memcpy(new_data, buf->data, buf->len);
    }

    SAFE_FREE(buf->data);
    buf->data = new_data;
    buf->capacity = new_capacity;
  }

  // Append the data
  memcpy(buf->data + buf->len, str, len);
  buf->len += len;
}

void frame_buffer_printf(frame_buffer_t *buf, const char *fmt, ...) {
  if (!buf || !fmt) {
    return;
  }

  // Ensure we have space for a formatted string
  // Start with a reasonable buffer size
  size_t remaining = buf->capacity - buf->len;
  if (remaining < 256) {
    // Grow buffer to have at least 512 bytes of space
    size_t new_capacity = buf->capacity + 512;
    char *new_data = SAFE_MALLOC(new_capacity, char *);
    if (!new_data) {
      return;
    }

    if (buf->len > 0 && buf->data) {
      memcpy(new_data, buf->data, buf->len);
    }

    SAFE_FREE(buf->data);
    buf->data = new_data;
    buf->capacity = new_capacity;
    remaining = buf->capacity - buf->len;
  }

  // Format the string into the buffer
  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(buf->data + buf->len, remaining, fmt, args);
  va_end(args);

  if (written > 0 && written < (int)remaining) {
    buf->len += (size_t)written;
  }
  // If vsnprintf failed or truncated, we silently ignore (buffer may be full)
}

void frame_buffer_cursor_home(frame_buffer_t *buf) {
  if (!buf) {
    return;
  }

  frame_buffer_append(buf, "\033[H", 3);
}

void frame_buffer_clear_screen(frame_buffer_t *buf) {
  if (!buf) {
    return;
  }

  // Combine clear screen and home in one operation
  frame_buffer_append(buf, "\033[2J\033[H", 8);
}

void frame_buffer_flush(frame_buffer_t *buf) {
  if (!buf || buf->len == 0 || !buf->data) {
    return;
  }

  // Write entire buffer to stdout in one atomic operation
  platform_write_all(STDOUT_FILENO, buf->data, buf->len);
}
