/**
 * @file options.h
 * @brief OPTIONS section generator for man pages
 * @ingroup options_manpage
 *
 * Generates the OPTIONS section from option descriptors in the config.
 */

#pragma once

#include "../../../options/builder.h"
#include <stdio.h>
#include <stddef.h>

/**
 * @brief Generate OPTIONS section content
 *
 * Creates formatted OPTIONS section from option descriptors,
 * grouped by category with proper groff formatting.
 *
 * @param[in] config Options configuration with descriptors
 * @return Newly allocated content string (caller must free)
 *
 * @ingroup options_manpage
 */
char *manpage_content_generate_options(const options_config_t *config);

/**
 * @brief Free generated options content
 *
 * @param[in] content Content to free (can be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_content_free_options(char *content);
