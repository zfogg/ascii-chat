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
 * Parse a shell-style argument string into argv[]
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

  *out_args_copy = NULL;
  char *args_copy = strdup(args_str);
  if (!args_copy) {
    return -1;
  }

  *out_args_copy = args_copy;

  // Compact each parsed argument in place so quoted values can contain spaces.
  int argc = 0;
  char *read = args_copy;
  char *write = args_copy;
  while (*read != '\0') {
    while (*read == ' ' || *read == '\t' || *read == '\n' || *read == '\r')
      read++;
    if (*read == '\0')
      break;
    if (argc >= max_args - 1) {
      free(args_copy);
      *out_args_copy = NULL;
      return -1;
    }

    argv[argc++] = write;
    char quote = '\0';
    while (*read != '\0') {
      char current = *read++;
      if (current == '\\' && *read != '\0') {
        *write++ = *read++;
        continue;
      }
      if (quote != '\0') {
        if (current == quote)
          quote = '\0';
        else
          *write++ = current;
        continue;
      }
      if (current == '\'' || current == '"') {
        quote = current;
      } else if (current == ' ' || current == '\t' || current == '\n' || current == '\r') {
        break;
      } else {
        *write++ = current;
      }
    }
    if (quote != '\0') {
      free(args_copy);
      *out_args_copy = NULL;
      return -1;
    }
    *write++ = '\0';
  }
  argv[argc] = NULL;

  return argc;
}
