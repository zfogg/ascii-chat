/**
 * @file environment.h
 * @brief ENVIRONMENT section generator for man pages
 * @ingroup options_manpage
 *
 * Generates the ENVIRONMENT section from environment variable
 * metadata in option descriptors.
 */

#pragma once

#include "../../builder.h"
#include <stdio.h>
#include <stddef.h>

/**
 * @brief Generate ENVIRONMENT VARIABLES section content
 *
 * Extracts environment variables from option descriptors and
 * generates formatted ENVIRONMENT section with sorted variable names.
 *
 * @param[in] config Options configuration with descriptors
 * @return Newly allocated content string (caller must free)
 *
 * @ingroup options_manpage
 */
char *manpage_content_generate_environment(const options_config_t *config);

/**
 * @brief Free generated environment content
 *
 * @param[in] content Content to free (can be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_content_free_environment(char *content);
