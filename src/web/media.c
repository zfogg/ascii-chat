/** Browser host bindings for the same lifecycle used by native participants. */
#include "session/media.h"
#include <ascii-chat/common.h>
#include <emscripten/emscripten.h>

EMSCRIPTEN_KEEPALIVE session_media_t *web_media_create(void) {
  return SAFE_CALLOC(1, sizeof(session_media_t), session_media_t *);
}

EMSCRIPTEN_KEEPALIVE void web_media_destroy(session_media_t *media) {
  SAFE_FREE(media);
}

EMSCRIPTEN_KEEPALIVE uint32_t web_media_generation(session_media_t *media) {
  return media ? media->generation : 0;
}
