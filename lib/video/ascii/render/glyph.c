#include <ascii-chat/otel.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render/render.h>
#include <ascii-chat/video/ascii/render/common.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/util/overflow.h>
#include <string.h>
#include <limits.h>

#include "glyph.h"

/* Compare a small spatial image patch against glyph coverage masks.
 * The source is fitted at 8x8 samples per output cell, then averaged to 4x4.
 * ASCII coverage comes from DejaVu Sans Mono; real terminal fonts may differ. */
#include "../glyph_masks.h"

#define GLYPH_ASCII_COUNT 95
#define GLYPH_BLOCK_COUNT 20
#define GLYPH_COUNT (GLYPH_ASCII_COUNT + GLYPH_BLOCK_COUNT)

static int glyph_palette_index(const uint8_t *p, size_t *length) {
  if (*p >= 32 && *p <= 126) {
    *length = 1;
    return (int)*p - 32;
  }
  if (*p == 0xe2 && p[1] == 0x96 && p[2] >= 0x80 && p[2] <= 0x93) {
    *length = 3;
    return GLYPH_ASCII_COUNT + (int)(p[2] - 0x80);
  }
  return -1;
}

static uint8_t glyph_block_pixel(int block, int x, int y) {
  if (block >= 17)
    return (uint8_t)((block - 16) * 255 / 4);
  bool set = block == 0 ? y < 4 : block <= 7 ? y >= 8 - block : block == 8 ? true : block <= 15 ? x < 16 - block : x >= 4;
  return set ? 255 : 0;
}

// Block elements have font-independent geometry. Coverage is the area of
// each quarter-cell covered by the block, including eighth-cell boundaries.
static void glyph_block_mask(int block, uint8_t mask[16]) {
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      int coverage = 0;
      for (int sy = 0; sy < 2; ++sy) {
        for (int sx = 0; sx < 2; ++sx) {
          int px = 2 * x + sx, py = 2 * y + sy;
          coverage += glyph_block_pixel(block, px, py) == 255 ? 1 : 0;
        }
      }
      mask[y * 4 + x] = block >= 17 ? (uint8_t)((block - 16) * 255 / 4) : (uint8_t)(coverage * 255 / 4);
    }
  }
}

static uint32_t glyph_key(int index) {
  if (index < GLYPH_ASCII_COUNT)
    return (uint32_t)(index + 32);
  uint32_t cp = 0x2580u + (uint32_t)(index - GLYPH_ASCII_COUNT);
  const uint8_t bytes[4] = {(uint8_t)(0xe0u | (cp >> 12)), (uint8_t)(0x80u | ((cp >> 6) & 63)), (uint8_t)(0x80u | (cp & 63)), 0};
  uint32_t key = 0;
  memcpy(&key, bytes, sizeof(key));
  return key;
}

bool ascii_glyph_palette_supported_impl(const char *palette) {
  if (!palette || !*palette)
    return false;
  for (const uint8_t *p = (const uint8_t *)palette; *p;) {
    size_t length;
    if (glyph_palette_index(p, &length) < 0)
      return false;
    p += length;
  }
  return true;
}

typedef struct {
  int16_t mask[16];
  int16_t centered[16];
  int16_t mask_row_sum[4];
  uint8_t fine[64];
  int16_t fine_row_sum[8];
  int sum, tone, energy, fine_mean, fine_energy;
  uint32_t key;
} glyph_candidate_t;

// Cache only the sufficient statistics used by glyph scoring. The bounded,
// frame-local table never retains source images or temporal history.
#define GLYPH_MATCH_CACHE_SIZE 256
typedef struct {
  uint64_t hash;
  uint32_t previous;
  int chroma_energy;
  uint8_t patch[16];
  int16_t chroma[2][16], fine[64];
  size_t best, previous_index;
  uint64_t best_error, previous_error;
} glyph_match_cache_t;

