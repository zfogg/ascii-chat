#include <ascii-chat/video/anim/controller.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/video/rgba/color_filter.h>
#include <ascii-chat/debug/named.h>
#include <math.h>
#include <string.h>

#include "backends.h"

static bool valid_config(animation_config_t c) {
  return c.type >= ANIMATION_SPLASH_RAINBOW && c.type <= ANIMATION_TEST_PATTERN && c.fps > 0 && c.fps <= 1000000000 &&
         isfinite(c.speed) && c.speed >= 0 &&
         (c.hidden_policy == ANIMATION_HIDDEN_PAUSE || c.hidden_policy == ANIMATION_HIDDEN_CONTINUE);
}

asciichat_error_t animation_init(animation_t *a, const char *name, animation_config_t config) {
  if (!a || !name || !valid_config(config))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid animation configuration");
  *a = (animation_t){.config = config, .visible = true};
  NAMED_REGISTER(a, name, "animation", NULL, NULL);
  return ASCIICHAT_OK;
}

void animation_destroy(animation_t *a) {
  if (!a)
    return;
  NAMED_UNREGISTER(a);
  memset(a, 0, sizeof(*a));
}

void animation_reset(animation_t *a) {
  if (!a)
    return;
  a->seconds = 0;
  a->started = false;
  a->last_frame = 0;
}
void animation_set_paused(animation_t *a, bool paused) {
  if (a)
    a->paused = paused;
}
void animation_set_visible(animation_t *a, bool visible) {
  if (a)
    a->visible = visible;
}
asciichat_error_t animation_set_speed(animation_t *a, double speed) {
  if (!a || !isfinite(speed) || speed < 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid animation speed");
  a->config.speed = speed;
  return ASCIICHAT_OK;
}

asciichat_error_t animation_sample_at(animation_config_t config, double seconds, animation_sample_t *out) {
  if (!out || !valid_config(config) || !isfinite(seconds) || seconds < 0 || seconds * config.fps >= (double)UINT64_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid animation timestamp");
  *out = (animation_sample_t){.type = config.type,
                              .seconds = seconds,
                              .frame = (uint64_t)(seconds * config.fps),
                              .changed = true,
                              .next_frame_ns = UINT64_MAX};
  return ASCIICHAT_OK;
}

asciichat_error_t animation_update(animation_t *a, uint64_t now, animation_sample_t *out) {
  if (!a || !out || !valid_config(a->config) || (a->started && now < a->last_ns))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Animation timestamps must be monotonic; reset before seeking");
  bool running =
      !a->paused && a->config.speed > 0 && (a->visible || a->config.hidden_policy == ANIMATION_HIDDEN_CONTINUE);
  double seconds = a->seconds;
  if (a->started && running)
    seconds += (double)(now - a->last_ns) / 1e9 * a->config.speed;
  asciichat_error_t err = animation_sample_at(a->config, seconds, out);
  if (err != ASCIICHAT_OK)
    return err;
  out->changed = !a->started || out->frame != a->last_frame;
  if (running) {
    double remaining = ((double)out->frame + 1.0) / a->config.fps - seconds;
    double delay = ceil(fmax(1.0, remaining * 1e9 / a->config.speed));
    if (delay < (double)(UINT64_MAX - now))
      out->next_frame_ns = now + (uint64_t)delay;
  }
  a->seconds = seconds;
  a->last_ns = now;
  a->last_frame = out->frame;
  a->started = true;
  return ASCIICHAT_OK;
}

asciichat_error_t animation_apply(const animation_sample_t *s, animation_target_t *t) {
  if (!s || !t || !isfinite(s->seconds) || s->seconds < 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid animation sample or target");
  switch (s->type) {
  case ANIMATION_SPLASH_RAINBOW: {
    if (t->type != ANIMATION_TARGET_COLOR || !t->color.out || !isfinite(t->color.position))
      break;
    static const rgb_pixel_t colors[] = {{255, 0, 0},   {255, 165, 0}, {255, 255, 0}, {0, 255, 0},
                                         {0, 255, 255}, {0, 0, 255},   {255, 0, 255}};
    double position = fmod(t->color.position, 1.0) + fmod(s->seconds, 2.5) * 0.4;
    double phase = (position - floor(position)) * 6;
    int i = (int)phase;
    double blend = phase - i;
    rgb_pixel_t a = colors[i], b = colors[i + 1];
    *t->color.out =
        (rgb_pixel_t){(uint8_t)(a.r * (1 - blend) + b.r * blend), (uint8_t)(a.g * (1 - blend) + b.g * blend),
                      (uint8_t)(a.b * (1 - blend) + b.b * blend)};
    return ASCIICHAT_OK;
  }
  case ANIMATION_RAINBOW_FILTER:
    if (t->type == ANIMATION_TARGET_COLOR && t->color.out) {
      color_filter_calculate_rainbow((float)fmod(s->seconds, 3.5), &t->color.out->r, &t->color.out->g,
                                     &t->color.out->b);
      return ASCIICHAT_OK;
    }
    if (t->type != ANIMATION_TARGET_ANSI || !t->ansi.input || !t->ansi.out)
      break;
    *t->ansi.out = rainbow_render_ansi_at(t->ansi.input, (float)fmod(s->seconds, 3.5));
    return *t->ansi.out ? ASCIICHAT_OK : SET_ERRNO(ERROR_MEMORY, "Rainbow output allocation failed");
  case ANIMATION_DIGITAL_RAIN:
    if (t->type != ANIMATION_TARGET_ANSI || !t->ansi.input || !t->ansi.out || !t->ansi.rain)
      break;
    *t->ansi.out = digital_rain_render_at(t->ansi.rain, t->ansi.input, (float)s->seconds);
    return *t->ansi.out ? ASCIICHAT_OK : SET_ERRNO(ERROR_MEMORY, "Digital rain output allocation failed");
  case ANIMATION_TEST_PATTERN:
    if (t->type != ANIMATION_TARGET_TEST_PATTERN || !t->test_pattern.context)
      break;
    return test_pattern_render_at(t->test_pattern.context, t->test_pattern.index, s->seconds * 1000.0,
                                  t->test_pattern.cadence);
  }
  return SET_ERRNO(ERROR_INVALID_PARAM, "Animation type does not support this target");
}
