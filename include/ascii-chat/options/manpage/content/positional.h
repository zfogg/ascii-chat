/**
 * @file positional.h
 * @brief POSITIONAL ARGUMENTS section generator for man pages
 * @ingroup options_manpage
 *
 * Generates the POSITIONAL ARGUMENTS section from positional argument
 * descriptors showing what arguments a program accepts.
 */

#pragma once

#include <ascii-chat/options/builder.h>
#include <stdio.h>
#include <stddef.h>

/**
 * @brief Generate POSITIONAL ARGUMENTS section content
 *
 * Creates POSITIONAL ARGUMENTS section from positional argument
 * descriptors with proper groff formatting and examples.
 *
 * @param[in] config Options configuration with positional descriptors
 * @return Newly allocated content string (caller must free)
 *
 * @ingroup options_manpage
 */
char *manpage_content_generate_positional(const options_config_t *config);

/**
 * @brief Free generated positional content
 *
 * @param[in] content Content to free (can be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_content_free_positional(char *content);
