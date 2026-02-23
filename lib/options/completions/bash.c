/**
 * @file bash.c
 * @brief Bash shell completion script generator
 * @ingroup options_completions
 * @addtogroup options_completions Completions
 * @{
 *
 * **Bash Completion Generator**: Auto-generates bash completion scripts from the
 * centralized options registry, enabling tab-completion for ascii-chat options.
 *
 * **Bash Completion Strategy**:
 *
 * The bash generator creates a completion function using bash-completion v2 API:
 *
 * 1. **Completion Function**: Defines `_ascii_chat()` completion handler
 *    - Triggered when user presses TAB after typing "ascii-chat"
 *    - Uses bash-completion built-ins (`_init_completion`, `_get_comp_words_by_ref`)
 *    - Parses current command line to detect what to complete
 *
 * 2. **Option Extraction**: Reads all options from registry via
 *    - `completions_collect_all_modes_unique()` - Get all options across modes
 *    - Filter by mode bitmask to show only applicable options
 *    - Include both short (-x) and long (--long) variants
 *
 * 3. **Completion Types**:
 *    - **Flags**: Options that don't take arguments (--help, --version)
 *    - **Arguments**: Options that take values (--port 27224, --width 120)
 *    - **Positional**: Mode names (server, client, mirror, discovery-service)
 *
 * 4. **Output Format**: Bash completion function with COMPREPLY array:
 *    ```bash
 *    _ascii_chat() {
 *      local cur prev words cword
 *      _init_completion || return
 *      COMPREPLY=($(compgen -W "--help --version --width ..." -- "$cur"))
 *    }
 *    complete -o bashdefault -o default -o nospace -F _ascii_chat ascii-chat
 *    ```
 *
 * **Usage**:
 *
 * Users enable bash completions by sourcing the generated script:
 * ```bash
 * eval "$(ascii-chat --completions bash)"
 * ```
 *
 * Or save to bash_completion.d for persistent shell integration:
 * ```bash
 * ascii-chat --completions bash > ~/.local/share/bash-completion/completions/ascii-chat
 * ```
 *
 * **Special Handling**:
 *
 * - **Help Text Escaping**: Single quotes in help text are escaped as `'\''`
 *   (end quote, escaped quote, start quote) for safe bash strings
 * - **Newlines**: Converted to spaces in help text to fit single line
 * - **Short Names**: Generated for all options with short variant
 * - **Long Names**: Generated for all options with long variant
 *
 * @see completions.h for public completion API
 * @see fish.c for Fish shell completion strategy
 * @}
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <ascii-chat/options/completions/bash.h>
#include <ascii-chat/options/completions/completions.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/enums.h>
#include <ascii-chat/common.h>

/**
 * Escape shell special characters in help text
 */
static void bash_escape_help(FILE *output, const char *text) {
  if (!text) {
    return;
  }

  for (const char *p = text; *p; p++) {
    switch (*p) {
    case '\'':
      fprintf(output, "'\\''");
      break;
    case '\n':
      fprintf(output, "\\n");
      break;
    case '\t':
      fprintf(output, "\\t");
      break;
    default:
      fputc(*p, output);
    }
  }
}

/**
 * Write a single bash option (short and long names with help text)
 */
static void bash_write_option(FILE *output, const option_descriptor_t *opt) {
  if (!opt) {
    return;
  }

  if (opt->short_name != '\0') {
    fprintf(output, "    '-%c' '", opt->short_name);
    bash_escape_help(output, opt->help_text);
    fprintf(output, "'\n");
  }

  fprintf(output, "    '--%s' '", opt->long_name);
  bash_escape_help(output, opt->help_text);
  fprintf(output, "'\n");
}

/**
 * Write bash header
 */
