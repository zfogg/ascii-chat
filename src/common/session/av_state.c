#include "session/av_state.h"

uint32_t session_av_state_begin(session_av_state_t *state) {
  if (!state || atomic_load_int(&state->phase) != SESSION_AV_STATE_IDLE)
    return 0;
  state->generation = state->generation == UINT32_MAX ? 1 : state->generation + 1;
  atomic_store_int(&state->phase, SESSION_AV_STATE_STARTING);
  return state->generation;
}

bool session_av_state_is_current(session_av_state_t *state, uint32_t generation) {
  return state && generation && state->generation == generation && session_av_state_should_run(state);
}

bool session_av_state_complete(session_av_state_t *state, uint32_t generation) {
  if (!session_av_state_is_current(state, generation) || atomic_load_int(&state->phase) != SESSION_AV_STATE_STARTING)
    return false;
  atomic_store_int(&state->phase, SESSION_AV_STATE_ACTIVE);
  return true;
}

bool session_av_state_is_active(session_av_state_t *state) {
  return state && atomic_load_int(&state->phase) == SESSION_AV_STATE_ACTIVE;
}

bool session_av_state_should_run(session_av_state_t *state) {
  if (!state)
    return false;
  int phase = atomic_load_int(&state->phase);
  return phase == SESSION_AV_STATE_STARTING || phase == SESSION_AV_STATE_ACTIVE;
}

void session_av_state_begin_stop(session_av_state_t *state) {
  if (state)
    atomic_store_int(&state->phase, SESSION_AV_STATE_STOPPING);
}

void session_av_state_finish_stop(session_av_state_t *state) {
  if (state)
    atomic_store_int(&state->phase, SESSION_AV_STATE_IDLE);
}
