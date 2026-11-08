#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/stabilize.h>
#include <ascii-chat/video/ascii/render/common.h>
#include <string.h>
#include <stdlib.h>

static _Thread_local ascii_stabilizer_t *g_stabilizer_target;

ascii_stabilizer_t *ascii_stabilizer_target(void) {
  return g_stabilizer_target;
}

ascii_stabilizer_t *ascii_stabilizer_set_target(ascii_stabilizer_t *state) {
  ascii_stabilizer_t *previous = g_stabilizer_target;
  g_stabilizer_target = state;
  return previous;
}

void ascii_stabilizer_reset(ascii_stabilizer_t *state) {
  if (!state)
    return;
  SAFE_FREE(state->cells);
  memset(state, 0, sizeof(*state));
}

asciichat_error_t ascii_stabilizer_prepare(ascii_stabilizer_t *state, size_t width, size_t height,
                                           uint64_t configuration) {
  if (!state || !width || !height || width > SIZE_MAX / height ||
      width * height > SIZE_MAX / sizeof(ascii_temporal_cell_t))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid stabilization viewport");
  if (state->cells && state->width == width && state->height == height && state->configuration == configuration)
    return ASCIICHAT_OK;
  ascii_temporal_cell_t *cells = SAFE_CALLOC(width * height, sizeof(*cells), ascii_temporal_cell_t *);
  if (!cells)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate stabilization history");
  SAFE_FREE(state->cells);
  state->cells = cells;
  state->width = width;
  state->height = height;
  state->configuration = configuration;
  state->last_timestamp_ns = 0;
  state->elapsed_ns = UINT64_MAX;
  return ASCIICHAT_OK;
}

uint32_t ascii_stabilizer_blend_color(uint32_t previous, uint32_t current, unsigned weight) {
  uint32_t result = 0;
  for (unsigned shift = 0; shift <= 16; shift += 8) {
    unsigned old_channel = (previous >> shift) & 255;
    unsigned new_channel = (current >> shift) & 255;
    result |= (uint32_t)((old_channel * (256 - weight) + new_channel * weight + 128) / 256) << shift;
  }
  return result;
}

static unsigned source_color_motion(uint32_t previous, uint32_t current) {
  unsigned difference = 0;
  for (unsigned shift = 0; shift <= 16; shift += 8)
    difference += (unsigned)abs((int)((previous >> shift) & 255) - (int)((current >> shift) & 255));
  return difference / 3;
}

uint32_t ascii_stabilizer_choose(ascii_temporal_cell_t *cell, const uint8_t samples[16], uint32_t candidate,
                                 uint64_t candidate_error, uint64_t previous_error, uint32_t color, uint64_t elapsed_ns,
                                 uint32_t *stabilized_color) {
  unsigned motion = 0;
  if (cell->valid) {
    for (size_t i = 0; i < 16; ++i)
      motion += (unsigned)abs((int)samples[i] - (int)cell->samples[i]);
    motion /= 16;
  }
  uint32_t result = candidate;
  uint32_t output_color = color;
  if (cell->valid && motion <= ASCII_TEMPORAL_MOTION_LIMIT && elapsed_ns <= ASCII_TEMPORAL_MAX_INTERVAL_NS) {
    uint64_t margin = candidate_error / 8 + 128;
    if (previous_error != UINT64_MAX &&
        (previous_error <= candidate_error || previous_error - candidate_error <= margin))
      result = cell->glyph;
    // Weight depends on elapsed time, avoiding a different response at each FPS.
    unsigned weight = (unsigned)(elapsed_ns * 256 / (elapsed_ns + ASCII_TEMPORAL_COLOR_TIME_NS));
    output_color = ascii_stabilizer_blend_color(cell->color, color, weight);
  }
  memcpy(cell->samples, samples, sizeof(cell->samples));
  cell->glyph = result;
  cell->color = output_color;
  cell->valid = true;
  if (stabilized_color)
    *stabilized_color = output_color;
  return result;
}

uint32_t ascii_stabilizer_glyph(ascii_temporal_cell_t *cell, const uint8_t samples[16],
                                const int16_t rgb_samples[3][16], bool colored, uint32_t candidate,
                                uint64_t candidate_error, uint64_t previous_error, uint32_t previous_color,
                                uint64_t elapsed_ns, uint32_t *color, uint32_t *background) {
  ascii_temporal_cell_t old = *cell;
  unsigned motion = 0, color_motion = 0;
  for (int i = 0; i < 16; ++i)
    motion += (unsigned)abs((int)samples[i] - (int)old.samples[i]);
  if (old.valid && colored) {
    for (int channel = 0; channel < 3; ++channel)
      for (int i = 0; i < 16; ++i)
        color_motion += (unsigned)abs(rgb_samples[channel][i] - (int)old.rgb_samples[channel][i]);
    color_motion /= 48;
  }
  uint64_t effective_elapsed = color_motion > ASCII_TEMPORAL_MOTION_LIMIT ? UINT64_MAX : elapsed_ns;
  uint32_t selected = ascii_stabilizer_choose(cell, samples, candidate, candidate_error, previous_error, *color,
                                              effective_elapsed, color);
  if (selected != candidate) {
    *cell = old;
    selected = ascii_stabilizer_choose(cell, samples, candidate, candidate_error, previous_error, previous_color,
                                       effective_elapsed, color);
  }
  if (old.valid && motion / 16 <= ASCII_TEMPORAL_MOTION_LIMIT && color_motion <= ASCII_TEMPORAL_MOTION_LIMIT &&
      elapsed_ns <= ASCII_TEMPORAL_MAX_INTERVAL_NS) {
    unsigned weight = (unsigned)(elapsed_ns * 256 / (elapsed_ns + ASCII_TEMPORAL_COLOR_TIME_NS));
    *background = ascii_stabilizer_blend_color(old.background, *background, weight);
  }
  for (int channel = 0; channel < 3; ++channel)
    for (int i = 0; i < 16; ++i)
      cell->rgb_samples[channel][i] = (uint8_t)rgb_samples[channel][i];
  cell->background = *background;
  return selected;
}

