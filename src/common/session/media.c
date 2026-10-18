#include "session/media.h"

uint32_t session_media_begin(session_media_t *media) {
  if (!media || atomic_load_int(&media->phase) != SESSION_MEDIA_IDLE)
    return 0;
  media->generation = media->generation == UINT32_MAX ? 1 : media->generation + 1;
  atomic_store_int(&media->phase, SESSION_MEDIA_STARTING);
  return media->generation;
}

bool session_media_is_current(session_media_t *media, uint32_t generation) {
  return media && generation && media->generation == generation && session_media_should_run(media);
}

bool session_media_complete(session_media_t *media, uint32_t generation) {
  if (!session_media_is_current(media, generation) || atomic_load_int(&media->phase) != SESSION_MEDIA_STARTING)
    return false;
  atomic_store_int(&media->phase, SESSION_MEDIA_ACTIVE);
  return true;
}

bool session_media_is_active(session_media_t *media) {
  return media && atomic_load_int(&media->phase) == SESSION_MEDIA_ACTIVE;
}

bool session_media_should_run(session_media_t *media) {
  if (!media)
    return false;
  int phase = atomic_load_int(&media->phase);
  return phase == SESSION_MEDIA_STARTING || phase == SESSION_MEDIA_ACTIVE;
}

void session_media_begin_stop(session_media_t *media) {
  if (media)
    atomic_store_int(&media->phase, SESSION_MEDIA_STOPPING);
}

void session_media_finish_stop(session_media_t *media) {
  if (media)
    atomic_store_int(&media->phase, SESSION_MEDIA_IDLE);
}
