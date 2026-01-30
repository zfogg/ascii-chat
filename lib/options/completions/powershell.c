/**
 * @file powershell.c
 * @brief PowerShell completion script generator
 * @ingroup options
 */

#include <string.h>
#include <stdio.h>
#include "options/completions/powershell.h"
#include "options/registry.h"
#include "common.h"

/**
 * Escape help text for PowerShell
 * PowerShell uses single quotes, so single quotes need to be escaped by doubling them
 */
static void ps_escape_help(FILE *output, const char *text) {
  if (!text) {
    return;
  }

  for (const char *p = text; *p; p++) {
    if (*p == '\'') {
      // Escape single quotes by doubling them
      fprintf(output, "''");
    } else if (*p == '\n' || *p == '\t') {
      // Convert newlines and tabs to spaces
      fprintf(output, " ");
    } else {
      fputc(*p, output);
    }
  }
}

static void ps_write_option(FILE *output, const option_descriptor_t *opt) {
  if (!opt) {
    return;
  }

  // Get completion metadata for this option
  const option_metadata_t *meta = options_registry_get_metadata(opt->long_name);

  // Build values array if metadata exists
  if (meta) {
    if (meta->input_type == OPTION_INPUT_ENUM && meta->enum_values && meta->enum_count > 0) {
      // Enum values
      if (opt->short_name != '\0') {
        fprintf(output, "    @{ Name = '-%c'; Description = '", opt->short_name);
        ps_escape_help(output, opt->help_text);
        fprintf(output, "'; Values = @(");
        for (size_t i = 0; i < meta->enum_count; i++) {
          if (i > 0)
            fprintf(output, ", ");
          fprintf(output, "'%s'", meta->enum_values[i]);
        }
        fprintf(output, ") }\n");
      }
      fprintf(output, "    @{ Name = '--%s'; Description = '", opt->long_name);
      ps_escape_help(output, opt->help_text);
      fprintf(output, "'; Values = @(");
      for (size_t i = 0; i < meta->enum_count; i++) {
        if (i > 0)
          fprintf(output, ", ");
        fprintf(output, "'%s'", meta->enum_values[i]);
      }
      fprintf(output, ") }\n");
      return;
    } else if (meta->examples && meta->examples[0] != NULL) {
      // Example values (practical values, higher priority than calculated ranges)
      if (opt->short_name != '\0') {
        fprintf(output, "    @{ Name = '-%c'; Description = '", opt->short_name);
        ps_escape_help(output, opt->help_text);
        fprintf(output, "'; Values = @(");
        for (size_t i = 0; meta->examples[i] != NULL; i++) {
          if (i > 0)
            fprintf(output, ", ");
          fprintf(output, "'%s'", meta->examples[i]);
        }
        fprintf(output, ") }\n");
      }
      fprintf(output, "    @{ Name = '--%s'; Description = '", opt->long_name);
      ps_escape_help(output, opt->help_text);
      fprintf(output, "'; Values = @(");
      for (size_t i = 0; meta->examples[i] != NULL; i++) {
        if (i > 0)
          fprintf(output, ", ");
        fprintf(output, "'%s'", meta->examples[i]);
      }
      fprintf(output, ") }\n");
      return;
    } else if (meta->input_type == OPTION_INPUT_NUMERIC) {
      // Numeric range - suggest min, middle, max values
      if (opt->short_name != '\0') {
        fprintf(output, "    @{ Name = '-%c'; Description = '", opt->short_name);
        ps_escape_help(output, opt->help_text);
        fprintf(output, " (numeric %d-%d)'; Values = @(", meta->numeric_range.min, meta->numeric_range.max);
        fprintf(output, "'%d'", meta->numeric_range.min);
        if (meta->numeric_range.max > meta->numeric_range.min) {
          int middle = (meta->numeric_range.min + meta->numeric_range.max) / 2;
          fprintf(output, ", '%d'", middle);
          fprintf(output, ", '%d'", meta->numeric_range.max);
        }
        fprintf(output, ") }\n");
      }
      fprintf(output, "    @{ Name = '--%s'; Description = '", opt->long_name);
      ps_escape_help(output, opt->help_text);
      fprintf(output, " (numeric %d-%d)'; Values = @(", meta->numeric_range.min, meta->numeric_range.max);
      fprintf(output, "'%d'", meta->numeric_range.min);
      if (meta->numeric_range.max > meta->numeric_range.min) {
        int middle = (meta->numeric_range.min + meta->numeric_range.max) / 2;
        fprintf(output, ", '%d'", middle);
        fprintf(output, ", '%d'", meta->numeric_range.max);
      }
      fprintf(output, ") }\n");
      return;
    }
  }

  // Basic option without values
  if (opt->short_name != '\0') {
    fprintf(output, "    @{ Name = '-%c'; Description = '", opt->short_name);
    ps_escape_help(output, opt->help_text);
    fprintf(output, "' }\n");
  }
  fprintf(output, "    @{ Name = '--%s'; Description = '", opt->long_name);
  ps_escape_help(output, opt->help_text);
  fprintf(output, "' }\n");
}

