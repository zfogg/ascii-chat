/**
 * @file fish.h
 * @brief Fish shell completion generator
 * @ingroup options
 */

#pragma once

#include <stdio.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate fish completion script
 */
asciichat_error_t completions_generate_fish(FILE *output);

#ifdef __cplusplus
}
#endif