static void bash_write_header(FILE *output) {
  fprintf(output, "# Bash completion script for ascii-chat\n"
                  "# Generated from options registry - DO NOT EDIT MANUALLY\n"
                  "# Usage: eval \"$(ascii-chat --completions bash)\"\n"
                  "\n"
                  "_ascii_chat() {\n"
                  "  local cur prev words cword\n"
                  "  _init_completion || return\n"
                  "\n");
}

/**
 * Write bash footer
 */
static void bash_write_footer(FILE *output) {
  fprintf(output, "}\n"
                  "\n"
                  "complete -F _ascii_chat ascii-chat\n");
}

/**
 * Generate all option arrays from registry
 *
 * Uses options_registry_get_for_display() to ensure completions match
 * the help system's filtering logic exactly.
 */
static asciichat_error_t bash_write_all_options(FILE *output) {
  /* Binary-level options - use MODE_DISCOVERY (the default mode) with for_binary_help=true
   * This matches how options_print_help_for_mode() filters options for '--help' display */
  size_t binary_count = 0;
  const option_descriptor_t *binary_opts = options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  fprintf(output, "  # Binary-level options (same as 'ascii-chat --help')\n  local -a binary_opts=(\n");
  if (binary_opts) {
    for (size_t i = 0; i < binary_count; i++) {
      bash_write_option(output, &binary_opts[i]);
    }
    SAFE_FREE(binary_opts);
  }
  fprintf(output, "  )\n\n");

  /* Server options - mode-specific with for_binary_help=false
   * This matches how options_print_help_for_mode() filters for 'ascii-chat server --help' */
  size_t server_count = 0;
  const option_descriptor_t *server_opts = options_registry_get_for_display(MODE_SERVER, false, &server_count);

  fprintf(output, "  # Server-mode options (same as 'ascii-chat server --help')\n  local -a server_opts=(\n");
  if (server_opts) {
    for (size_t i = 0; i < server_count; i++) {
      bash_write_option(output, &server_opts[i]);
    }
    SAFE_FREE(server_opts);
  }
  fprintf(output, "  )\n\n");

  /* Client options - mode-specific with for_binary_help=false */
  size_t client_count = 0;
  const option_descriptor_t *client_opts = options_registry_get_for_display(MODE_CLIENT, false, &client_count);

  fprintf(output, "  # Client-mode options (same as 'ascii-chat client --help')\n  local -a client_opts=(\n");
  if (client_opts) {
    for (size_t i = 0; i < client_count; i++) {
      bash_write_option(output, &client_opts[i]);
    }
    SAFE_FREE(client_opts);
  }
  fprintf(output, "  )\n\n");

  /* Mirror options - mode-specific with for_binary_help=false */
  size_t mirror_count = 0;
  const option_descriptor_t *mirror_opts = options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);

  fprintf(output, "  # Mirror-mode options (same as 'ascii-chat mirror --help')\n  local -a mirror_opts=(\n");
  if (mirror_opts) {
    for (size_t i = 0; i < mirror_count; i++) {
      bash_write_option(output, &mirror_opts[i]);
    }
    SAFE_FREE(mirror_opts);
  }
  fprintf(output, "  )\n\n");

  /* Discovery-service options */
  size_t discovery_svc_count = 0;
  const option_descriptor_t *discovery_svc_opts =
      options_registry_get_for_display(MODE_DISCOVERY_SERVICE, false, &discovery_svc_count);

  fprintf(output, "  # Discovery-service options (same as 'ascii-chat discovery-service --help')\n  local -a "
                  "discovery_svc_opts=(\n");
  if (discovery_svc_opts) {
    for (size_t i = 0; i < discovery_svc_count; i++) {
      bash_write_option(output, &discovery_svc_opts[i]);
    }
    SAFE_FREE(discovery_svc_opts);
  }
  fprintf(output, "  )\n\n");

  return ASCIICHAT_OK;
}

/**
 * Generate enum completion cases from registry
 */
