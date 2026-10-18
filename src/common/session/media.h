/** @file session/media.h
 * Shared stream lifecycle for native workers and asynchronous host capture.
 * The owner serializes transitions; workers may query should_run atomically.
 * Adapters release their resources before finish_stop, including failed starts.
 */
#pragma once

#include <ascii-chat/atomic.h>

typedef enum {
  SESSION_MEDIA_IDLE,
  SESSION_MEDIA_STARTING,
  SESSION_MEDIA_ACTIVE,
  SESSION_MEDIA_STOPPING,
} session_media_phase_t;

/**
 * @brief Shared lifecycle state for a native or asynchronous host media stream.
 *
 * Zero initialization represents an idle stream with no startup token.
 * The owner serializes transitions and generation access; workers may query
 * the atomic phase through session_media_should_run().
 */
typedef struct {
  atomic_t phase;      ///< Current session_media_phase_t, queried atomically by workers.
  uint32_t generation; ///< Owner-managed startup token used to reject stale asynchronous completions.
} session_media_t;

/** Returns a nonzero startup token, or zero when already starting/active/stopping. */
uint32_t session_media_begin(session_media_t *media);
/** Only the current startup can become active; cancelled completions return false. */
bool session_media_complete(session_media_t *media, uint32_t generation);
bool session_media_is_current(session_media_t *media, uint32_t generation);
bool session_media_is_active(session_media_t *media);
bool session_media_should_run(session_media_t *media);
/** Invalidate pending startup before the adapter stops and joins/releases resources. */
void session_media_begin_stop(session_media_t *media);
void session_media_finish_stop(session_media_t *media);
