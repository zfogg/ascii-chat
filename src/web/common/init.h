/**
 * @file init.h
 * @brief WASM initialization helpers
 * @ingroup web_common
 *
 * Provides shared initialization utilities for WASM modules.
 */

#pragma once

/**
 * Parse space-separated args_str into argv[]
 * Sets *out_args_copy to the strdup'd buffer that must be freed by the caller
 * AFTER argv is no longer needed (since argv points into args_copy).
 *
 * @param args_str Space-separated argument string
 * @param argv Output array to store argument pointers (at least max_args elements)
 * @param max_args Maximum number of arguments to parse
 * @param out_args_copy Output buffer for the strdup'd args_str
 * @return argc (number of arguments parsed), or -1 on allocation failure
 */
int wasm_parse_args(const char *args_str, char **argv, int max_args, char **out_args_copy);
