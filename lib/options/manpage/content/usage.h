/**
 * @file usage.h
 * @brief USAGE section generator for man pages
 * @ingroup options_manpage
 *
 * Generates the USAGE section from usage descriptors showing
 * different ways to invoke the program.
 */

#pragma once

#include "../../builder.h"
#include <stdio.h>
#include <stddef.h>

/**
 * @brief Generate USAGE section content
 *
 * Creates USAGE section from usage descriptors with proper
 * groff tagged paragraph formatting.
 *
 * @param[in] config Options configuration with usage descriptors
 * @return Newly allocated content string (caller must free)
 *
 * @ingroup options_manpage
 */
char *manpage_content_generate_usage(const options_config_t *config);

/**
 * @brief Free generated usage content
 *
 * @param[in] content Content to free (can be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_content_free_usage(char *content);