static bool glyph_match_cached(const glyph_match_cache_t *match, uint32_t previous, const uint8_t patch[16], const int16_t chroma[2][16], int chroma_energy, const int16_t fine[64], bool colored) {
  if (match->previous != previous || match->chroma_energy != chroma_energy || memcmp(match->patch, patch, sizeof(match->patch)))
    return false;
  if (chroma_energy && memcmp(match->chroma, chroma, sizeof(match->chroma)))
    return false;
  return !colored || memcmp(match->fine, fine, sizeof(match->fine)) == 0;
}

// Preserve hue while keeping visible ink against the dark canvas. Color is
// weighted by glyph coverage, so a small feature is not colored by its corner.
static uint32_t glyph_foreground(const glyph_candidate_t *glyph, const int16_t samples[3][16], const int sums[3], bool uniform_rows) {
  unsigned channels[3], peak = 0;
  int stride = uniform_rows ? 4 : 1, count = 16 / stride;
  // With constant color, both coverage-weighted estimates equal that color.
  // Upscaled source pixels commonly cover an entire terminal cell.
  bool uniform = true;
  for (int i = stride; uniform && i < 16; i += stride)
    uniform = samples[0][i] == samples[0][0] && samples[1][i] == samples[1][0] && samples[2][i] == samples[2][0];
  if (uniform) {
    for (int c = 0; c < 3; ++c) {
      channels[c] = (unsigned)samples[c][0];
      if (channels[c] > peak)
        peak = channels[c];
    }
  } else {
    int order[16];
    int luminance[16];
    for (int i = 0; i < count; ++i) {
      int index = i * stride;
      luminance[index] = (77 * samples[0][index] + 150 * samples[1][index] + 29 * samples[2][index]) >> 8;
      int j = i;
      while (j > 0 && luminance[order[j - 1]] > luminance[index]) {
        order[j] = order[j - 1];
        --j;
      }
      order[j] = index;
    }
    int median = order[count / 2];
    int weighted_luma = 0;
    for (int i = 0; i < count; ++i)
      weighted_luma += luminance[i * stride] * (uniform_rows ? glyph->mask_row_sum[i] : glyph->mask[i]);
    if (glyph->sum) {
      int covered = 0;
      for (int i = 0; i < count; ++i) {
        covered += uniform_rows ? glyph->mask_row_sum[order[i] / 4] : glyph->mask[order[i]];
        if (covered * 2 >= glyph->sum) {
          median = order[i];
          break;
        }
      }
    }
    for (int c = 0; c < 3; ++c) {
      int weighted = 0;
      for (int i = 0; i < count; ++i)
        weighted += samples[c][i * stride] * (uniform_rows ? glyph->mask_row_sum[i] : glyph->mask[i]);
      unsigned average = glyph->sum ? (unsigned)((weighted + glyph->sum / 2) / glyph->sum) : (unsigned)((sums[c] + 8) / 16);
      // Blend with a covered median pixel to reduce isolated outliers without
      // creating colors by independently taking a median of each channel.
      bool representative = !glyph->sum || luminance[median] * glyph->sum * 2 >= weighted_luma;
      channels[c] = representative ? (average + (unsigned)samples[c][median] + 1) / 2 : average;
      if (channels[c] > peak)
        peak = channels[c];
    }
  }
  if (peak) {
    unsigned target = 128 + peak / 2;
    for (int c = 0; c < 3; ++c)
      channels[c] = (channels[c] * target + peak / 2) / peak;
  }
  return (channels[0] << 16) | (channels[1] << 8) | channels[2];
}

static int glyph_lift_luma(int luma) {
  return luma + (luma * (255 - luma) + 256) / 512;
}

static uint32_t glyph_ordered_dither(uint32_t rgb, int x, int y, ascii_color_t mode) {
  static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  int amplitude = mode == ASCII_COLOR_16 ? 32 : 16;
  int offset = ((int)bayer[y & 3][x & 3] * 2 - 15) * amplitude / 16;
  uint32_t result = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    int channel = (int)((rgb >> shift) & 255);
    if (channel > 0 && channel < 255) {
      channel += offset;
      if (channel < 0)
        channel = 0;
      if (channel > 255)
        channel = 255;
    }
    result |= (uint32_t)channel << shift;
  }
  return result;
}

