/**
 * @file levenshtein.c
 * @brief Levenshtein distance algorithm for fuzzy string matching
 *
 * MIT licensed.
 * Copyright (c) 2015 Titus Wormer <tituswormer@gmail.com>
 * From: https://github.com/wooorm/levenshtein.c
 *
 * Modified for ascii-chat to use project memory macros and UTF-8 character support.
 */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "options/levenshtein.h"
#include "util/utf8.h"
#include "common.h"

// Returns a size_t, depicting the difference between `a` and `b`.
// See <https://en.wikipedia.org/wiki/Levenshtein_distance> for more information.
// Works with UTF-8 strings by comparing codepoints instead of bytes.
size_t levenshtein_n(const char *a, const size_t length, const char *b, const size_t bLength) {
  // Shortcut optimizations / degenerate cases.
  if (a == b) {
    return 0;
  }

  if (length == 0) {
    return bLength;
  }

  if (bLength == 0) {
    return length;
  }

  size_t *cache = SAFE_CALLOC(length, sizeof(size_t), size_t *);
  if (!cache) {
    return SIZE_MAX; // Allocation failed
  }

  size_t index = 0;
  size_t bIndex = 0;
  size_t distance;
  size_t bDistance;
  size_t result = 0;
  char code;

  // initialize the vector.
  while (index < length) {
    cache[index] = index + 1;
    index++;
  }

  // Loop.
  while (bIndex < bLength) {
    code = b[bIndex];
    result = distance = bIndex++;

    for (index = 0; index < length; index++) {
      bDistance = code == a[index] ? distance : distance + 1;
      distance = cache[index];

      cache[index] = result = distance > result      ? bDistance > result ? result + 1 : bDistance
                              : bDistance > distance ? distance + 1
                                                     : bDistance;
    }
  }

  SAFE_FREE(cache);

  return result;
}

size_t levenshtein(const char *a, const char *b) {
  if (!a || !b) {
    return SIZE_MAX;
  }

  // Count UTF-8 characters instead of bytes
  size_t char_count_a = utf8_char_count(a);
  size_t char_count_b = utf8_char_count(b);

  if (char_count_a == SIZE_MAX || char_count_b == SIZE_MAX) {
    return SIZE_MAX; // Invalid UTF-8 in one of the strings
  }

  // Convert strings to codepoint arrays for UTF-8-aware comparison
  if (char_count_a == 0) {
    return char_count_b;
  }
  if (char_count_b == 0) {
    return char_count_a;
  }

  uint32_t *codepoints_a = SAFE_MALLOC(char_count_a * sizeof(uint32_t), uint32_t *);
  uint32_t *codepoints_b = SAFE_MALLOC(char_count_b * sizeof(uint32_t), uint32_t *);

  if (!codepoints_a || !codepoints_b) {
    SAFE_FREE(codepoints_a);
    SAFE_FREE(codepoints_b);
    return SIZE_MAX;
  }

  size_t decoded_a = utf8_to_codepoints(a, codepoints_a, char_count_a);
  size_t decoded_b = utf8_to_codepoints(b, codepoints_b, char_count_b);

  if (decoded_a == SIZE_MAX || decoded_b == SIZE_MAX) {
    SAFE_FREE(codepoints_a);
    SAFE_FREE(codepoints_b);
    return SIZE_MAX;
  }

  // Compute Levenshtein distance on codepoint arrays
  size_t *cache = SAFE_CALLOC(char_count_a * sizeof(size_t), 1, size_t *);
  if (!cache) {
    SAFE_FREE(codepoints_a);
    SAFE_FREE(codepoints_b);
    return SIZE_MAX;
  }

  // Initialize cache
  for (size_t i = 0; i < char_count_a; i++) {
    cache[i] = i + 1;
  }

  size_t distance = 0;
  size_t result = 0;
  for (size_t b_idx = 0; b_idx < char_count_b; b_idx++) {
    uint32_t b_code = codepoints_b[b_idx];
    result = distance = b_idx;

    for (size_t a_idx = 0; a_idx < char_count_a; a_idx++) {
      uint32_t a_code = codepoints_a[a_idx];
      size_t b_distance = (a_code == b_code) ? distance : distance + 1;
      distance = cache[a_idx];

      cache[a_idx] = result = (distance > result) ? ((b_distance > result) ? result + 1 : b_distance)
                                                  : ((b_distance > distance) ? distance + 1 : b_distance);
    }
  }

  SAFE_FREE(cache);
  SAFE_FREE(codepoints_a);
  SAFE_FREE(codepoints_b);

  return result;
}

const char *levenshtein_find_similar(const char *unknown, const char *const *candidates) {
  if (!unknown || !candidates) {
    return NULL;
  }

  const char *best_match = NULL;
  size_t best_distance = SIZE_MAX;

  for (int i = 0; candidates[i] != NULL; i++) {
    size_t dist = levenshtein(unknown, candidates[i]);
    if (dist < best_distance) {
      best_distance = dist;
      best_match = candidates[i];
    }
  }

  // Only suggest if the distance is within our threshold
  if (best_distance <= LEVENSHTEIN_SUGGESTION_THRESHOLD) {
    return best_match;
  }

  return NULL;
}
