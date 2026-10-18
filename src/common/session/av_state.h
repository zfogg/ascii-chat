/** @file session/av_state.h
 * Shared audio/video run state for native workers and asynchronous host adapters.
 * The owner serializes transitions; workers may query should_run atomically.
 * Adapters release their resources before finish_stop, including failed starts.
 */
#pragma once

#include <ascii-chat/atomic.h>

typedef enum {
  SESSION_AV_STATE_IDLE,
  SESSION_AV_STATE_STARTING,
  SESSION_AV_STATE_ACTIVE,
  SESSION_AV_STATE_STOPPING,
} session_av_state_phase_t;

/**
 * @brief Shared run state for a native or asynchronous host audio/video stream.
 *
 * Zero initialization represents an idle stream with no startup token.
 * The owner serializes transitions and generation access; workers may query
 * the atomic phase through session_av_state_should_run().
 */
typedef struct {
  atomic_t phase;      ///< Current session_av_state_phase_t, queried atomically by workers.
  uint32_t generation; ///< Owner-managed startup token used to reject stale asynchronous completions.
} session_av_state_t;

/**
 * @brief Allocate an idle audio/video state for callers that need a separate handle.
 * @return Owned state, or NULL on allocation failure. Embedded states may still use zero initialization.
 */
session_av_state_t *session_av_state_create(void);
/**
 * @brief Free a state allocated by session_av_state_create(); NULL is accepted.
 * @note Stop resources and settle pending startup callbacks before destroying the state.
 * Do not pass stack-allocated or embedded states.
 */
void session_av_state_destroy(session_av_state_t *state);
/** @brief Read the owner-managed startup generation; returns zero for NULL or a new state. */
uint32_t session_av_state_generation(const session_av_state_t *state);

/** Returns a nonzero startup token, or zero when already starting/active/stopping. */
uint32_t session_av_state_begin(session_av_state_t *state);
/** Only the current startup can become active; cancelled completions return false. */
bool session_av_state_complete(session_av_state_t *state, uint32_t generation);
bool session_av_state_is_current(session_av_state_t *state, uint32_t generation);
bool session_av_state_is_active(session_av_state_t *state);
bool session_av_state_should_run(session_av_state_t *state);
/** Invalidate pending startup before the adapter stops and joins/releases resources. */
void session_av_state_begin_stop(session_av_state_t *state);
void session_av_state_finish_stop(session_av_state_t *state);
