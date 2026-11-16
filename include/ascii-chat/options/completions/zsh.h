/**
 * @file zsh.h
 * @brief Zsh shell completion generator
 * @ingroup options
 */

#pragma once

#include <stdio.h>
#include "../../common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate zsh completion script
 */
asciichat_error_t completions_generate_zsh(FILE *output);

#ifdef __cplusplus
}
#endif
