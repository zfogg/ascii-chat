/**
 * @file schema.c
 * @brief Schema metadata registry for config file options
 * @ingroup options
 */

#include "options/schema.h"
#include "options/validation.h"
#include "options/options.h"
#include "util/path.h"
#include "video/palette.h"
#include "platform/terminal.h"
#include "common.h"

#include <string.h>
#include <limits.h>

// ============================================================================
// Validation Wrapper Functions
// ============================================================================

/**
 * @brief Wrapper for port validation
 */
static int validate_port_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                  char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  (void)parsed_value;
  return validate_opt_port(value_str, error_msg, error_msg_size);
}

/**
 * @brief Wrapper for IP address validation
 */
static int validate_ip_address_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                       char *error_msg, size_t error_msg_size) {
  (void)opts;
  char *parsed_addr = (char *)parsed_value;
  if (!parsed_addr) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Internal error: parsed_value buffer is NULL");
    }
    return -1;
  }
  // parsed_value should be a buffer of OPTIONS_BUFF_SIZE
  return validate_opt_ip_address(value_str, parsed_addr, OPTIONS_BUFF_SIZE, is_client, error_msg, error_msg_size);
}

/**
 * @brief Wrapper for positive integer validation
 */
static int validate_positive_int_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                          char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_positive_int(value_str, error_msg, error_msg_size);
  if (val >= 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for non-negative integer validation
 */
static int validate_non_negative_int_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                             char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_non_negative_int(value_str, error_msg, error_msg_size);
  if (val >= 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for device index validation
 */
static int validate_device_index_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                         char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_device_index(value_str, error_msg, error_msg_size);
  if (val != INT_MIN && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for color mode validation
 */
static int validate_color_mode_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                       char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_color_mode(value_str, error_msg, error_msg_size);
  if (val >= 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for render mode validation
 */
static int validate_render_mode_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                        char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_render_mode(value_str, error_msg, error_msg_size);
  if (val >= 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for palette validation
 */
static int validate_palette_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                     char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_palette(value_str, error_msg, error_msg_size);
  if (val >= 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for FPS validation
 */
static int validate_fps_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_fps(value_str, error_msg, error_msg_size);
  if (val > 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for float validation
 */
static int validate_float_non_negative_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                               char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  float *result = (float *)parsed_value;
  float val = validate_opt_float_non_negative(value_str, error_msg, error_msg_size);
  if (val >= 0.0f && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for password validation
 */
static int validate_password_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                      char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  (void)parsed_value;
  return validate_opt_password(value_str, error_msg, error_msg_size);
}

/**
 * @brief Wrapper for path/identifier validation (for keys, log files, etc.)
 * This validates that the value is a non-empty string. Path normalization
 * happens in the parser based on path role.
 */
static int validate_path_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                 char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Value is required");
    }
    return -1;
  }
  // Copy to parsed_value buffer for parser to use
  char *normalized = (char *)parsed_value;
  if (normalized) {
    SAFE_STRNCPY(normalized, value_str, OPTIONS_BUFF_SIZE);
  }
  return 0;
}

// ============================================================================
// Option Metadata Registry
// ============================================================================

static const config_option_metadata_t OPTIONS_METADATA[] = {
  // ========================================================================
  // Network Options
  // ========================================================================
  {
    .toml_key = "network.port",
    .cli_flag = "--port",
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "network",
    .field_offset = offsetof(options_t, port),
    .field_size = sizeof(((options_t *)0)->port),
    .validate_fn = validate_port_wrapper,
    .description = "Port number (1-65535)",
  },
  {
    .toml_key = "network.address",
    .cli_flag = NULL, // Legacy/positional
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_CONFIG, // Legacy fallback only
    .category = "network",
    .field_offset = offsetof(options_t, address),
    .field_size = sizeof(((options_t *)0)->address),
    .validate_fn = validate_ip_address_wrapper,
    .description = "Legacy network address (fallback)",
  },

  // ========================================================================
  // Server Options
  // ========================================================================
  {
    .toml_key = "server.bind_ipv4",
    .cli_flag = NULL, // Positional argument
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "server",
    .field_offset = offsetof(options_t, address),
    .field_size = sizeof(((options_t *)0)->address),
    .validate_fn = validate_ip_address_wrapper,
    .is_server_only = true,
    .description = "IPv4 bind address",
  },
  {
    .toml_key = "server.bind_ipv6",
    .cli_flag = NULL, // Positional argument
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "server",
    .field_offset = offsetof(options_t, address6),
    .field_size = sizeof(((options_t *)0)->address6),
    .validate_fn = validate_ip_address_wrapper,
    .is_server_only = true,
    .description = "IPv6 bind address",
  },

  // ========================================================================
  // Client Options
  // ========================================================================
  {
    .toml_key = "client.address",
    .cli_flag = NULL, // Positional argument
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, address),
    .field_size = sizeof(((options_t *)0)->address),
    .validate_fn = validate_ip_address_wrapper,
    .is_client_only = true,
    .description = "Server address to connect to",
  },
  {
    .toml_key = "client.width",
    .cli_flag = "--width",
    .type = OPTION_TYPE_INT,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, width),
    .field_size = sizeof(((options_t *)0)->width),
    .validate_fn = validate_positive_int_wrapper,
    .is_client_only = true,
    .description = "Terminal width in characters",
  },
  {
    .toml_key = "client.height",
    .cli_flag = "--height",
    .type = OPTION_TYPE_INT,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, height),
    .field_size = sizeof(((options_t *)0)->height),
    .validate_fn = validate_positive_int_wrapper,
    .is_client_only = true,
    .description = "Terminal height in characters",
  },
  {
    .toml_key = "client.webcam_index",
    .cli_flag = "--webcam-index",
    .type = OPTION_TYPE_INT,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, webcam_index),
    .field_size = sizeof(((options_t *)0)->webcam_index),
    .validate_fn = validate_non_negative_int_wrapper,
    .is_client_only = true,
    .description = "Webcam device index",
  },
  {
    .toml_key = "client.webcam_flip",
    .cli_flag = "--webcam-flip",
    .type = OPTION_TYPE_BOOL,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, webcam_flip),
    .field_size = sizeof(((options_t *)0)->webcam_flip),
    .validate_fn = NULL, // Boolean, no validation needed
    .is_client_only = true,
    .description = "Flip webcam image horizontally",
  },
  {
    .toml_key = "client.color_mode",
    .cli_flag = "--color-mode",
    .type = OPTION_TYPE_ENUM,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, color_mode),
    .field_size = sizeof(((options_t *)0)->color_mode),
    .validate_fn = validate_color_mode_wrapper,
    .is_client_only = true,
    .description = "Color mode (auto/none/16/256/truecolor)",
  },
  {
    .toml_key = "client.render_mode",
    .cli_flag = "--render-mode",
    .type = OPTION_TYPE_ENUM,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, render_mode),
    .field_size = sizeof(((options_t *)0)->render_mode),
    .validate_fn = validate_render_mode_wrapper,
    .is_client_only = true,
    .description = "Render mode (foreground/background/half-block)",
  },
  {
    .toml_key = "client.fps",
    .cli_flag = "--fps",
    .type = OPTION_TYPE_INT,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, fps),
    .field_size = sizeof(((options_t *)0)->fps),
    .validate_fn = validate_fps_wrapper,
    .constraints = {.int_range = {1, 144}},
    .is_client_only = true,
    .description = "Frames per second (1-144)",
  },
  {
    .toml_key = "client.stretch",
    .cli_flag = "--stretch",
    .type = OPTION_TYPE_BOOL,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, stretch),
    .field_size = sizeof(((options_t *)0)->stretch),
    .validate_fn = NULL,
    .is_client_only = true,
    .description = "Stretch video to terminal size",
  },
  {
    .toml_key = "client.quiet",
    .cli_flag = "--quiet",
    .type = OPTION_TYPE_BOOL,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, quiet),
    .field_size = sizeof(((options_t *)0)->quiet),
    .validate_fn = NULL,
    .is_client_only = true,
    .description = "Quiet mode (suppress logs)",
  },
  {
    .toml_key = "client.snapshot_mode",
    .cli_flag = "--snapshot",
    .type = OPTION_TYPE_BOOL,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, snapshot_mode),
    .field_size = sizeof(((options_t *)0)->snapshot_mode),
    .validate_fn = NULL,
    .is_client_only = true,
    .description = "Snapshot mode (capture one frame and exit)",
  },
  {
    .toml_key = "client.snapshot_delay",
    .cli_flag = "--snapshot-delay",
    .type = OPTION_TYPE_DOUBLE,
    .context = OPTION_CONTEXT_BOTH,
    .category = "client",
    .field_offset = offsetof(options_t, snapshot_delay),
    .field_size = sizeof(((options_t *)0)->snapshot_delay),
    .validate_fn = validate_float_non_negative_wrapper,
    .is_client_only = true,
    .description = "Snapshot delay in seconds",
  },

  // ========================================================================
  // Audio Options
  // ========================================================================
  {
    .toml_key = "audio.enabled",
    .cli_flag = "--audio",
    .type = OPTION_TYPE_BOOL,
    .context = OPTION_CONTEXT_BOTH,
    .category = "audio",
    .field_offset = offsetof(options_t, audio_enabled),
    .field_size = sizeof(((options_t *)0)->audio_enabled),
    .validate_fn = NULL,
    .is_client_only = true,
    .description = "Enable audio streaming",
  },
  {
    .toml_key = "audio.microphone_index",
    .cli_flag = "--microphone-index",
    .type = OPTION_TYPE_INT,
    .context = OPTION_CONTEXT_BOTH,
    .category = "audio",
    .field_offset = offsetof(options_t, microphone_index),
    .field_size = sizeof(((options_t *)0)->microphone_index),
    .validate_fn = validate_device_index_wrapper,
    .is_client_only = true,
    .description = "Microphone device index (-1 for default)",
  },
  {
    .toml_key = "audio.speakers_index",
    .cli_flag = "--speakers-index",
    .type = OPTION_TYPE_INT,
    .context = OPTION_CONTEXT_BOTH,
    .category = "audio",
    .field_offset = offsetof(options_t, speakers_index),
    .field_size = sizeof(((options_t *)0)->speakers_index),
    .validate_fn = validate_device_index_wrapper,
    .is_client_only = true,
    .description = "Speakers device index (-1 for default)",
  },

  // ========================================================================
  // Palette Options
  // ========================================================================
  {
    .toml_key = "palette.type",
    .cli_flag = "--palette",
    .type = OPTION_TYPE_ENUM,
    .context = OPTION_CONTEXT_BOTH,
    .category = "palette",
    .field_offset = offsetof(options_t, palette_type),
    .field_size = sizeof(((options_t *)0)->palette_type),
    .validate_fn = validate_palette_wrapper,
    .description = "Palette type (standard/blocks/digital/minimal/cool/custom)",
  },
  {
    .toml_key = "palette.chars",
    .cli_flag = "--palette-chars",
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "palette",
    .field_offset = offsetof(options_t, palette_custom),
    .field_size = sizeof(((options_t *)0)->palette_custom),
    .validate_fn = NULL, // Special handling in parser (length check)
    .description = "Custom palette characters",
  },

  // ========================================================================
  // Crypto Options
  // ========================================================================
  {
    .toml_key = "crypto.encrypt_enabled",
    .cli_flag = NULL, // Implicit via --key or --password
    .type = OPTION_TYPE_BOOL,
    .context = OPTION_CONTEXT_BOTH,
    .category = "crypto",
    .field_offset = offsetof(options_t, encrypt_enabled),
    .field_size = sizeof(((options_t *)0)->encrypt_enabled),
    .validate_fn = NULL,
    .description = "Enable encryption",
  },
  {
    .toml_key = "crypto.key",
    .cli_flag = "--key",
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "crypto",
    .field_offset = offsetof(options_t, encrypt_key),
    .field_size = sizeof(((options_t *)0)->encrypt_key),
    .validate_fn = validate_path_wrapper, // Handles both paths and identifiers
    .description = "Encryption key identifier or path",
  },
  {
    .toml_key = "crypto.password",
    .cli_flag = "--password",
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "crypto",
    .field_offset = offsetof(options_t, password),
    .field_size = sizeof(((options_t *)0)->password),
    .validate_fn = validate_password_wrapper,
    .description = "Password for encryption (WARNING: insecure in config!)",
  },
  {
    .toml_key = "crypto.keyfile",
    .cli_flag = "--keyfile",
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "crypto",
    .field_offset = offsetof(options_t, encrypt_keyfile),
    .field_size = sizeof(((options_t *)0)->encrypt_keyfile),
    .validate_fn = validate_path_wrapper,
    .description = "Path to key file",
  },
  {
    .toml_key = "crypto.no_encrypt",
    .cli_flag = "--no-encrypt",
    .type = OPTION_TYPE_BOOL,
    .context = OPTION_CONTEXT_BOTH,
    .category = "crypto",
    .field_offset = offsetof(options_t, no_encrypt),
    .field_size = sizeof(((options_t *)0)->no_encrypt),
    .validate_fn = NULL,
    .description = "Disable encryption",
  },
  {
    .toml_key = "crypto.server_key",
    .cli_flag = "--server-key",
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "crypto",
    .field_offset = offsetof(options_t, server_key),
    .field_size = sizeof(((options_t *)0)->server_key),
    .validate_fn = validate_path_wrapper,
    .is_client_only = true,
    .description = "Server public key (client only)",
  },
  {
    .toml_key = "crypto.client_keys",
    .cli_flag = "--client-keys",
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "crypto",
    .field_offset = offsetof(options_t, client_keys),
    .field_size = sizeof(((options_t *)0)->client_keys),
    .validate_fn = validate_path_wrapper,
    .is_server_only = true,
    .description = "Client keys directory (server only)",
  },

  // ========================================================================
  // Logging Options
  // ========================================================================
  {
    .toml_key = "log_file",
    .cli_flag = "--log-file",
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "logging",
    .field_offset = offsetof(options_t, log_file),
    .field_size = sizeof(((options_t *)0)->log_file),
    .validate_fn = validate_path_wrapper,
    .description = "Log file path",
  },
  {
    .toml_key = "logging.log_file",
    .cli_flag = "--log-file",
    .type = OPTION_TYPE_STRING,
    .context = OPTION_CONTEXT_BOTH,
    .category = "logging",
    .field_offset = offsetof(options_t, log_file),
    .field_size = sizeof(((options_t *)0)->log_file),
    .validate_fn = validate_path_wrapper,
    .description = "Log file path (alternative location)",
  },
};

