#include <ascii-chat/video/face_follow.h>
#include <criterion/criterion.h>
#include <math.h>

static const face_follow_detection_t subject = {{0.2f, 0.3f, 0.12f, 0.2f}, 0.95f};

static void advance(face_follow_state_t *state, double from, double to, bool observe, int fps) {
  for (double t = from; t < to; t += 1.0 / fps) {
    if (observe)
      face_follow_observe(state, &subject, 1, t);
    face_follow_tick(state, t, true);
    cr_assert(isfinite(state->crop.w));
    cr_assert_geq(state->crop.x, 0);
    cr_assert_geq(state->crop.y, 0);
    cr_assert_leq(state->crop.x + state->crop.w, 1.00001);
    cr_assert_leq(state->crop.y + state->crop.h, 1.00001);
  }
}

Test(face_follow, no_face_stays_full_frame) {
  face_follow_state_t state;
  face_follow_init(&state);
  advance(&state, 0, 3, false, 60);
  cr_assert_float_eq(state.crop.w, 1, 0.0001);
  cr_assert_float_eq(state.crop.x, 0, 0.0001);
}

Test(face_follow, typical_webcam_face_gets_a_visible_crop) {
  face_follow_state_t state;
  face_follow_init(&state);
  face_follow_detection_t webcam = {{0.35625f, 0.23333f, 0.221875f, 0.4125f}, 0.99f};
  for (double t = 0; t < 3; t += 1.0 / 60) {
    face_follow_observe(&state, &webcam, 1, t);
    face_follow_tick(&state, t, true);
  }
  cr_assert_lt(state.crop.w, 0.7);
  cr_assert_gt(webcam.box.h / state.crop.h, 0.6);
  cr_assert_float_eq(state.crop.x + state.crop.w * 0.5f, webcam.box.x + webcam.box.w * 0.5f, state.crop.w * 0.07f);
}

Test(face_follow, short_loss_holds_then_smoothly_widens) {
  face_follow_state_t state;
  face_follow_init(&state);
  advance(&state, 0, 3, true, 60);
  float framed = state.crop.w;
  cr_assert_lt(framed, 0.5);
  face_follow_observe(&state, NULL, 0, 3);
  advance(&state, 3, 3.5, false, 60);
  cr_assert_float_eq(state.crop.w, framed, 0.001);
  float previous = state.crop.w;
  for (double t = 3.7; t < 7; t += 1.0 / 60) {
    face_follow_tick(&state, t, true);
    cr_assert_geq(state.crop.w, previous - 0.00001);
    cr_assert_lt(state.crop.w - previous, 0.13);
    previous = state.crop.w;
  }
  cr_assert_gt(state.crop.w, 0.999);
}

Test(face_follow, larger_bystander_does_not_steal_subject) {
  face_follow_state_t state;
  face_follow_init(&state);
  advance(&state, 0, 2, true, 60);
  face_follow_detection_t faces[] = {{{0.65f, 0.1f, 0.3f, 0.5f}, 0.99f}, subject};
  face_follow_observe(&state, faces, 2, 2);
  cr_assert_float_eq(state.face.x, subject.box.x, 0.0001);
  face_follow_observe(&state, faces, 1, 2.1);
  cr_assert_not(state.detected);
  cr_assert_float_eq(state.face.x, subject.box.x, 0.0001);
  face_follow_tick(&state, 4, true);
  face_follow_observe(&state, faces, 1, 4);
  cr_assert(state.detected);
  cr_assert_float_eq(state.face.x, faces[0].box.x, 0.0001);
}

Test(face_follow, behavior_is_independent_of_render_fps) {
  face_follow_state_t slow, fast;
  face_follow_init(&slow);
  face_follow_init(&fast);
  advance(&slow, 0, 3, true, 30);
  advance(&fast, 0, 3, true, 120);
  cr_assert_float_eq(slow.crop.x, fast.crop.x, 0.003);
  cr_assert_float_eq(slow.crop.w, fast.crop.w, 0.003);
}

Test(face_follow, disabling_returns_to_full_frame) {
  face_follow_state_t state;
  face_follow_init(&state);
  advance(&state, 0, 2, true, 60);
  for (double t = 2; t < 7; t += 1.0 / 60)
    face_follow_tick(&state, t, false);
  cr_assert_float_eq(state.crop.w, 1, 0.0001);
}

static face_follow_detection_t seated(float dx, float dy, float scale) {
  float w = 0.23f * scale, h = 0.4f * scale;
  return (face_follow_detection_t){{0.5f + dx - w * 0.5f, 0.45f + dy - h * 0.55f, w, h}, 0.99f};
}

