#include <ascii-chat/video/face_follow.h>
#include <emscripten/emscripten.h>

static face_follow_state_t state;
static unsigned char rgba[320 * 240 * 4], bgr[320 * 240 * 3], results[0x9000];
static float output[11];
static size_t face_count;

EMSCRIPTEN_KEEPALIVE void face_web_reset(void) {
  face_follow_init(&state);
  face_count = 0;
}

EMSCRIPTEN_KEEPALIVE unsigned char *face_web_input(void) {
  return rgba;
}

EMSCRIPTEN_KEEPALIVE void face_web_detect(int width, int height, double now) {
  if (width < 1 || height < 1 || width > 320 || height > 240)
    return;
  for (int i = 0; i < width * height; i++) {
    bgr[i * 3] = rgba[i * 4 + 2];
    bgr[i * 3 + 1] = rgba[i * 4 + 1];
    bgr[i * 3 + 2] = rgba[i * 4];
  }
  face_follow_detection_t faces[32];
  face_follow_detect(results, bgr, width, height, faces, 32, &face_count);
  face_follow_observe(&state, faces, face_count, now);
}

EMSCRIPTEN_KEEPALIVE float *face_web_tick(double now, int enabled) {
  face_follow_tick(&state, now, enabled != 0);
  output[0] = state.crop.x;
  output[1] = state.crop.y;
  output[2] = state.crop.w;
  output[3] = state.crop.h;
  output[4] = state.face.x;
  output[5] = state.face.y;
  output[6] = state.face.w;
  output[7] = state.face.h;
  output[8] = state.detected && now - state.last_seen < 0.3;
  output[9] = (float)face_count;
  output[10] = state.tracking && now - state.last_seen <= 0.7;
  return output;
}