static void bash_write_enum_cases(FILE *output) {
  size_t combined_count = 0;
  option_descriptor_t *combined_opts = completions_collect_all_modes_unique(&combined_count);

  // Also add binary-level options (--log-level, --log-file, etc.)
  size_t binary_count = 0;
  const option_descriptor_t *binary_opts = options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  if (binary_opts) {
    for (size_t i = 0; i < binary_count; i++) {
      // Check if already in combined_opts
      bool already_has = false;
      for (size_t j = 0; j < combined_count; j++) {
        if (strcmp(combined_opts[j].long_name, binary_opts[i].long_name) == 0) {
          already_has = true;
          break;
        }
      }
      if (!already_has) {
        combined_count++;
        option_descriptor_t *temp = (option_descriptor_t *)SAFE_REALLOC(
            combined_opts, combined_count * sizeof(option_descriptor_t), option_descriptor_t *);
        if (temp) {
          combined_opts = temp;
          combined_opts[combined_count - 1] = binary_opts[i];
        }
      }
    }
    SAFE_FREE(binary_opts);
  }

  if (!combined_opts) {
    return;
  }

  for (size_t i = 0; i < combined_count; i++) {
    const option_descriptor_t *opt = &combined_opts[i];
    const option_metadata_t *meta = &opt->metadata;

    // Skip options with no enum or examples
    bool has_examples = meta->examples && meta->examples[0] != NULL;
    if (meta->input_type != OPTION_INPUT_ENUM && !has_examples && meta->numeric_range.max == 0) {
      continue;
    }

    // Write case statement
    if (opt->short_name != '\0') {
      fprintf(output, "  -%c | ", opt->short_name);
    }
    fprintf(output, "--%-25s)\n", opt->long_name);
    fprintf(output, "    COMPREPLY=($(compgen -W \"");

    // Write enum values
    if (meta->input_type == OPTION_INPUT_ENUM && meta->enum_values && meta->enum_values[0] != NULL) {
      for (size_t j = 0; meta->enum_values[j] != NULL; j++) {
        fprintf(output, "%s", meta->enum_values[j]);
        if (meta->enum_values[j + 1] != NULL) {
          fprintf(output, " ");
        }
      }
    }
    // Check for examples first (more practical than calculated ranges)
    else if (has_examples) {
      for (size_t j = 0; meta->examples[j] != NULL; j++) {
        fprintf(output, "%s", meta->examples[j]);
        if (meta->examples[j + 1] != NULL) {
          fprintf(output, " ");
        }
      }
    }
    // Fallback to numeric range if no examples
    else if (meta->input_type == OPTION_INPUT_NUMERIC) {
      if (meta->numeric_range.min == 1 && meta->numeric_range.max == 9) {
        fprintf(output, "1 2 3 4 5 6 7 8 9");
      } else if (meta->numeric_range.max > 0) {
        fprintf(output, "%d %d %d", meta->numeric_range.min, (meta->numeric_range.min + meta->numeric_range.max) / 2,
                meta->numeric_range.max);
      }
    }

    fprintf(output, "\" -- \"$cur\"))\n");
    fprintf(output, "    return\n");
    fprintf(output, "    ;;\n");
  }

  SAFE_FREE(combined_opts);
}

/**
 * Write completion logic
 */
