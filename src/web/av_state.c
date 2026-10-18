/** Browser host bindings for the audio/video state machine used by native participants. */
#include "session/av_state.h"
#include <ascii-chat/common.h>
#include <emscripten/emscripten.h>

EMSCRIPTEN_KEEPALIVE session_av_state_t *web_av_state_create(void) {
  return SAFE_CALLOC(1, sizeof(session_av_state_t), session_av_state_t *);
}

EMSCRIPTEN_KEEPALIVE void web_av_state_destroy(session_av_state_t *state) {
  SAFE_FREE(state);
}

EMSCRIPTEN_KEEPALIVE uint32_t web_av_state_generation(session_av_state_t *state) {
  return state ? state->generation : 0;
}
