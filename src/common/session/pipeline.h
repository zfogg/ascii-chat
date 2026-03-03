/**
 * @file session/pipeline.h
 * @brief Three-thread render pipeline: capture → display/encode
 * @ingroup session
 *
 * Decouples capture, terminal display, and file encoding into separate threads:
 * - Capture thread: reads raw image_t frames from media source
 * - Main thread: converts to ASCII, prints to terminal (fast)
 * - Encode thread: converts to ASCII, encodes to FFmpeg file (slow, non-blocking)
 */

#ifndef ASCIICHAT_SESSION_PIPELINE_H
#define ASCIICHAT_SESSION_PIPELINE_H

#include <stdint.h>
#include <stdbool.h>
#include "session/render.h"  // for callback types

/* Forward declarations */
typedef struct session_capture_ctx session_capture_ctx_t;
typedef struct session_display_ctx session_display_ctx_t;
typedef struct session_pipeline_s session_pipeline_t;

/**
 * Create and start the capture thread and (if --render-file) encode thread.
 *
 * @param capture   Capture context (must have real media_source_t for synchronous mode)
 * @param display   Display context (may have render_file set for --render-file mode)
 * @param out       Pointer to receive allocated pipeline context
 *
 * @return ASCIICHAT_OK on success, error code otherwise
 *
 * @note The pipeline does NOT own capture or display — caller must keep them alive
 *       until session_pipeline_destroy() returns.
 */
asciichat_error_t session_pipeline_create(
    session_capture_ctx_t *capture,
    session_display_ctx_t *display,
    session_pipeline_t **out);

/**
 * Main thread loop: drives terminal output and keyboard handling.
 *
 * Pops frames from display queue, converts to ASCII, writes to terminal.
 * Handles snapshot delay timer and exit conditions.
 *
 * @param pipeline        Pipeline context created by session_pipeline_create()
 * @param should_exit     Callback to check if rendering should stop
 * @param keyboard_handler Optional keyboard handler callback
 * @param user_data       Opaque data passed to callbacks
 *
 * @return ASCIICHAT_OK on success
 *
 * @note This function blocks until should_exit() returns true or capture ends.
 *       Signals capture/encode threads to stop before returning.
 */
asciichat_error_t session_pipeline_run_main(
    session_pipeline_t *pipeline,
    session_should_exit_fn should_exit,
    session_keyboard_handler_fn keyboard_handler,
    void *user_data);

/**
 * Stop threads and free pipeline.
 *
 * Waits for capture and encode threads to exit cleanly.
 *
 * @param pipeline Pipeline context (safe to call with NULL)
 * @return ASCIICHAT_OK
 */
asciichat_error_t session_pipeline_destroy(session_pipeline_t *pipeline);

#endif // ASCIICHAT_SESSION_PIPELINE_H
