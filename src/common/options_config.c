/**
 * @file config.c
 * @ingroup config
 * @brief 📋 TOML configuration file parser with schema validation and CLI override support
 */

/* Storage capacities in elements; text buffers include the terminating NUL. */
#define OPTIONS_CONFIG_QUESTION_BUFFER_SIZE 4096

#include <ascii-chat/tui/notice.h>

#include <ascii-chat/options/config.h>
#include <ascii-chat/options/enums.h>
#include <ascii-chat/common/error_codes.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/validation.h>
#include <ascii-chat/options/schema.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/path.h>
#include <util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/common.h>
#include <libplatform/terminal.h>
#include <libplatform/system.h>
#include <libplatform/question.h>
#include <sys/stat.h>
#include <libplatform/filesystem.h>
#include <libflags/builder.h>
#include <libflags/config.h>
#include <libflags/config/keys.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/video/rgba/color_filter.h>
#include <ascii-chat/version.h>
#include <defer.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

/**
 * Build the generic scalar/string portion of ascii-chat's TOML schema.
 *
 * The application registry remains authoritative for mode filtering and
 * callbacks while libflags owns storage binding and TOML conversion.
 */
flags_schema_builder_t *options_config_build_flags_schema(option_mode_bitmask_t mode_mask) {
  size_t count = 0;
  const config_option_metadata_t *metadata = config_schema_get_all(&count);
  flags_schema_builder_t *builder = flags_schema_builder_create();
  if (!builder)
    return NULL;
  if (options_registry_add_all_to_flags_builder(builder, mode_mask) != ASCIICHAT_OK) {
    flags_schema_builder_destroy(builder);
    return NULL;
  }

  /* Configuration keys are application policy; attach them to the generic
   * registry descriptors without duplicating their storage/type definitions. */
  flags_schema_t *schema = (flags_schema_t *)flags_schema_builder_schema(builder);
  for (size_t option_index = 0; option_index < schema->option_count; ++option_index) {
    flags_option_spec_t *option = (flags_option_spec_t *)&schema->options[option_index];
    char cli_flag[OPTIONS_CONFIG_QUESTION_BUFFER_SIZE];
    if (!flags_config_make_cli_flag(option->name, cli_flag, sizeof(cli_flag)))
      continue;
    for (size_t metadata_index = 0; metadata_index < count; ++metadata_index) {
      const config_option_metadata_t *meta = &metadata[metadata_index];
      if (meta->cli_flag && strcmp(meta->cli_flag, cli_flag) == 0) {
        option->config_key = meta->toml_key;
        break;
      }
    }
  }
  return builder;
}

/**
 * @brief Print configuration warning using the logging system
 *
 * Uses log_warn so that config warnings respect the --quiet flag and
 * are routed through the logging system for proper filtering.
 */
