/**
 * @file examples.h
 * @brief EXAMPLES section generator for man pages
 * @ingroup options_manpage
 *
 * Generates the EXAMPLES section from example descriptors showing
 * common usage patterns.
 */

#pragma once

#include "../../../options/builder.h"
#include <stdio.h>
#include <stddef.h>

/**
 * @brief Generate EXAMPLES section content
 *
 * Creates EXAMPLES section from example descriptors with proper
 * code block and description formatting.
 *
 * @param[in] config Options configuration with example descriptors
 * @return Newly allocated content string (caller must free)
 *
 * @ingroup options_manpage
 */
char *manpage_content_generate_examples(const options_config_t *config);

/**
 * @brief Free generated examples content
 *
 * @param[in] content Content to free (can be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_content_free_examples(char *content);
