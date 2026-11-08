#pragma once
#include <ascii-chat/common.h>
#include <stdint.h>
#define ASCII_TEMPORAL_MOTION_LIMIT 8
#define ASCII_TEMPORAL_MAX_INTERVAL_NS 250000000ULL
#define ASCII_TEMPORAL_COLOR_TIME_NS 50000000ULL

/** A renderer-owned history; one instance must not be shared by independent streams. */
typedef struct {
  uint8_t samples[16];
  uint8_t rgb_samples[3][16];
  uint32_t glyph;
  uint32_t color;
  uint32_t background;
  uint64_t source_owner;
  bool valid;
} ascii_temporal_cell_t;

typedef struct {
  ascii_temporal_cell_t *cells;
  size_t width, height;
  uint64_t configuration;
  uint64_t elapsed_ns;
  uint64_t last_timestamp_ns;
  uint64_t fixed_step_ns;
} ascii_stabilizer_t;

/** Prepare history for a viewport and rendering contract, resetting it on change. */
asciichat_error_t ascii_stabilizer_prepare(ascii_stabilizer_t *state, size_t width, size_t height,
                                           uint64_t configuration);
void ascii_stabilizer_reset(ascii_stabilizer_t *state);
/** Select history for the current thread until the previous target is restored. */
ascii_stabilizer_t *ascii_stabilizer_set_target(ascii_stabilizer_t *state);
/** Choose a glyph using current and previous candidate errors and patch motion. */
uint32_t ascii_stabilizer_choose(ascii_temporal_cell_t *cell, const uint8_t samples[16], uint32_t candidate,
                                 uint64_t candidate_error, uint64_t previous_error, uint32_t color, uint64_t elapsed_ns,
                                 uint32_t *stabilized_color);
ascii_stabilizer_t *ascii_stabilizer_target(void);
uint32_t ascii_stabilizer_blend_color(uint32_t previous, uint32_t current, unsigned weight);
uint32_t ascii_stabilizer_glyph(ascii_temporal_cell_t *cell, const uint8_t samples[16],
                                const int16_t rgb_samples[3][16], bool colored, uint32_t candidate,
                                uint64_t candidate_error, uint64_t previous_error, uint32_t previous_color,
                                uint64_t elapsed_ns, uint32_t *color, uint32_t *background);
