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
    state->pan_pending = state->zoom_pending = false;
    state->misses++;
    return;
  }
  bool acquired = !retain;
  state->face = faces[selected].box;
  state->last_seen = now;
  state->tracking = true;
  state->misses = 0;
  face_follow_rect_t box = state->face;
  // Retain the source aspect ratio and let the face fill about two thirds of the view.
  float size = clampf(fmaxf(box.w * 1.8f, box.h * 1.6f), 0.35f, 1.0f);
  float cx = box.x + box.w * 0.5f;
  float cy = box.y + box.h * 0.55f;
  // Measure against the last accepted face position, not the moving/clamped crop.
  // Keep the entire composition fixed through breathing and detector box noise.
  float dx = fabsf(cx - state->anchor_x) / state->target.w;
  float dy = fabsf(cy - state->anchor_y) / state->target.h;
  if (state->pan_pending &&
      (cx - state->anchor_x) * state->pan_direction_x + (cy - state->anchor_y) * state->pan_direction_y <= 0)
    state->pan_pending = false;
  if (dx < 0.12f && dy < 0.09f)
    state->pan_pending = false;
  else if (!state->pan_pending && (dx > 0.18f || dy > 0.14f)) {
    state->pan_pending = true;
    state->pan_outside_since = now;
    state->pan_direction_x = cx - state->anchor_x;
    state->pan_direction_y = cy - state->anchor_y;
  }
  // Large moves near the edge still need two or more consistent detections.
  double pan_grace = dx > 0.35f || dy > 0.25f ? 0.2 : 0.85;
  bool pan = acquired || (state->pan_pending && now - state->pan_outside_since >= pan_grace);
  float zoom_delta = logf(size / state->target.w);
  float zoom_change = fabsf(zoom_delta);
  if (state->zoom_pending && zoom_delta * state->zoom_direction <= 0)
    state->zoom_pending = false;
  if (zoom_change < 0.12f)
    state->zoom_pending = false;
  else if (!state->zoom_pending && zoom_change > 0.18f) {
    state->zoom_pending = true;
    state->zoom_outside_since = now;
    state->zoom_direction = zoom_delta;
  }
  bool zoom = acquired || (state->zoom_pending && now - state->zoom_outside_since >= 1.2);
  if (pan) {
    state->anchor_x = cx;
    state->anchor_y = cy;
    state->pan_pending = false;
  }
  if (zoom) {
    state->target.w = state->target.h = size;
    state->zoom_pending = false;
  }
  if (pan || zoom) {
    state->target.x = clampf(state->anchor_x - state->target.w * 0.5f, 0, 1 - state->target.w);
    state->target.y = clampf(state->anchor_y - state->target.h * 0.5f, 0, 1 - state->target.h);
  }
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
  float pan = (float)(1 - exp(-dt / (lost || !enabled ? 0.45 : 0.28)));
  float zoom = (float)(1 - exp(-dt / (lost || !enabled ? 0.45 : 0.35)));
  float cx = state->crop.x + state->crop.w * 0.5f;
  float cy = state->crop.y + state->crop.h * 0.5f;
  cx += (target.x + target.w * 0.5f - cx) * pan;
  cy += (target.y + target.h * 0.5f - cy) * pan;
  if (fabsf(cx - (target.x + target.w * 0.5f)) < 0.0001f)
    cx = target.x + target.w * 0.5f;
  if (fabsf(cy - (target.y + target.h * 0.5f)) < 0.0001f)
    cy = target.y + target.h * 0.5f;
  float size = expf(logf(state->crop.w) + (logf(target.w) - logf(state->crop.w)) * zoom);
  if (fabsf(size - target.w) < 0.0001f)
    size = target.w;
  state->crop =
      (face_follow_rect_t){clampf(cx - size * 0.5f, 0, 1 - size), clampf(cy - size * 0.5f, 0, 1 - size), size, size};
}