static void bash_write_completion_logic(FILE *output) {
  fprintf(output, "  # Modes\n"
                  "  local modes=\"server client mirror discovery-service\"\n"
                  "\n"
                  "  # Detect which mode we're in\n"
                  "  local mode=\"\"\n"
                  "  local i\n"
                  "  for ((i = 1; i < cword; i++)); do\n"
                  "    case \"${words[i]}\" in\n"
                  "    server | client | mirror | discovery-service)\n"
                  "      mode=\"${words[i]}\"\n"
                  "      break\n"
                  "      ;;\n"
                  "    esac\n"
                  "  done\n"
                  "\n"
                  "  case \"$prev\" in\n"
                  "  # Options that take file paths\n");

  // Generate file path options dynamically from registry (collect from all modes)
  size_t combined_count = 0;
  option_descriptor_t *combined_opts = completions_collect_all_modes_unique(&combined_count);

  if (combined_opts) {
    bool first = true;
    for (size_t i = 0; i < combined_count; i++) {
      const option_descriptor_t *opt = &combined_opts[i];
      const option_metadata_t *meta = options_registry_get_metadata(opt->long_name);

      if (meta && meta->input_type == OPTION_INPUT_FILEPATH) {
        if (!first) {
          fprintf(output, " | ");
        }
        first = false;

        if (opt->short_name != '\0') {
          fprintf(output, "-%c | ", opt->short_name);
        }
        fprintf(output, "--%s", opt->long_name);
      }
    }
    SAFE_FREE(combined_opts);

    if (!first) {
      fprintf(output, ")\n");
      fprintf(output, "    _filedir\n");
      fprintf(output, "    return\n");
      fprintf(output, "    ;;\n");
    }
  }

  /* Write enum cases */
  bash_write_enum_cases(output);

  fprintf(output, "  esac\n"
                  "\n"
                  "  # If current word starts with -, complete options\n"
                  "  if [[ \"$cur\" == -* ]]; then\n"
                  "    local -a opts_to_complete\n"
                  "\n"
                  "    case \"$mode\" in\n"
                  "    client)\n"
                  "      opts_to_complete=(\"${binary_opts[@]}\" \"${client_opts[@]}\")\n"
                  "      ;;\n"
                  "    server)\n"
                  "      opts_to_complete=(\"${binary_opts[@]}\" \"${server_opts[@]}\")\n"
                  "      ;;\n"
                  "    mirror)\n"
                  "      opts_to_complete=(\"${binary_opts[@]}\" \"${mirror_opts[@]}\")\n"
                  "      ;;\n"
                  "    *)\n"
                  "      opts_to_complete=(\"${binary_opts[@]}\")\n"
                  "      ;;\n"
                  "    esac\n"
                  "\n"
                  "    # Generate completions with help text\n"
                  "    local -a completions\n"
                  "    for ((i = 0; i < ${#opts_to_complete[@]}; i += 2)); do\n"
                  "      if [[ \"${opts_to_complete[i]}\" == \"$cur\"* ]]; then\n"
                  "        completions+=(\"${opts_to_complete[i]}\")\n"
                  "      fi\n"
                  "    done\n"
                  "\n"
                  "    if [[ ${#completions[@]} -gt 0 ]]; then\n"
                  "      if compopt &>/dev/null 2>&1; then\n"
                  "        compopt -o nosort 2>/dev/null || true\n"
                  "        COMPREPLY=()\n"
                  "        for opt in \"${completions[@]}\"; do\n"
                  "          for ((i = 0; i < ${#opts_to_complete[@]}; i += 2)); do\n"
                  "            if [[ \"${opts_to_complete[i]}\" == \"$opt\" ]]; then\n"
                  "              COMPREPLY+=(\"$opt	${opts_to_complete[i+1]}\")\n"
                  "              break\n"
                  "            fi\n"
                  "          done\n"
                  "        done\n"
                  "      else\n"
                  "        COMPREPLY=($(compgen -W \"${completions[*]}\" -- \"$cur\"))\n"
                  "      fi\n"
                  "    fi\n"
                  "    return\n"
                  "  fi\n"
                  "\n"
                  "  # If no mode specified yet, suggest modes\n"
                  "  if [[ -z \"$mode\" ]]; then\n"
                  "    COMPREPLY=($(compgen -W \"$modes\" -- \"$cur\"))\n"
                  "    return\n"
                  "  fi\n");
}

asciichat_error_t completions_generate_bash(FILE *output) {
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  bash_write_header(output);
  asciichat_error_t err = bash_write_all_options(output);
  if (err != ASCIICHAT_OK) {
    return err;
  }
  bash_write_completion_logic(output);
  bash_write_footer(output);

  return ASCIICHAT_OK;
}
