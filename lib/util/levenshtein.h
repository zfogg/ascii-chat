/**
 * @file levenshtein.h
 * @brief Levenshtein distance algorithm for fuzzy string matching
 *
 * MIT licensed.
 * Copyright (c) 2015 Titus Wormer <tituswormer@gmail.com>
 * From: https://github.com/wooorm/levenshtein.c
 */

#ifndef LEVENSHTEIN_H
#define LEVENSHTEIN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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
size_t levenshtein(const char *a, const char *b);

/**
 * @brief Calculate Levenshtein distance with explicit string lengths
 *
 * @param a First string
 * @param length Length of first string
 * @param b Second string
 * @param bLength Length of second string
 * @return Edit distance, or SIZE_MAX on error
 */
size_t levenshtein_n(const char *a, const size_t length, const char *b, const size_t bLength);

#ifdef __cplusplus
}
#endif

#endif // LEVENSHTEIN_H
