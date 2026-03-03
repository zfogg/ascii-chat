/**
 * @file ui/frame_buffer.c
 * @brief Buffered terminal frame rendering implementation
 *
 * Accumulates terminal output into a growable buffer and flushes atomically.
 */

#include "ascii-chat/ui/frame_buffer.h"
#include "ascii-chat/platform/abstraction.h"
#include <ascii-chat/util/display.h>
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
  int output_fd;   ///< File descriptor to write to (default: STDOUT_FILENO)
};

// Global output FD configuration (used if not overridden per-buffer)
static int g_output_fd = STDOUT_FILENO;

frame_buffer_t *frame_buffer_create(int rows, int cols) {
  frame_buffer_t *buf = SAFE_MALLOC(sizeof(frame_buffer_t), frame_buffer_t *);
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
  buf->output_fd = g_output_fd; // Use global default, can be overridden

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

  // Write entire buffer to configured FD in one atomic operation
  platform_write_all(buf->output_fd, buf->data, buf->len);
}

void frame_buffer_set_output_fd(frame_buffer_t *buf, int fd) {
  if (buf) {
    buf->output_fd = fd;
  }
}

void frame_buffer_set_global_output_fd(int fd) {
  g_output_fd = fd;
}

size_t frame_buffer_get_length(const frame_buffer_t *buf) {
  if (!buf) {
    return 0;
  }
  return buf->len;
}

const char *frame_buffer_get_content(const frame_buffer_t *buf) {
  if (!buf || !buf->data) {
    return NULL;
  }
  return buf->data;
}

void frame_buffer_render_border(frame_buffer_t *buf, int cols, const char *color) {
  if (!buf || cols <= 0) {
    return;
  }

  if (color) {
    frame_buffer_printf(buf, "%s━", color);
  } else {
    frame_buffer_printf(buf, "━");
  }
  for (int i = 1; i < cols - 1; i++) {
    frame_buffer_printf(buf, "━");
  }
  if (color) {
    frame_buffer_printf(buf, "\033[0m");
  }
  frame_buffer_printf(buf, "\n");
}

int frame_buffer_render_centered(frame_buffer_t *buf, const char *text, int cols) {
  if (!buf || !text || cols <= 0) {
    return 0;
  }

  // Calculate visible width accounting for ANSI codes
  int visible_width = display_width(text);
  if (visible_width < 0) {
    visible_width = (int)strlen(text);
  }

  int padding = (cols - visible_width) / 2;
  if (padding < 0) {
    padding = 0;
  }

  for (int i = 0; i < padding; i++) {
    frame_buffer_printf(buf, " ");
  }
  frame_buffer_printf(buf, "%s\n", text);

  return padding;
}
