/**
 * @file util/levenshtein.h
 * @brief Levenshtein distance algorithm for fuzzy string matching
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * MIT licensed.
 * Copyright (c) 2015 Titus Wormer <tituswormer@gmail.com>
 * From: https://github.com/wooorm/levenshtein.c
 */

#ifndef LEVENSHTEIN_H
#define LEVENSHTEIN_H

#include <stddef.h>
#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum edit distance to suggest an option
 *
 * Threshold of 2 catches most typos (single char errors, transpositions)
 * without suggesting unrelated options.
 */
#define LEVENSHTEIN_SUGGESTION_THRESHOLD 2

/**
 * @brief Calculate Levenshtein distance between two strings
 *
 * The Levenshtein distance is the minimum number of single-character edits
 * (insertions, deletions, or substitutions) required to change one string
 * into the other.
 *
 * @param a First string
 * @param b Second string
 * @return Edit distance, or SIZE_MAX on error
 *
 * @see https://en.wikipedia.org/wiki/Levenshtein_distance
 */
ASCIICHAT_API size_t levenshtein(const char *a, const char *b);

/**
 * @brief Calculate Levenshtein distance with explicit string lengths
 *
 * @param a First string
 * @param length Length of first string
 * @param b Second string
 * @param bLength Length of second string
 * @return Edit distance, or SIZE_MAX on error
 */
ASCIICHAT_API size_t levenshtein_n(const char *a, const size_t length, const char *b, const size_t bLength);

/**
 * @brief Find the most similar string from a NULL-terminated array
 *
 * Searches through an array of candidate strings to find the one most similar
 * to the input string, using Levenshtein distance.
 *
 * @param unknown The string to match against
 * @param candidates NULL-terminated array of candidate strings
 * @return Best matching string, or NULL if no match within threshold
 */
ASCIICHAT_API const char *levenshtein_find_similar(const char *unknown, const char *const *candidates);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // LEVENSHTEIN_H