#define OPTIONS_METADATA_COUNT (sizeof(OPTIONS_METADATA) / sizeof(OPTIONS_METADATA[0]))

// ============================================================================
// Lookup Functions
// ============================================================================

const config_option_metadata_t *config_schema_get_by_toml_key(const char *toml_key) {
  if (!toml_key) {
    return NULL;
  }

  for (size_t i = 0; i < OPTIONS_METADATA_COUNT; i++) {
    if (strcmp(OPTIONS_METADATA[i].toml_key, toml_key) == 0) {
      return &OPTIONS_METADATA[i];
    }
  }

  return NULL;
}

const config_option_metadata_t **config_schema_get_by_category(const char *category, size_t *count) {
  static const config_option_metadata_t *results[64]; // Max options per category
  size_t result_count = 0;

  if (!category) {
    if (count) {
      *count = 0;
    }
    return NULL;
  }

  for (size_t i = 0; i < OPTIONS_METADATA_COUNT && result_count < 64; i++) {
    if (strcmp(OPTIONS_METADATA[i].category, category) == 0) {
      results[result_count++] = &OPTIONS_METADATA[i];
    }
  }

  if (count) {
    *count = result_count;
  }

  return (result_count > 0) ? results : NULL;
}

const config_option_metadata_t *config_schema_get_all(size_t *count) {
  if (count) {
    *count = OPTIONS_METADATA_COUNT;
  }
  return OPTIONS_METADATA;
}
