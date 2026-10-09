#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/video/rgba/image.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  ANIMATION_SPLASH_RAINBOW,
  ANIMATION_DIGITAL_RAIN,
  ANIMATION_RAINBOW_FILTER,
  ANIMATION_TEST_PATTERN,
} animation_type_t;

typedef enum { ANIMATION_HIDDEN_PAUSE, ANIMATION_HIDDEN_CONTINUE } animation_hidden_policy_t;

// Instances belong to their producer thread. Pass samples by value to renderers;
// no mutable instance is shared with the presentation thread.
typedef struct {
  animation_type_t type;
  uint32_t fps;
  double speed;
  animation_hidden_policy_t hidden_policy;
} animation_config_t;

typedef struct {
  animation_type_t type;
  double seconds;
  uint64_t frame;
  bool changed;
  uint64_t next_frame_ns; // In the supplied timestamp domain; UINT64_MAX if suspended.
} animation_sample_t;

typedef struct {
  animation_config_t config;
  double seconds;
  uint64_t last_ns;
  uint64_t last_frame;
  bool started, paused, visible;
} animation_t;

asciichat_error_t animation_init(animation_t *animation, const char *name, animation_config_t config);
void animation_destroy(animation_t *animation);
void animation_reset(animation_t *animation);
void animation_set_paused(animation_t *animation, bool paused);
void animation_set_visible(animation_t *animation, bool visible);
asciichat_error_t animation_set_speed(animation_t *animation, double speed);
// Update at monotonic live timestamps or explicit export/test timestamps.
// Clock changes are applied at the most recent update; update before changing controls.
asciichat_error_t animation_update(animation_t *animation, uint64_t timestamp_ns, animation_sample_t *out);
// For callers already supplying elapsed media time. Does not allocate or register an instance.
asciichat_error_t animation_sample_at(animation_config_t config, double seconds, animation_sample_t *out);

struct digital_rain;
typedef enum { ANIMATION_TARGET_COLOR, ANIMATION_TARGET_ANSI, ANIMATION_TARGET_IMAGE } animation_target_type_t;
typedef struct {
  animation_target_type_t type;
  union {
    struct {
      double position;
      rgb_pixel_t *out;
    } color;
    // ANSI output is allocated; caller releases it with SAFE_FREE.
    struct {
      const char *input;
      char **out;
      struct digital_rain *rain;
    } ansi;
    image_t *image;
  };
} animation_target_t;
asciichat_error_t animation_apply(const animation_sample_t *sample, animation_target_t *target);
