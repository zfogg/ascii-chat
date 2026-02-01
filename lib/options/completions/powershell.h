/**
 * @file powershell.h
 * @brief PowerShell completion generator
 * @ingroup options
 */

#pragma once

#include <stdio.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate PowerShell completion script
 */
asciichat_error_t completions_generate_powershell(FILE *output);

#ifdef __cplusplus
}
#endif
