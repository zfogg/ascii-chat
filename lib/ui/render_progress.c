#include <ascii-chat/ui/render_progress.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/ascii/rle.h>
#include <string.h>
#include <stdio.h>

// Preview storage is bounded independently of recording duration and queue depth.
#define PREVIEW_MAX_BYTES (1024 * 1024)

typedef struct {
  uint64_t started_ns;
  uint64_t completed;
  uint64_t total;
  bool total_known;
  bool finalizing;
  bool failed;
  size_t length;
  char frame[];
} progress_snapshot_t;

struct render_progress {
  mutex_t mutex;
  progress_snapshot_t *snapshot;
  int fd;
  bool active;
};

static void progress_render(terminal_size_t size, const void *data) {
  const progress_snapshot_t *snapshot = data;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  char *line = SAFE_MALLOC(snapshot->length + 1, char *);
  char *clipped = SAFE_MALLOC(snapshot->length + 32, char *);
  const char *cursor = snapshot->frame;
  const char *end = cursor + snapshot->length;
  // Address each row explicitly, leaving the last column free to prevent wrapping.
  for (int row = 1; row <= size.rows; ++row) {
    frame_buffer_printf(buffer, "\033[%d;1H\033[0m\033[2K", row);
    if (cursor < end) {
      const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
      size_t length = (size_t)((newline ? newline : end) - cursor);
      memcpy(line, cursor, length);
      line[length] = '\0';
      truncate_with_ellipsis(line, clipped, snapshot->length + 32, size.cols - 1);
      frame_buffer_append(buffer, clipped, strlen(clipped));
      cursor += length + (newline ? 1 : 0);
    }
  }
  uint64_t elapsed = snapshot->started_ns ? time_get_ns() - snapshot->started_ns : 0;
  uint64_t seconds = elapsed / NS_PER_SEC_INT;
  char label[160], total[32];
  if (snapshot->total_known)
    snprintf(total, sizeof(total), "%llu", (unsigned long long)snapshot->total);
  else
    snprintf(total, sizeof(total), "?");
  const char *phase = snapshot->failed       ? "Recording failed"
                      : snapshot->finalizing ? "Finalizing file"
                                             : "Rendering file";
  snprintf(label, sizeof(label), " %c %s (%llu:%02llu) (frame %llu/%s) ",
           "|/-\\"[(elapsed / (125 * NS_PER_MS_INT)) % 4], phase, (unsigned long long)(seconds / 60),
           (unsigned long long)(seconds % 60), (unsigned long long)snapshot->completed, total);
  int width = (int)strlen(label);
  if (width > size.cols - 2) {
    width = size.cols - 2;
    label[width] = '\0';
  }
  int col = (size.cols - width) / 2 + 1;
  frame_buffer_printf(buffer, "\033[%d;%dH\033[0;7m%s\033[0m", (size.rows + 1) / 2, col, label);
  ui_controller_write(STDOUT_FILENO, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  SAFE_FREE(line);
  SAFE_FREE(clipped);
  frame_buffer_destroy(buffer);
}

// The presentation callback reads only the controller-owned copy and never takes this mutex.
static void progress_publish(render_progress_t *progress) {
  if (progress->active)
    ui_controller_submit(UI_SCREEN_RENDER_PROGRESS, progress->fd, (terminal_size_t){.cols = 20, .rows = 5},
                         progress_render, progress->snapshot,
                         sizeof(*progress->snapshot) + progress->snapshot->length + 1);
}

render_progress_t *render_progress_create(bool enabled) {
  if (!enabled || !terminal_is_stdout_tty())
    return NULL;
  render_progress_t *progress = SAFE_CALLOC(1, sizeof(*progress), render_progress_t *);
  progress->fd = platform_dup(STDOUT_FILENO);
  if (progress->fd < 0) {
    SAFE_FREE(progress);
    return NULL;
  }
  mutex_init(&progress->mutex, "render_progress");
  progress->snapshot = SAFE_CALLOC(1, sizeof(*progress->snapshot) + 1, progress_snapshot_t *);
  progress->snapshot->started_ns = time_get_ns();
  return progress;
}

void render_progress_frame(render_progress_t *progress, const char *frame, bool succeeded) {
  if (!progress)
    return;
  mutex_lock(&progress->mutex);
  char *expanded = ansi_expand_rle(frame, strlen(frame));
  const char *preview = expanded ? expanded : frame;
  size_t length = strnlen(preview, PREVIEW_MAX_BYTES + 1);
  // Oversized frames still encode normally; omit their terminal preview.
  if (length > PREVIEW_MAX_BYTES)
    length = 0;
  progress_snapshot_t *next = SAFE_CALLOC(1, sizeof(*next) + length + 1, progress_snapshot_t *);
  memcpy(next, progress->snapshot, sizeof(*next));
  next->length = length;
  memcpy(next->frame, preview, length);
  SAFE_FREE(expanded);
  if (!next->started_ns)
    next->started_ns = time_get_ns();
  if (succeeded)
    next->completed++;
  else
    next->failed = true;
  SAFE_FREE(progress->snapshot);
  progress->snapshot = next;
  progress_publish(progress);
  mutex_unlock(&progress->mutex);
}

void render_progress_begin(render_progress_t *progress, uint64_t total, bool total_known) {
  if (!progress)
    return;
  mutex_lock(&progress->mutex);
  progress->active = true;
  progress->snapshot->total = total;
  progress->snapshot->total_known = total_known;
  progress_publish(progress);
  mutex_unlock(&progress->mutex);
}

void render_progress_finalize(render_progress_t *progress) {
  if (!progress)
    return;
  mutex_lock(&progress->mutex);
  progress->active = true;
  progress->snapshot->finalizing = true;
  if (!progress->snapshot->total_known) {
    progress->snapshot->total = progress->snapshot->completed;
    progress->snapshot->total_known = true;
  }
  progress_publish(progress);
  mutex_unlock(&progress->mutex);
}

void render_progress_destroy(render_progress_t *progress) {
  if (!progress)
    return;
  ui_controller_remove(UI_SCREEN_RENDER_PROGRESS);
  platform_close(progress->fd);
  SAFE_FREE(progress->snapshot);
  mutex_destroy(&progress->mutex);
  SAFE_FREE(progress);
}
