#include <ascii-chat/video/face_follow.h>
#include <math.h>
#include <string.h>

static float clampf(float value, float low, float high) {
  return fminf(high, fmaxf(low, value));
}

static face_follow_rect_t full_frame(void) {
  return (face_follow_rect_t){0, 0, 1, 1};
}

void face_follow_init(face_follow_state_t *state) {
  memset(state, 0, sizeof(*state));
  state->crop = state->target = full_frame();
}

static float overlap(face_follow_rect_t a, face_follow_rect_t b) {
  float w = fmaxf(0, fminf(a.x + a.w, b.x + b.w) - fmaxf(a.x, b.x));
  float h = fmaxf(0, fminf(a.y + a.h, b.y + b.h) - fmaxf(a.y, b.y));
  float intersection = w * h;
  return intersection / fmaxf(0.000001f, a.w * a.h + b.w * b.h - intersection);
}

void face_follow_observe(face_follow_state_t *state, const face_follow_detection_t *faces, size_t count, double now) {
  int selected = -1;
  float best = -1;
  bool retain = state->tracking && now - state->last_seen < 1.5;
  for (size_t i = 0; i < count; i++) {
    face_follow_rect_t box = faces[i].box;
    if (!isfinite(box.x) || !isfinite(box.y) || !isfinite(box.w) || !isfinite(box.h) || faces[i].confidence < 0.65f ||
        box.w <= 0 || box.h <= 0)
      continue;
    float cx = box.x + box.w * 0.5f, cy = box.y + box.h * 0.5f;
    float score;
    if (retain) {
      float dx = cx - (state->face.x + state->face.w * 0.5f);
      float dy = cy - (state->face.y + state->face.h * 0.5f);
      float distance = sqrtf(dx * dx + dy * dy);
      float match = overlap(box, state->face);
      // A bystander must not steal the crop while the selected subject is absent.
      if (match < 0.08f && distance > fmaxf(0.12f, state->face.w * 0.8f))
        continue;
      score = 3 * match - distance;
    } else {
      float dx = cx - 0.5f, dy = cy - 0.45f;
      score = sqrtf(box.w * box.h) - 0.35f * sqrtf(dx * dx + dy * dy);
    }
    if (score > best) {
      best = score;
      selected = (int)i;
    }
  }
  state->detected = selected >= 0;
  if (selected < 0) {
    state->misses++;
    return;
  }
  state->face = faces[selected].box;
  state->last_seen = now;
  state->tracking = true;
  state->misses = 0;
  face_follow_rect_t box = state->face;
  // Retain the source aspect ratio and let the face fill about two thirds of the view.
  float size = clampf(fmaxf(box.w * 1.8f, box.h * 1.6f), 0.35f, 1.0f);
  float cx = box.x + box.w * 0.5f;
  float cy = box.y + box.h * 0.55f;
  face_follow_rect_t target = {clampf(cx - size * 0.5f, 0, 1 - size), clampf(cy - size * 0.5f, 0, 1 - size), size,
                               size};
  // Ignore tiny movements inside the composition, including detector box noise.
  float old_cx = state->target.x + state->target.w * 0.5f;
  float old_cy = state->target.y + state->target.h * 0.5f;
  if (fabsf(cx - old_cx) < size * 0.07f)
    target.x = clampf(old_cx - size * 0.5f, 0, 1 - size);
  if (fabsf(cy - old_cy) < size * 0.07f)
    target.y = clampf(old_cy - size * 0.5f, 0, 1 - size);
  if (fabsf(logf(size / state->target.w)) < 0.08f) {
    target.w = target.h = state->target.w;
    target.x = clampf(target.x, 0, 1 - target.w);
    target.y = clampf(target.y, 0, 1 - target.h);
  }
  state->target = target;
}

void face_follow_tick(face_follow_state_t *state, double now, bool enabled) {
  if (!state->initialized) {
    state->last_tick = now;
    state->initialized = true;
  }
  double dt = fmax(0, fmin(0.1, now - state->last_tick));
  state->last_tick = now;
  bool lost = !state->tracking || now - state->last_seen > 0.7;
  face_follow_rect_t target = enabled && !lost ? state->target : full_frame();
  if (state->tracking && now - state->last_seen > 1.5)
    state->tracking = false;
  float pan = (float)(1 - exp(-dt / (lost || !enabled ? 0.45 : 0.16)));
  float zoom = (float)(1 - exp(-dt / (lost || !enabled ? 0.45 : 0.35)));
  float cx = state->crop.x + state->crop.w * 0.5f;
  float cy = state->crop.y + state->crop.h * 0.5f;
  cx += (target.x + target.w * 0.5f - cx) * pan;
  cy += (target.y + target.h * 0.5f - cy) * pan;
  float size = expf(logf(state->crop.w) + (logf(target.w) - logf(state->crop.w)) * zoom);
  if (fabsf(size - target.w) < 0.0001f)
    size = target.w;
  state->crop =
      (face_follow_rect_t){clampf(cx - size * 0.5f, 0, 1 - size), clampf(cy - size * 0.5f, 0, 1 - size), size, size};
}