char *ascii_glyph_render(const image_t *image, ascii_color_t mode, const simd_cells_t *ops, const char *palette, bool background) {
  if (!image || !image->pixels || !ops || image->w <= 0 || image->h <= 0 || image->w % 8 || image->h % 8 || mode < ASCII_COLOR_NONE || mode > ASCII_COLOR_TRUE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid glyph matching input");
    return NULL;
  }
  bool allowed[GLYPH_COUNT] = {false};
  int candidates[GLYPH_COUNT];
  // Restrict the atlas to unique palette entries before scoring any cells.
  size_t candidate_count = 0;
  int (*shape_correlation)(const int16_t[16], const int16_t[16]) = ops->glyph_shape_correlation ? ops->glyph_shape_correlation : glyph_shape_correlation_scalar;
  if (palette) {
    if (!ascii_glyph_palette_supported_impl(palette)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Glyph palette contains an unsupported character");
      return NULL;
    }
    for (const uint8_t *p = (const uint8_t *)palette; *p;) {
      size_t length;
      int index = glyph_palette_index(p, &length);
      if (!allowed[index]) {
        allowed[index] = true;
        candidates[candidate_count++] = index;
      }
      p += length;
    }
  } else {
    for (int i = 0; i < GLYPH_ASCII_COUNT; ++i) {
      allowed[i] = true;
      candidates[candidate_count++] = i;
    }
  }
  // An explicitly selected palette may be syntactically valid but empty after
  // runtime option changes.  Do not enter the scoring loop with no candidates:
  // the fallback below dereferences glyphs[best].
  if (candidate_count == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Glyph renderer has no candidate glyphs");
    return NULL;
  }
  // Prepare coverage and its sufficient statistics once per frame.
  glyph_candidate_t glyphs[GLYPH_COUNT];
  // Precompute each mask's brightness and centered shape for correlation.
  for (size_t c = 0; c < candidate_count; ++c) {
    int g = candidates[c];
    glyph_candidate_t *glyph = &glyphs[c];
    glyph->key = glyph_key(g);
    uint8_t block_mask[16];
    const uint8_t *mask = g < GLYPH_ASCII_COUNT ? glyph_masks[g] : block_mask;
    if (g >= GLYPH_ASCII_COUNT)
      glyph_block_mask(g - GLYPH_ASCII_COUNT, block_mask);
    glyph->fine_mean = 0;
    for (int i = 0; i < 64; ++i) {
      glyph->fine[i] = g < GLYPH_ASCII_COUNT ? glyph_masks_fine[g][i] : glyph_block_pixel(g - GLYPH_ASCII_COUNT, i % 8, i / 8);
      glyph->fine_mean += glyph->fine[i];
    }
    glyph->fine_mean /= 64;
    glyph->fine_energy = 0;
    for (int y = 0; y < 8; ++y) {
      glyph->fine_row_sum[y] = 0;
      for (int x = 0; x < 8; ++x) {
        int centered = (int)glyph->fine[y * 8 + x] - glyph->fine_mean;
        glyph->fine_row_sum[y] += centered;
        glyph->fine_energy += centered * centered;
      }
    }
    glyph->sum = glyph->energy = 0;
    for (int i = 0; i < 16; ++i) {
      glyph->mask[i] = mask[i];
      glyph->sum += mask[i];
    }
    for (int row = 0; row < 4; ++row) {
      glyph->mask_row_sum[row] = 0;
      for (int x = 0; x < 4; ++x)
        glyph->mask_row_sum[row] += glyph->mask[row * 4 + x];
    }
    int mean = glyph->sum / 16;
    glyph->tone = glyph->sum * 255 / (g < GLYPH_ASCII_COUNT ? GLYPH_MASK_MAX_SUM : 16 * 255);
    for (int i = 0; i < 16; ++i) {
      glyph->centered[i] = (int16_t)((int)mask[i] - mean);
      glyph->energy += glyph->centered[i] * glyph->centered[i];
    }
  }
  // Visit nearby tones first so the existing brightness lower bound can
  // reject distant candidates sooner. Canonical indices still break ties.
  size_t tone_order[GLYPH_COUNT];
  for (size_t c = 0; c < candidate_count; ++c) {
    size_t position = c;
    while (position && glyphs[tone_order[position - 1]].tone > glyphs[c].tone) {
      tone_order[position] = tone_order[position - 1];
      --position;
    }
    tone_order[position] = c;
  }
  size_t tone_start[256];
  size_t position = 0;
  for (int tone = 0; tone < 256; ++tone) {
    while (position < candidate_count && glyphs[tone_order[position]].tone < tone)
      ++position;
    tone_start[tone] = position;
  }
  image_t cells = *image;
  cells.w /= 8;
  cells.h /= 8;
  ascii_frame_t frame;
  if (ascii_frame_init(&frame, &cells, " .") != ASCIICHAT_OK)
    return NULL;
  frame.temporal_preselected = true;
  // Cells often have the same fit at different RGB hues. Reuse only the fit,
  // within this frame, and only for the same scoring inputs and prior glyph.
  // Color and temporal history are still resolved independently for each cell.
  glyph_match_cache_t matches[GLYPH_MATCH_CACHE_SIZE] = {0};
  uint8_t recent_way[GLYPH_MATCH_CACHE_SIZE / 2] = {0};
  for (int y = 0; y < cells.h; ++y) {
    for (int x = 0; x < cells.w; ++x) {
      const rgb_pixel_t *pixels = image->pixels + (size_t)y * 8 * (size_t)image->w + (size_t)x * 8;
      rgb_pixel_t row_colors[8];
      bool uniform_rows = true;
      for (int sy = 0; uniform_rows && sy < 8; ++sy) {
        const rgb_pixel_t *row = pixels + (size_t)sy * image->w;
        row_colors[sy] = row[0];
        for (int sx = 1; uniform_rows && sx < 8; ++sx)
          uniform_rows = memcmp(row, row + sx, sizeof(*row)) == 0;
      }
      int16_t samples[3][16] = {{0}};
      int sums[3] = {0};
      uint8_t patch[16];
      int sample_mean = 0;
      uint64_t patch_hash = UINT64_C(0xcbf29ce484222325);
      for (int sy = 0; sy < 4; ++sy) {
        const rgb_pixel_t *top = image->pixels + (size_t)(y * 8 + sy * 2) * (size_t)image->w + (size_t)(x * 8);
        const rgb_pixel_t *bottom = top + image->w;
        int red = 0, green = 0, blue = 0;
        if (uniform_rows) {
          red = (row_colors[sy * 2].r + row_colors[sy * 2 + 1].r) / 2;
          green = (row_colors[sy * 2].g + row_colors[sy * 2 + 1].g) / 2;
          blue = (row_colors[sy * 2].b + row_colors[sy * 2 + 1].b) / 2;
        }
        for (int sx = 0; sx < 4; ++sx) {
          if (!uniform_rows) {
            const rgb_pixel_t a = top[sx * 2], b = top[sx * 2 + 1];
            const rgb_pixel_t c = bottom[sx * 2], d = bottom[sx * 2 + 1];
            red = (a.r + b.r + c.r + d.r) / 4;
            green = (a.g + b.g + c.g + d.g) / 4;
            blue = (a.b + b.b + c.b + d.b) / 4;
          }
          int i = sy * 4 + sx;
          patch[i] = (uint8_t)glyph_lift_luma((77 * red + 150 * green + 29 * blue) >> 8);
          patch_hash = (patch_hash ^ patch[i]) * UINT64_C(0x100000001b3);
          samples[0][i] = (int16_t)red;
          samples[1][i] = (int16_t)green;
          samples[2][i] = (int16_t)blue;
          sample_mean += patch[i];
          for (int channel = 0; channel < 3; ++channel)
            sums[channel] += samples[channel][i];
        }
      }
      // Compare local shape independently of the patch's average brightness.
      sample_mean /= 16;
      int16_t centered_samples[16];
      int sample_energy = 0;
      for (int i = 0; i < 16; ++i) {
        int centered = (int)patch[i] - sample_mean;
        centered_samples[i] = (int16_t)centered;
        sample_energy += centered * centered;
      }
      // Compare chromatic structure as well as luminance: red and green can
      // have the same brightness while forming a visible edge.
      int16_t chroma[2][16];
      int chroma_energy = 0;
      for (int i = 0; i < 16; ++i) {
        chroma[0][i] = (int16_t)((samples[0][i] - samples[1][i]) - (sums[0] - sums[1]) / 16);
        chroma[1][i] = (int16_t)((samples[2][i] - samples[1][i]) - (sums[2] - sums[1]) / 16);
        chroma_energy += chroma[0][i] * chroma[0][i] + chroma[1][i] * chroma[1][i];
      }
      size_t best = 0, previous_index = 0;
      uint64_t best_error = UINT64_MAX;
      uint64_t previous_error = UINT64_MAX;
      ascii_temporal_cell_t *previous = frame.stabilizer ? &frame.stabilizer->cells[(size_t)y * frame.width + x] : NULL;
      uint32_t previous_key = previous && previous->valid ? previous->glyph : 0;
      int16_t fine_samples[64];
      if (mode != ASCII_COLOR_NONE) {
        for (int sy = 0; sy < 8; ++sy) {
          const rgb_pixel_t *row = pixels + (size_t)sy * image->w;
          int luma = uniform_rows ? glyph_lift_luma((77 * row_colors[sy].r + 150 * row_colors[sy].g + 29 * row_colors[sy].b) >> 8) : 0;
          for (int sx = 0; sx < 8; ++sx) {
            if (!uniform_rows) {
              rgb_pixel_t p = row[sx];
              luma = glyph_lift_luma((77 * p.r + 150 * p.g + 29 * p.b) >> 8);
            }
            fine_samples[sy * 8 + sx] = (int16_t)(luma - sample_mean);
          }
        }
      }
      // Residuals below eight contribute zero for every candidate. Otherwise
      // compare the complete chroma vectors, including their integer centering.
      int contributing_chroma = mode != ASCII_COLOR_NONE && chroma_energy >= 8 ? chroma_energy : 0;
      patch_hash ^= previous_key ^ ((uint64_t)contributing_chroma << 32);
      // Zero marks an empty entry; full comparisons still guard collisions.
      uint64_t cache_hash = patch_hash ? patch_hash : 1;
      size_t set = ((patch_hash ^ (patch_hash >> 32)) & (GLYPH_MATCH_CACHE_SIZE - 1)) / 2;
      unsigned way = recent_way[set];
      glyph_match_cache_t *match = &matches[set * 2 + way];
      bool repeated = match->hash == cache_hash && glyph_match_cached(match, previous_key, patch, chroma, contributing_chroma, fine_samples, mode != ASCII_COLOR_NONE);
      if (!repeated) {
        way ^= 1;
        match = &matches[set * 2 + way];
        repeated = match->hash == cache_hash && glyph_match_cached(match, previous_key, patch, chroma, contributing_chroma, fine_samples, mode != ASCII_COLOR_NONE);
      }
      recent_way[set] = (uint8_t)way;
      if (repeated) {
        best = match->best;
        previous_index = match->previous_index;
        best_error = match->best_error;
        previous_error = match->previous_error;
      } else {
        enum { GLYPH_SHORTLIST = 8 };
        size_t shortlist[GLYPH_SHORTLIST];
        uint64_t shortlist_error[GLYPH_SHORTLIST];
        size_t shortlist_count = 0;
        uint64_t prior_coarse_error = UINT64_MAX;
        size_t lower = tone_start[sample_mean], upper = lower;
        while (lower || upper < candidate_count) {
          size_t c;
          if (!lower || (upper < candidate_count && glyphs[tone_order[upper]].tone - sample_mean < sample_mean - glyphs[tone_order[lower - 1]].tone))
            c = tone_order[upper++];
          else
            c = tone_order[--lower];
          bool is_previous = previous && previous->valid && previous->glyph == glyphs[c].key;
          int tone_delta = sample_mean - glyphs[c].tone;
          int tone_error = 16 * tone_delta * tone_delta;
          // Shape and color residuals cannot lower the brightness error.
          if (!is_previous && shortlist_count == GLYPH_SHORTLIST && (uint64_t)tone_error > shortlist_error[shortlist_count - 1]) {
            // A previous temporal choice must still receive its full score.
            if (previous && previous->valid && prior_coarse_error == UINT64_MAX)
              continue;
            break;
          }
          int dot = sample_energy ? shape_correlation(centered_samples, glyphs[c].centered) : 0;
          uint64_t error = (uint64_t)(tone_error + sample_energy + glyphs[c].energy - 2 * dot);
          // Chroma residuals are nonnegative too. Avoid their correlations and
          // division once the luminance fit alone cannot enter the shortlist.
          if (!is_previous && shortlist_count == GLYPH_SHORTLIST && error > shortlist_error[shortlist_count - 1])
            continue;
          // A chroma residual below eight contributes zero after division.
          if (mode != ASCII_COLOR_NONE && chroma_energy >= 8 && glyphs[c].energy) {
            int64_t red_dot = shape_correlation(chroma[0], glyphs[c].centered);
            int64_t blue_dot = shape_correlation(chroma[1], glyphs[c].centered);
            int64_t explained = (red_dot * red_dot + blue_dot * blue_dot) / glyphs[c].energy;
            int64_t residual = chroma_energy - explained;
            if (residual > 0)
              error += (uint64_t)residual / 8;
          } else if (mode != ASCII_COLOR_NONE) {
            error += (uint64_t)chroma_energy / 8;
          }
          if (is_previous) {
            prior_coarse_error = error;
            previous_index = c;
          }
          size_t shortlist_position = 0;
          while (shortlist_position < shortlist_count && (shortlist_error[shortlist_position] < error || (shortlist_error[shortlist_position] == error && shortlist[shortlist_position] < c)))
            ++shortlist_position;
          if (shortlist_position < GLYPH_SHORTLIST) {
            size_t end = shortlist_count < GLYPH_SHORTLIST ? shortlist_count++ : GLYPH_SHORTLIST - 1;
            for (size_t j = end; j > shortlist_position; --j) {
              shortlist[j] = shortlist[j - 1];
              shortlist_error[j] = shortlist_error[j - 1];
            }
            shortlist[shortlist_position] = c;
            shortlist_error[shortlist_position] = error;
          }
        }
        // The second pass resolves near-ties with the original 8x8 samples.
        // Keep monochrome's established glyph scoring and its reference behavior.
        bool previous_scored = false;
        for (size_t n = 0; n < shortlist_count + (prior_coarse_error != UINT64_MAX ? 1u : 0u); ++n) {
          bool prior = n == shortlist_count;
          size_t index = prior ? previous_index : shortlist[n];
          if (prior && previous_scored)
            continue;
          uint64_t error = prior ? prior_coarse_error : shortlist_error[n];
          bool is_previous = previous && previous->valid && previous->glyph == glyphs[index].key;
          if (!is_previous && error >= best_error)
            continue;
          if (mode != ASCII_COLOR_NONE) {
            // Both centered values are in [-255, 255], so the complete squared
            // residual is at most 64 * 510 * 510 and fits in a signed int.
            int fine_error = 0;
            if (uniform_rows) {
              // Each source value occurs eight times. Expand the squared
              // residual using the mask's precomputed row sums and energy.
              fine_error = glyphs[index].fine_energy;
              for (int sy = 0; sy < 8; ++sy) {
                int value = fine_samples[sy * 8];
                fine_error += 8 * value * value - 2 * value * glyphs[index].fine_row_sum[sy];
              }
            } else {
              for (int i = 0; i < 64; ++i) {
                int delta = fine_samples[i] - ((int)glyphs[index].fine[i] - glyphs[index].fine_mean);
                fine_error += delta * delta;
              }
            }
            error += (uint64_t)fine_error / 16;
          }
          if (is_previous) {
            previous_error = error;
            previous_scored = true;
          }
          if (!prior && error < best_error) {
            best_error = error;
            best = index;
          }
        }
        match->hash = cache_hash;
        match->previous = previous_key;
        match->chroma_energy = contributing_chroma;
        match->best = best;
        match->previous_index = previous_index;
        match->best_error = best_error;
        match->previous_error = previous_error;
        memcpy(match->patch, patch, sizeof(patch));
        if (contributing_chroma)
          memcpy(match->chroma, chroma, sizeof(chroma));
        if (mode != ASCII_COLOR_NONE)
          memcpy(match->fine, fine_samples, sizeof(fine_samples));
      }
      uint32_t rgb = glyph_foreground(&glyphs[best], samples, sums, uniform_rows);
      uint32_t bg_rgb = ((uint32_t)(sums[0] / 16) << 16) | ((uint32_t)(sums[1] / 16) << 8) | (uint32_t)(sums[2] / 16);
      uint32_t selected = glyphs[best].key;
      if (previous) {
        uint32_t previous_color = rgb;
        uint64_t margin = best_error / 8 + 128;
        if (previous->glyph != selected && previous_error != UINT64_MAX && (previous_error <= best_error || previous_error - best_error <= margin))
          previous_color = glyph_foreground(&glyphs[previous_index], samples, sums, uniform_rows);
        selected = ascii_stabilizer_glyph(previous, patch, samples, mode != ASCII_COLOR_NONE, selected, best_error, previous_error, previous_color, frame.stabilizer->elapsed_ns, &rgb, &bg_rgb);
      }
      frame.glyph[x] = selected;
      frame.packed[x] = rgb;
      frame.index[x] = bg_rgb;
      uint32_t color = background ? bg_rgb : rgb;
      frame.luma[x] = (uint8_t)((77 * ((color >> 16) & 255) + 150 * ((color >> 8) & 255) + 29 * (color & 255)) >> 8);
    }
    // Anchor the ordered pattern to terminal cells so stationary video does
    // not shimmer; truecolor and monochrome output remain unchanged.
    if (mode == ASCII_COLOR_16 || mode == ASCII_COLOR_256) {
      for (int x = 0; x < cells.w; ++x) {
        frame.packed[x] = glyph_ordered_dither(frame.packed[x], x, y, mode);
        if (background)
          frame.index[x] = glyph_ordered_dither(frame.index[x], x, y, mode);
      }
    }
    if (background && mode != ASCII_COLOR_NONE) {
      if (mode == ASCII_COLOR_TRUE) {
        memcpy(frame.bg, frame.index, frame.width * sizeof(uint32_t));
      } else {
        ops->quantize(frame.index, frame.bg, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
      }
      for (size_t x = 0; x < frame.width; ++x)
        frame.fg[x] = ascii_background_contrast(mode, frame.bg[x], frame.luma[x]);
      ascii_emit_prepared_row(&frame.output, mode, ops, frame.glyph, frame.fg, frame.bg, frame.width);
      emit_row_end(&frame.output, true, y + 1 < cells.h);
      ++frame.row;
    } else {
      if (mode == ASCII_COLOR_16 || mode == ASCII_COLOR_256)
        ops->quantize(frame.packed, frame.index, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
      ascii_frame_emit_row(&frame, mode, false, y + 1 < cells.h, ops);
    }
  }
  return ascii_frame_finish(&frame);
}
