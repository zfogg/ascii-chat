#include "session/av_state.h"
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

EMSCRIPTEN_KEEPALIVE session_av_state_t *session_av_state_create(void) {
  session_av_state_t *state = SAFE_CALLOC(1, sizeof(session_av_state_t), session_av_state_t *);
  if (state)
    NAMED_REGISTER_ATOMIC(&state->phase, "session_av.phase", NULL);
  return state;
}

EMSCRIPTEN_KEEPALIVE void session_av_state_destroy(session_av_state_t *state) {
  if (state)
    NAMED_UNREGISTER(&state->phase);
  SAFE_FREE(state);
}

EMSCRIPTEN_KEEPALIVE uint32_t session_av_state_generation(const session_av_state_t *state) {
  return state ? state->generation : 0;
}

EMSCRIPTEN_KEEPALIVE uint32_t session_av_state_begin(session_av_state_t *state) {
  if (!state || atomic_load_int(&state->phase) != SESSION_AV_STATE_IDLE)
    return 0;
  state->generation = state->generation == UINT32_MAX ? 1 : state->generation + 1;
  atomic_store_int(&state->phase, SESSION_AV_STATE_STARTING);
  return state->generation;
}

EMSCRIPTEN_KEEPALIVE bool session_av_state_is_current(session_av_state_t *state, uint32_t generation) {
  return state && generation && state->generation == generation && session_av_state_should_run(state);
}

EMSCRIPTEN_KEEPALIVE bool session_av_state_complete(session_av_state_t *state, uint32_t generation) {
  if (!session_av_state_is_current(state, generation) || atomic_load_int(&state->phase) != SESSION_AV_STATE_STARTING)
    return false;
  atomic_store_int(&state->phase, SESSION_AV_STATE_ACTIVE);
  return true;
}

EMSCRIPTEN_KEEPALIVE bool session_av_state_is_active(session_av_state_t *state) {
  return state && atomic_load_int(&state->phase) == SESSION_AV_STATE_ACTIVE;
}

EMSCRIPTEN_KEEPALIVE bool session_av_state_should_run(session_av_state_t *state) {
  if (!state)
    return false;
  int phase = atomic_load_int(&state->phase);
  return phase == SESSION_AV_STATE_STARTING || phase == SESSION_AV_STATE_ACTIVE;
}

EMSCRIPTEN_KEEPALIVE void session_av_state_begin_stop(session_av_state_t *state) {
  if (state)
    atomic_store_int(&state->phase, SESSION_AV_STATE_STOPPING);
}

EMSCRIPTEN_KEEPALIVE void session_av_state_finish_stop(session_av_state_t *state) {
  if (state)
    atomic_store_int(&state->phase, SESSION_AV_STATE_IDLE);
}