static void settle_seated(face_follow_state_t *state) {
  face_follow_init(state);
  face_follow_detection_t face = seated(0, 0, 1);
  for (double t = 0; t < 3; t += 1.0 / 60) {
    face_follow_observe(state, &face, 1, t);
    face_follow_tick(state, t, true);
  }
}

Test(face_follow, breathing_and_seated_shifts_do_not_move_the_view) {
  face_follow_state_t state;
  settle_seated(&state);
  face_follow_rect_t crop = state.crop;
  for (double t = 3; t < 15; t += 0.1) {
    face_follow_detection_t face =
        seated(0.065f * sinf((float)t * 3), 0.05f * cosf((float)t * 2), 1 + 0.08f * sinf((float)t * 4));
    face_follow_observe(&state, &face, 1, t);
    face_follow_tick(&state, t, true);
    cr_assert_float_eq(state.crop.x, crop.x, 0.0002);
    cr_assert_float_eq(state.crop.y, crop.y, 0.0002);
    cr_assert_float_eq(state.crop.w, crop.w, 0.0002);
  }
}

Test(face_follow, brief_lean_outside_quiet_zone_does_not_start_a_pan) {
  face_follow_state_t state;
  settle_seated(&state);
  face_follow_rect_t crop = state.crop;
  for (double t = 3; t < 6; t += 0.1) {
    face_follow_detection_t face = seated(t < 3.4 ? 0.15f : 0, 0, 1);
    face_follow_observe(&state, &face, 1, t);
    face_follow_tick(&state, t, true);
    cr_assert_float_eq(state.crop.x, crop.x, 0.0002);
  }
}

Test(face_follow, sustained_deliberate_move_pans_after_grace_period) {
  face_follow_state_t state;
  settle_seated(&state);
  face_follow_rect_t crop = state.crop;
  face_follow_detection_t face = seated(0.15f, 0, 1);
  for (double t = 3; t < 3.8; t += 0.1) {
    face_follow_observe(&state, &face, 1, t);
    face_follow_tick(&state, t, true);
    cr_assert_float_eq(state.crop.x, crop.x, 0.0002);
  }
  for (double t = 3.9; t < 6; t += 0.1) {
    face_follow_observe(&state, &face, 1, t);
    face_follow_tick(&state, t, true);
  }
  cr_assert_gt(state.crop.x - crop.x, 0.14);
}

Test(face_follow, zoom_requires_a_sustained_size_change_and_keeps_the_anchor) {
  face_follow_state_t state;
  settle_seated(&state);
  face_follow_rect_t crop = state.crop;
  face_follow_detection_t face = seated(0, 0, 1.25f);
  for (double t = 3; t < 4.1; t += 0.1) {
    face_follow_observe(&state, &face, 1, t);
    face_follow_tick(&state, t, true);
    cr_assert_float_eq(state.crop.w, crop.w, 0.0002);
  }
  for (double t = 4.3; t < 7; t += 0.1) {
    face_follow_observe(&state, &face, 1, t);
    face_follow_tick(&state, t, true);
  }
  cr_assert_gt(state.crop.w, 0.79);
  cr_assert_float_eq(state.crop.x + state.crop.w / 2, crop.x + crop.w / 2, 0.0002);
  cr_assert_float_eq(state.crop.y + state.crop.h / 2, crop.y + crop.h / 2, 0.0002);
}

Test(face_follow, reversing_box_noise_does_not_accumulate_a_direction_or_zoom) {
  face_follow_state_t state;
  face_follow_init(&state);
  face_follow_detection_t face = {{0.3f, 0.23f, 0.4f, 0.4f}, 0.99f};
  for (double t = 0; t < 3; t += 0.1) {
    face_follow_observe(&state, &face, 1, t);
    face_follow_tick(&state, t, true);
  }
  face_follow_rect_t crop = state.crop;
  for (int i = 0; i < 60; i++) {
    float direction = (i / 5) % 2 ? -1 : 1;
    float size = 0.4f * (1 + direction * 0.23f);
    face.box = (face_follow_rect_t){0.5f + direction * 0.14f - size / 2, 0.45f - size * 0.55f, size, size};
    double t = 3 + i * 0.1;
    face_follow_observe(&state, &face, 1, t);
    face_follow_tick(&state, t, true);
    cr_assert(state.detected);
    cr_assert_float_eq(state.crop.x, crop.x, 0.0002);
    cr_assert_float_eq(state.crop.y, crop.y, 0.0002);
    cr_assert_float_eq(state.crop.w, crop.w, 0.0002);
  }
}