asciichat_error_t completions_generate_powershell(FILE *output) {
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  fprintf(output, "# PowerShell completion script for ascii-chat\n"
                  "# Generated from options registry - DO NOT EDIT MANUALLY\n"
                  "# Usage: ascii-chat --completions powershell | Out-String | Invoke-Expression\n"
                  "\n"
                  "$script:AsciiChatCompleter = {\n"
                  "  param($wordToComplete, $commandAst, $cursorPosition)\n"
                  "\n"
                  "  $words = @($commandAst.CommandElements | ForEach-Object { $_.Value })\n"
                  "  $mode = $null\n"
                  "\n"
                  "  foreach ($word in $words) {\n"
                  "    if ($word -in @('server', 'client', 'mirror')) {\n"
                  "      $mode = $word\n"
                  "      break\n"
                  "    }\n"
                  "  }\n"
                  "\n"
                  "  $binaryOptions = @(\n");

  /* Binary options - use unified display API matching help system */
  size_t binary_count = 0;
  const option_descriptor_t *binary_opts = options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  if (binary_opts) {
    for (size_t i = 0; i < binary_count; i++) {
      ps_write_option(output, &binary_opts[i]);
    }
    SAFE_FREE(binary_opts);
  }

  fprintf(output, "  )\n\n  $serverOptions = @(\n");

  /* Server options - use unified display API matching help system */
  size_t server_count = 0;
  const option_descriptor_t *server_opts = options_registry_get_for_display(MODE_SERVER, false, &server_count);

  if (server_opts) {
    for (size_t i = 0; i < server_count; i++) {
      ps_write_option(output, &server_opts[i]);
    }
    SAFE_FREE(server_opts);
  }

  fprintf(output, "  )\n\n  $clientOptions = @(\n");

  /* Client options - use unified display API matching help system */
  size_t client_count = 0;
  const option_descriptor_t *client_opts = options_registry_get_for_display(MODE_CLIENT, false, &client_count);

  if (client_opts) {
    for (size_t i = 0; i < client_count; i++) {
      ps_write_option(output, &client_opts[i]);
    }
    SAFE_FREE(client_opts);
  }

  fprintf(output, "  )\n\n  $mirrorOptions = @(\n");

  /* Mirror options - use unified display API matching help system */
  size_t mirror_count = 0;
  const option_descriptor_t *mirror_opts = options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);

  if (mirror_opts) {
    for (size_t i = 0; i < mirror_count; i++) {
      ps_write_option(output, &mirror_opts[i]);
    }
    SAFE_FREE(mirror_opts);
  }

  fprintf(output, "  )\n\n  $discoverySvcOptions = @(\n");

  /* Discovery-service options */
  size_t discovery_svc_count = 0;
  const option_descriptor_t *discovery_svc_opts =
      options_registry_get_for_display(MODE_DISCOVERY_SERVICE, false, &discovery_svc_count);

  if (discovery_svc_opts) {
    for (size_t i = 0; i < discovery_svc_count; i++) {
      ps_write_option(output, &discovery_svc_opts[i]);
    }
    SAFE_FREE(discovery_svc_opts);
  }

  fprintf(output,
          "  )\n"
          "\n"
          "  $options = $binaryOptions\n"
          "  \n"
          "  if ($mode -eq 'server') {\n"
          "    $options += $serverOptions\n"
          "  } elseif ($mode -eq 'client') {\n"
          "    $options += $clientOptions\n"
          "  } elseif ($mode -eq 'mirror') {\n"
          "    $options += $mirrorOptions\n"
          "  } elseif ($mode -eq 'discovery-service') {\n"
          "    $options += $discoverySvcOptions\n"
          "  }\n"
          "\n"
          "  if (-not $mode -and -not $wordToComplete.StartsWith('-')) {\n"
          "    @('server', 'client', 'mirror', 'discovery-service') | Where-Object { $_ -like \"$wordToComplete*\" } | "
          "ForEach-Object {\n"
          "      [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', \"Mode: $_\")\n"
          "    }\n"
          "  } else {\n"
          "    $options | Where-Object { $_.Name -like \"$wordToComplete*\" } | ForEach-Object {\n"
          "      if ($_.Values) {\n"
          "        $_.Values | ForEach-Object {\n"
          "          [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_.Description)\n"
          "        }\n"
          "      } else {\n"
          "        [System.Management.Automation.CompletionResult]::new($_.Name, $_.Name, 'ParameterValue', "
          "$_.Description)\n"
          "      }\n"
          "    }\n"
          "  }\n"
          "}\n"
          "\n"
          "Register-ArgumentCompleter -CommandName ascii-chat -ScriptBlock $script:AsciiChatCompleter\n");

  return ASCIICHAT_OK;
}
