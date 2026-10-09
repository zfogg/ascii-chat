#pragma once

#include <ascii-chat/video/anim/digital_rain.h>

// Internal effect implementations. Public callers use animation_apply().
char *digital_rain_render_at(digital_rain_t *rain, const char *frame, float seconds);
char *rainbow_render_ansi_at(const char *frame, float seconds);
asciichat_error_t test_pattern_render_at(struct test_pattern *pattern, int index, double time_ms, bool cadence);
