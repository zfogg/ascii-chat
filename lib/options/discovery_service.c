/**
 * @file discovery_service.c
 * @ingroup options
 * @brief Discovery service mode option parsing
 */

#include "options/discovery_service.h"
#include "options/builder.h"
#include "options/common.h"

#include "asciichat_errno.h"
#include "common.h"
#include "options/validation.h"
#include "util/path.h"

#include <stdio.h>
#include <stdlib.h>

asciichat_error_t parse_discovery_service_options(int argc, char **argv, options_t *opts) {
  const options_config_t *config = options_preset_unified(NULL, NULL);
  if (!config) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to create options configuration");
  }
  int remaining_argc;
  char **remaining_argv;

  asciichat_error_t defaults_result = options_config_set_defaults(config, opts);
  if (defaults_result != ASCIICHAT_OK) {
    options_config_destroy(config);
    return defaults_result;
  }

  asciichat_error_t result = options_config_parse(config, argc, argv, opts, &remaining_argc, &remaining_argv);
  if (result != ASCIICHAT_OK) {
    options_config_destroy(config);
    return result;
  }

  result = validate_options_and_report(config, opts);
  if (result != ASCIICHAT_OK) {
    options_config_destroy(config);
    return result;
  }

  if (remaining_argc > 0) {
    (void)fprintf(stderr, "Error: Unexpected arguments after options:\n");
    for (int i = 0; i < remaining_argc; i++) {
      (void)fprintf(stderr, "  %s\n", remaining_argv[i]);
    }
    options_config_destroy(config);
    return option_error_invalid();
  }

  // Set default paths if not specified
  if (opts->discovery_database_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      options_config_destroy(config);
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory for database path");
    }
    snprintf(opts->discovery_database_path, sizeof(opts->discovery_database_path), "%sdiscovery.db", config_dir);
    SAFE_FREE(config_dir);
  }

  if (opts->discovery_key_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      options_config_destroy(config);
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory for identity key path");
    }
    snprintf(opts->discovery_key_path, sizeof(opts->discovery_key_path), "%sdiscovery_identity", config_dir);
    SAFE_FREE(config_dir);
  }

  options_config_destroy(config);
  return ASCIICHAT_OK;
}
