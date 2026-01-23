/**
 * @file bash.h
 * @brief Bash shell completion generator
 * @ingroup options
 *
 * Generates bash completion scripts from the options registry.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdio.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate bash completion script
 *
 * Generates a complete bash completion script with option descriptions,
 * mode detection, and value suggestions for enum options.
 *
 * @param output FILE stream to write completion script to
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t completions_generate_bash(FILE *output);

#ifdef __cplusplus
}
#endif
