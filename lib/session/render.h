/**
 * @file session/render.h
 * @ingroup session
 * @brief Centralized render loop abstraction for all display modes
 *
 * Provides a single, unified render loop that handles:
 * - Frame rate limiting (adaptive sleep)
 * - Frame capture from media source or custom callbacks
 * - ASCII art conversion with palette and terminal capabilities
 * - Terminal rendering with snapshot mode support
 * - Memory cleanup
 *
 * All display modes (mirror, client, discovery) use this single loop.
 * Modes can either:
 * - Pass capture context (NULL callbacks) for synchronous operation
 * - Pass custom callbacks (NULL capture) for event-driven operation
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "capture.h"
#include "display.h"
#include "asciichat_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Exit condition callback type
 *
 * Called each iteration to check if render loop should terminate.
 * Return true to exit, false to continue.
 *
 * @param user_data Opaque pointer provided by caller
 * @return true if loop should exit, false to continue
 *
 * @ingroup session
 */
typedef bool (*session_should_exit_fn)(void *user_data);

/**
 * @brief Capture callback for event-driven frame source
 *
 * Called to obtain the next frame for rendering. Can return NULL if no frame
 * is currently available.
 *
 * Used when capture context is NULL (event-driven mode).
 * Not called if capture context is provided (synchronous mode).
 *
 * @param user_data Opaque pointer provided by caller
 * @return Pointer to frame (caller must not free), or NULL if unavailable
 *
 * @ingroup session
 */
typedef image_t *(*session_capture_fn)(void *user_data);

/**
 * @brief Sleep callback for custom frame timing
 *
 * Called to sleep/wait until the next frame is ready. Allows custom timing logic.
 *
 * Used when capture context is NULL (event-driven mode).
 * Not called if capture context is provided (synchronous mode).
 *
 * @param user_data Opaque pointer provided by caller
 *
 * @ingroup session
 */
typedef void (*session_sleep_for_frame_fn)(void *user_data);

/**
 * @brief Unified render loop for all display modes
 *
 * Flexible render loop that supports both synchronous and event-driven modes.
 *
 * **Synchronous Mode** (capture != NULL, callbacks == NULL):
 * Handles complete render lifecycle:
 * - Frame rate limiting via session_capture_sleep_for_fps()
 * - Frame capture via session_capture_read_frame()
 * - ASCII conversion via session_display_convert_to_ascii()
 * - Terminal rendering via session_display_render_frame()
 * - Memory cleanup with SAFE_FREE()
 *
 * Used by: mirror mode, discovery participant mode
 *
 * **Event-Driven Mode** (capture == NULL, callbacks != NULL):
 * Delegates all frame operations to callbacks:
 * - Custom frame capture via capture callback (can return NULL)
 * - Custom timing via sleep_for_frame callback
 * - ASCII conversion via session_display_convert_to_ascii()
 * - Terminal rendering via session_display_render_frame()
 * - Memory cleanup with SAFE_FREE()
 *
 * Used by: client mode, async frame sources
 *
 * @param capture Capture context for synchronous mode (NULL if using callbacks)
 * @param display Display context (must not be NULL)
 * @param should_exit Callback to check exit condition each iteration (must not be NULL)
 * @param capture_cb Custom capture callback for event-driven mode (NULL if using capture context)
 * @param sleep_cb Custom sleep callback for event-driven mode (NULL if using capture context)
 * @param user_data Opaque pointer passed to all callbacks
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Either capture XOR (capture_cb && sleep_cb) must be provided, not both
 * @note Caller must free capture and display contexts after this returns
 * @note In synchronous mode, capture callback can return NULL on frame unavailability
 * @note In event-driven mode, capture callback can return NULL gracefully
 *
 * @par Synchronous Example (Mirror Mode)
 * @code
 * static bool my_should_exit(void *user_data) {
 *   return atomic_load(&g_exit_flag);
 * }
 *
 * int main(void) {
 *   session_capture_ctx_t *capture = session_capture_create(NULL);
 *   session_display_ctx_t *display = session_display_create(NULL);
 *
 *   platform_set_console_ctrl_handler(my_ctrl_handler);
 *
 *   asciichat_error_t result = session_render_loop(
 *       capture, display, my_should_exit,
 *       NULL, NULL,  // No custom callbacks
 *       NULL         // No user_data needed
 *   );
 *
 *   session_display_destroy(display);
 *   session_capture_destroy(capture);
 *
 *   return result == ASCIICHAT_OK ? 0 : (int)result;
 * }
 * @endcode
 *
 * @par Event-Driven Example (Client Mode)
 * @code
 * static image_t *client_capture_frame(void *user_data) {
 *   client_ctx_t *ctx = (client_ctx_t *)user_data;
 *   return network_frame_queue_pop_nonblocking(ctx->frame_queue);
 * }
 *
 * static void client_sleep_for_frame(void *user_data) {
 *   client_ctx_t *ctx = (client_ctx_t *)user_data;
 *   if (network_frame_queue_empty(ctx->frame_queue)) {
 *     platform_sleep_ms(10);
 *   }
 * }
 *
 * static bool client_should_exit(void *user_data) {
 *   return !server_connection_is_active();
 * }
 *
 * int main(void) {
 *   session_display_ctx_t *display = session_display_create(NULL);
 *   client_ctx_t client_ctx = {...};
 *
 *   asciichat_error_t result = session_render_loop(
 *       NULL, display, client_should_exit,  // NULL capture = event-driven
 *       client_capture_frame,               // Custom capture
 *       client_sleep_for_frame,             // Custom sleep
 *       &client_ctx                         // Context for callbacks
 *   );
 *
 *   session_display_destroy(display);
 *   return result == ASCIICHAT_OK ? 0 : (int)result;
 * }
 * @endcode
 *
 * @ingroup session
 */
asciichat_error_t session_render_loop(session_capture_ctx_t *capture, session_display_ctx_t *display,
                                      session_should_exit_fn should_exit, session_capture_fn capture_cb,
                                      session_sleep_for_frame_fn sleep_cb, void *user_data);

#ifdef __cplusplus
}
#endif