void ascii_stabilizer_frame_row(ascii_frame_t *frame, ascii_color_t mode, bool background, const simd_cells_t *ops) {
  if (frame->stabilizer && !frame->temporal_preselected) {
    const uint64_t elapsed = frame->stabilizer->elapsed_ns;
    const bool recent = elapsed <= ASCII_TEMPORAL_MAX_INTERVAL_NS;
    const unsigned weight = recent ? (unsigned)(elapsed * 256 / (elapsed + ASCII_TEMPORAL_COLOR_TIME_NS)) : 256;
    for (size_t x = 0; x < frame->width; ++x) {
      ascii_temporal_cell_t *cell = &frame->stabilizer->cells[frame->row * frame->width + x];
      uint8_t luma = frame->luma[x];
      uint32_t glyph = frame->glyph[x];
      uint32_t color = frame->packed[x];
      if (cell->valid && recent && abs((int)luma - (int)cell->samples[0]) <= ASCII_TEMPORAL_MOTION_LIMIT) {
        if (cell->glyph != glyph) {
          // Only nearby brightness bins can win the hysteresis test. Searching
          // outward avoids scanning the entire 64-entry palette for every cell.
          unsigned bin = luma >> 2;
          int best_delta = (int)luma - ((int)bin * 4 + 2);
          uint64_t best_error = (uint64_t)(best_delta * best_delta);
          uint64_t margin = best_error / 8 + 128;
          for (unsigned distance = 1; distance < 64; ++distance) {
            bool found = false;
            for (int direction = -1; direction <= 1; direction += 2) {
              int index = (int)bin + direction * (int)distance;
              if (index < 0 || index >= 64 || frame->table[index] != cell->glyph)
                continue;
              int delta = (int)luma - (index * 4 + 2);
              uint64_t error = (uint64_t)(delta * delta);
              if (error <= best_error || error - best_error <= margin)
                glyph = cell->glyph;
              found = true;
            }
            // Bins farther than this cannot fall within the error margin.
            int nearest_delta = (int)distance * 4 - 3;
            if (found || (uint64_t)(nearest_delta * nearest_delta) > best_error + margin)
              break;
          }
        }
        if (mode == ASCII_COLOR_NONE || source_color_motion(cell->source_color, color) <= ASCII_TEMPORAL_MOTION_LIMIT)
          color = ascii_stabilizer_blend_color(cell->color, color, weight);
      }
      cell->source_color = frame->packed[x];
      cell->samples[0] = luma;
      cell->glyph = frame->glyph[x] = glyph;
      cell->color = frame->packed[x] = color;
      cell->valid = true;
      if (background)
        frame->luma[x] =
            (uint8_t)((77u * (color >> 16) + 150u * ((color >> 8) & 255) + 29u * (color & 255) + 128u) >> 8);
    }
    if (!frame->dithered && (mode == ASCII_COLOR_16 || mode == ASCII_COLOR_256))
      ops->quantize(frame->packed, frame->index, frame->width, mode == ASCII_COLOR_16 ? 16 : 256);
  }
}

void ascii_stabilizer_halfrow(ascii_frame_t *frame, ascii_color_t mode, const simd_cells_t *ops) {
  if (frame->stabilizer) {
    size_t top_row = frame->row * 2;
    bool has_bottom = top_row + 1 < frame->source_height;
    for (size_t x = 0; x < frame->width; ++x) {
      uint32_t *pixels[] = {&frame->packed[x], &frame->index[x]};
      for (size_t half = 0; half < (has_bottom ? 2u : 1u); ++half) {
        uint32_t color = *pixels[half];
        uint8_t luma = (uint8_t)((76u * (color >> 16) + 150u * ((color >> 8) & 255) + 29u * (color & 255)) >> 8);
        uint8_t samples[16];
        memset(samples, luma, sizeof(samples));
        ascii_temporal_cell_t *cell = &frame->stabilizer->cells[(top_row + half) * frame->width + x];
        uint64_t elapsed = frame->stabilizer->elapsed_ns;
        if (cell->valid && mode != ASCII_COLOR_NONE &&
            source_color_motion(cell->source_color, color) > ASCII_TEMPORAL_MOTION_LIMIT)
          elapsed = UINT64_MAX;
        cell->source_color = color;
        ascii_stabilizer_choose(cell, samples, 0, 0, UINT64_MAX, color, elapsed, pixels[half]);
      }
      if (!has_bottom)
        frame->index[x] = frame->packed[x];
    }
    if (mode == ASCII_COLOR_TRUE) {
      memcpy(frame->fg, frame->packed, frame->width * sizeof(uint32_t));
      memcpy(frame->bg, frame->index, frame->width * sizeof(uint32_t));
    } else if (mode != ASCII_COLOR_NONE) {
      ops->quantize(frame->packed, frame->fg, frame->width, mode == ASCII_COLOR_16 ? 16 : 256);
      ops->quantize(frame->index, frame->bg, frame->width, mode == ASCII_COLOR_16 ? 16 : 256);
    }
    ops->halfblock(frame->packed, frame->index, frame->glyph, frame->fg, frame->bg, frame->width, (unsigned)mode,
                   frame->table);
  }
}