#define CONFIG_WARN(fmt, ...)                                                                                                                                                                          \
  do {                                                                                                                                                                                                 \
    log_warn("Config file: " fmt, ##__VA_ARGS__);                                                                                                                                                      \
  } while (0)

/**
 * @brief Print configuration debug message
 *
 * Debug messages use the logging system if it's initialized, otherwise
 * they are silently dropped.
 */
#define CONFIG_DEBUG(fmt, ...)                                                                                                                                                                         \
  do {                                                                                                                                                                                                 \
    /* Debug messages are only shown in debug builds after logging is initialized */                                                                                                                   \
    /* Use log_dev which safely checks initialization itself */                                                                                                                                        \
    log_dev(fmt, ##__VA_ARGS__);                                                                                                                                                                       \
  } while (0)

/** @} */

asciichat_error_t config_create_default(const char *config_path) {
  /*
   * The schema and serializer belong to libflags. ascii-chat only owns the
   * application-specific destination policy and overwrite confirmation.
   */
  char *expanded_path = NULL;
  defer(SAFE_FREE(expanded_path));

  const char *output_path = "-";
  if (config_path && config_path[0] != '\0') {
    expanded_path = expand_path(config_path);
    if (!expanded_path)
      expanded_path = platform_strdup(config_path);
    if (!expanded_path)
      return SET_ERRNO(ERROR_MEMORY, "Failed to resolve config file path");

    char *validated_path = NULL;
    asciichat_error_t validation = path_validate_user_path(expanded_path, PATH_ROLE_CONFIG_FILE, &validated_path);
    if (validation != ASCIICHAT_OK) {
      SAFE_FREE(validated_path);
      return validation;
    }
    SAFE_FREE(expanded_path);
    expanded_path = validated_path;
    output_path = expanded_path;

    struct stat st;
    if (stat(output_path, &st) == 0) {
      log_plain("Config file already exists: %s", output_path);
      char question[OPTIONS_CONFIG_QUESTION_BUFFER_SIZE];
      safe_snprintf(question, sizeof(question), "File already exists: %s\n\nOverwrite", output_path);
      if (!ui_notice_confirm_kind(UI_PROMPT_CONFIG_OVERWRITE, UI_NOTICE_WARNING, question, 30))
        return SET_ERRNO(ERROR_CONFIG, "User cancelled overwrite");
    }

    char *directory = platform_strdup(output_path);
    if (!directory)
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for config directory");
    char *separator = strrchr(directory, PATH_DELIM);
    if (separator) {
      *separator = '\0';
      asciichat_error_t mkdir_result = platform_mkdir_recursive(directory, DIR_PERM_PRIVATE);
      SAFE_FREE(directory);
      if (mkdir_result != ASCIICHAT_OK)
        return mkdir_result;
    } else {
      SAFE_FREE(directory);
    }
  }

  options_t defaults = options_t_new();
  flags_schema_builder_t *builder = options_config_build_flags_schema(OPTION_MODE_ALL);
  if (!builder)
    return SET_ERRNO(ERROR_CONFIG, "Failed to build configuration schema");
  flags_context_t *context = flags_create(flags_schema_builder_schema(builder), &defaults, sizeof(defaults));
  if (!context) {
    flags_schema_builder_destroy(builder);
    return SET_ERRNO(ERROR_CONFIG, "Failed to create configuration context");
  }

  const bool written = flags_config_write_toml(context, output_path);
  flags_destroy(context);
  flags_schema_builder_destroy(builder);
  if (!written)
    return SET_ERRNO(ERROR_CONFIG, "Failed to write configuration to '%s'", output_path);
  return ASCIICHAT_OK;
}

asciichat_error_t config_load_system_and_user(asciichat_mode_t detected_mode, bool strict, options_t *opts) {
  // Use platform abstraction to find all config.toml files across standard locations
  config_file_list_t config_files = {0};
  asciichat_error_t search_result = platform_find_config_file("config.toml", &config_files);
  defer(config_file_list_destroy(&config_files));

  // Keep the application config authoritative even when an older platform
  // runtime is present.  The explicit config path and generated config both
  // use this directory, so discovery must not silently fall back to the
  // generic "application" directory.
  if (search_result == ASCIICHAT_OK && config_files.count == 0) {
    char *config_dir = get_config_dir();
    if (config_dir) {
      size_t dir_len = strlen(config_dir);
      bool has_separator = dir_len > 0 && (config_dir[dir_len - 1] == '/' || config_dir[dir_len - 1] == '\\');
      size_t path_len = dir_len + (has_separator ? 0 : 1) + strlen("config.toml") + 1;
      char *path = SAFE_MALLOC(path_len, char *);
      if (path) {
        safe_snprintf(path, path_len, "%s%sconfig.toml", config_dir, has_separator ? "" : "\\");
        if (config_files.capacity > config_files.count) {
          config_file_result_t *file = &config_files.files[config_files.count++];
          file->path = path;
          file->priority = 0;
          file->exists = true;
          file->is_system_config = false;
          path = NULL;
        }
        SAFE_FREE(path);
      }
      SAFE_FREE(config_dir);
    }
  }

  if (search_result != ASCIICHAT_OK) {
    CONFIG_DEBUG("Failed to search for config files: %d", search_result);
    return search_result;
  }

  // Cascade load: Load all found configs in reverse order (lowest priority first)
  // This allows higher-priority configs to override lower-priority values.
  // Example: System configs load first, then user configs override them.

  asciichat_error_t result = ASCIICHAT_OK;
  for (size_t i = config_files.count; i > 0; i--) {
    const config_file_result_t *file = &config_files.files[i - 1];

    // Determine strictness based on whether this is a system or user config
    // System configs are non-strict (values can be missing, errors are non-fatal)
    // User config is strict or non-strict based on parameter
    bool is_user_config = !file->is_system_config;
    bool file_strict = is_user_config ? strict : false;

    CONFIG_DEBUG("Loading config from %s (system=%s, strict=%s)", file->path, file->is_system_config ? "yes" : "no", file_strict ? OPT_VALUE_TRUE : OPT_VALUE_FALSE);

    asciichat_errno_scope_t load_scope = errno_scope_begin();
    asciichat_error_t load_result = config_load_and_apply(detected_mode, file->path, file_strict, opts);

    if (load_result != ASCIICHAT_OK) {
      if (file_strict) {
        // Strict mode: errors are fatal
        CONFIG_DEBUG("Strict config loading failed for %s", file->path);
        result = load_result;
      } else {
        // Non-strict mode: errors are non-fatal, just log and continue
        CONFIG_DEBUG("Non-strict config loading warning for %s: %d (continuing)", file->path, load_result);
        errno_scope_end(load_scope, ASCIICHAT_ERRNO_DISMISSED);
        ASSERT_NO_ERRNO_SINCE(load_scope);
      }
    } else {
      ASSERT_NO_ERRNO_SINCE(load_scope);
      errno_scope_end(load_scope, ASCIICHAT_ERRNO_HANDLED);
    }
  }

  return result;
}
