#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct render_progress render_progress_t;

/** Retains only the latest ASCII frame. All updates are serialized; callbacks own copied snapshots. */
render_progress_t *render_progress_create(bool enabled);
void render_progress_frame(render_progress_t *progress, const char *frame, bool succeeded);
void render_progress_begin(render_progress_t *progress, uint64_t total, bool total_known);
void render_progress_finalize(render_progress_t *progress);
void render_progress_destroy(render_progress_t *progress);
