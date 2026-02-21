/**
 * @file init.c
 * @brief WASM initialization helpers implementation
 * @ingroup web_common
 *
 * Shared initialization utilities compiled into both WASM modules.
 */

#include "init.h"
#include <stdlib.h>
#include <string.h>

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
int wasm_parse_args(const char *args_str, char **argv, int max_args, char **out_args_copy) {
  if (!args_str || !argv || !out_args_copy || max_args < 1) {
    return -1;
  }

  // Duplicate the input string (strtok modifies the string)
  char *args_copy = strdup(args_str);
  if (!args_copy) {
    return -1;
  }

  *out_args_copy = args_copy;

  // Parse space-separated arguments
  int argc = 0;
  char *token = strtok(args_copy, " ");
  while (token != NULL && argc < (max_args - 1)) {
    argv[argc++] = token;
    token = strtok(NULL, " ");
  }
  argv[argc] = NULL;

  return argc;
}
